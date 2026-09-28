#include "KasumiSplatSpzParser.h"

THIRD_PARTY_INCLUDES_START
#include "load-spz.h"
THIRD_PARTY_INCLUDES_END

namespace
{
    constexpr double ShDcFactor = 0.28209479177387814;

    float DecodeSigmoid(float Value)
    {
        return 1.0f / (1.0f + FMath::Exp(-FMath::Clamp(Value, -20.0f, 20.0f)));
    }

    bool IsFinitePoint(const spz::GaussianCloud& Cloud, int32 PointIndex)
    {
        const int64 PositionOffset = int64(PointIndex) * 3;
        return FMath::IsFinite(Cloud.positions[PositionOffset]) &&
            FMath::IsFinite(Cloud.positions[PositionOffset + 1]) &&
            FMath::IsFinite(Cloud.positions[PositionOffset + 2]);
    }

    uint32 ReadLittleEndianUint32(const uint8* Data)
    {
        return uint32(Data[0]) |
            (uint32(Data[1]) << 8) |
            (uint32(Data[2]) << 16) |
            (uint32(Data[3]) << 24);
    }
}

bool FKasumiSplatSpzParser::Parse(
    TConstArrayView<uint8> Bytes,
    const FKasumiSplatPlyImportOptions& Options,
    FKasumiSplatPlyImportResult& OutResult,
    FString& OutError)
{
    return Parse(Bytes.GetData(), int64(Bytes.Num()), Options, OutResult, OutError);
}

bool FKasumiSplatSpzParser::Parse(
    const uint8* Data,
    int64 Size,
    const FKasumiSplatPlyImportOptions& Options,
    FKasumiSplatPlyImportResult& OutResult,
    FString& OutError)
{
    OutResult = {};
    OutError.Reset();
    if (!Data || Size < 2)
    {
        OutError = TEXT("The file is too small to be an SPZ file.");
        return false;
    }

    const bool bVersion4 = Size >= 12 &&
        Data[0] == 'N' && Data[1] == 'G' && Data[2] == 'S' && Data[3] == 'P';
    const bool bLegacy = Data[0] == 0x1f && Data[1] == 0x8b;
    if (!bVersion4 && !bLegacy)
    {
        OutError = TEXT("Missing SPZ signature. Expected NGSP or a legacy gzip stream.");
        return false;
    }

    const int64 EffectivePointLimit = FMath::Min<int64>(Options.MaxPointCount, MAX_int32);
    if (bVersion4)
    {
        const uint32 HeaderPointCount = ReadLittleEndianUint32(Data + 8);
        if (HeaderPointCount == 0 || int64(HeaderPointCount) > EffectivePointLimit)
        {
            OutError = FString::Printf(
                TEXT("SPZ point count %u is outside the supported limit of %lld."),
                HeaderPointCount,
                EffectivePointLimit);
            return false;
        }
    }

    if (Options.ProgressCallback && !Options.ProgressCallback(0, 1))
    {
        OutError = TEXT("Import cancelled.");
        return false;
    }

    spz::UnpackOptions UnpackOptions;
    // Match the Standard 3DGS PLY path. This also rotates SH coefficients and
    // honors the SPZ_ADOBE_coordinate_system extension when it is present.
    UnpackOptions.to = spz::CoordinateSystem::RDF;
    const spz::GaussianCloud Cloud = spz::loadSpz(Data, size_t(Size), UnpackOptions);
    if (Cloud.numPoints <= 0)
    {
        OutError = TEXT("The SPZ decoder rejected the file or found no splats.");
        return false;
    }
    if (int64(Cloud.numPoints) > EffectivePointLimit)
    {
        OutError = FString::Printf(
            TEXT("SPZ point count %d is outside the supported limit of %lld."),
            Cloud.numPoints,
            EffectivePointLimit);
        return false;
    }

    const int64 PointCount = Cloud.numPoints;
    const int32 SourceCoefficientsPerChannel = Cloud.shDegree <= 0
        ? 0
        : FMath::Min(Cloud.shDegree * (Cloud.shDegree + 2), 24);
    const int32 RetainedCoefficientsPerChannel = FMath::Min(SourceCoefficientsPerChannel, 15);
    const int32 RetainedCoefficientsPerPoint = RetainedCoefficientsPerChannel * 3;
    if (Cloud.positions.size() != size_t(PointCount * 3) ||
        Cloud.scales.size() != size_t(PointCount * 3) ||
        Cloud.rotations.size() != size_t(PointCount * 4) ||
        Cloud.alphas.size() != size_t(PointCount) ||
        Cloud.colors.size() != size_t(PointCount * 3) ||
        Cloud.sh.size() != size_t(PointCount * SourceCoefficientsPerChannel * 3))
    {
        OutError = TEXT("The decoded SPZ attribute arrays are inconsistent.");
        return false;
    }

    OutResult.Points.Reserve(int32(PointCount));
    OutResult.HigherOrderSHCoefficientsPerPoint = RetainedCoefficientsPerPoint;
    if (RetainedCoefficientsPerPoint > 0)
    {
        const int64 ValueCount = PointCount * RetainedCoefficientsPerPoint;
        if (ValueCount > MAX_int32)
        {
            OutError = TEXT("Higher-order SPZ SH data exceeds the maximum array size supported by Unreal Engine.");
            return false;
        }
        OutResult.HigherOrderSH.Reserve(int32(ValueCount));
    }

    for (int32 PointIndex = 0; PointIndex < Cloud.numPoints; ++PointIndex)
    {
        if ((PointIndex & 4095) == 0 && Options.ProgressCallback &&
            !Options.ProgressCallback(PointIndex, PointCount))
        {
            OutError = TEXT("Import cancelled.");
            OutResult = {};
            return false;
        }
        if (!IsFinitePoint(Cloud, PointIndex))
        {
            if (Options.bSkipInvalidPoints)
            {
                ++OutResult.SkippedPointCount;
                continue;
            }
            OutError = FString::Printf(TEXT("SPZ splat %d has invalid position data."), PointIndex);
            OutResult = {};
            return false;
        }

        const int64 PositionOffset = int64(PointIndex) * 3;
        const int64 RotationOffset = int64(PointIndex) * 4;
        FKasumiSplatPoint Point;
        Point.StableId = PointIndex;
        Point.Position = FVector(
            Cloud.positions[PositionOffset],
            Cloud.positions[PositionOffset + 1],
            Cloud.positions[PositionOffset + 2]) * Options.UnitsToCentimeters;
        Point.Sigma = FVector(
            FMath::Exp(FMath::Clamp(Cloud.scales[PositionOffset], -20.0f, 20.0f)),
            FMath::Exp(FMath::Clamp(Cloud.scales[PositionOffset + 1], -20.0f, 20.0f)),
            FMath::Exp(FMath::Clamp(Cloud.scales[PositionOffset + 2], -20.0f, 20.0f))) * Options.UnitsToCentimeters;
        Point.Rotation = FQuat(
            Cloud.rotations[RotationOffset],
            Cloud.rotations[RotationOffset + 1],
            Cloud.rotations[RotationOffset + 2],
            Cloud.rotations[RotationOffset + 3]).GetNormalized();
        Point.Color = FLinearColor(
            0.5f + float(ShDcFactor) * Cloud.colors[PositionOffset],
            0.5f + float(ShDcFactor) * Cloud.colors[PositionOffset + 1],
            0.5f + float(ShDcFactor) * Cloud.colors[PositionOffset + 2],
            DecodeSigmoid(Cloud.alphas[PointIndex]));
        OutResult.Points.Add(MoveTemp(Point));

        // SPZ is coefficient-major RGB; Kasumi follows the channel-major f_rest PLY layout.
        const int64 SourceSHBase = int64(PointIndex) * SourceCoefficientsPerChannel * 3;
        for (int32 Channel = 0; Channel < 3; ++Channel)
        {
            for (int32 Coefficient = 0; Coefficient < RetainedCoefficientsPerChannel; ++Coefficient)
            {
                OutResult.HigherOrderSH.Add(Cloud.sh[SourceSHBase + Coefficient * 3 + Channel]);
            }
        }
    }

    if (Options.ProgressCallback && !Options.ProgressCallback(PointCount, PointCount))
    {
        OutError = TEXT("Import cancelled.");
        OutResult = {};
        return false;
    }

    OutResult.Encoding = bVersion4 ? TEXT("SPZ v4 (Zstandard)") : TEXT("SPZ legacy (gzip)");
    if (Cloud.shDegree > 3)
    {
        OutResult.Encoding += TEXT(", SH4 truncated to SH3");
    }
    if (Cloud.antialiased)
    {
        OutResult.Encoding += TEXT(", antialiased");
    }
    OutResult.LocalToSHDirection = FMatrix::Identity;
    OutResult.ResolvedProfile = EKasumiSplatImportProfile::Standard3DGS;
    return true;
}
