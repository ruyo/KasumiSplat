#include "KasumiSplatAsset.h"

#include "Async/Async.h"
#include "Math/Float16.h"
#include "Misc/ScopeLock.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

namespace
{
    struct FLegacyPackedKasumiSplatPoint
    {
        FVector3f Position;
        FVector4f Rotation;
        FVector3f Sigma;
        FVector4f Color;
        int32 StableId;
    };
#pragma pack(push, 1)
    struct FQuantizedKasumiSplatPoint
    {
        uint16 Position[3];
        uint32 Rotation = 0;
        uint16 LogSigma[3];
        uint16 Color[4];
        int32 StableId = 0;
    };
#pragma pack(pop)
    static_assert(sizeof(FQuantizedKasumiSplatPoint) == 28);

    constexpr int32 KasumiChunkPointCount = 65536;

#if WITH_EDITORONLY_DATA
    constexpr int32 KasumiThumbnailPointLimit = 2048;

    void BuildThumbnailPreview(
        const TArray<FKasumiSplatPoint>& Points,
        TArray<FKasumiSplatThumbnailPoint>& OutPoints)
    {
        OutPoints.Reset();
        if (Points.IsEmpty())
        {
            return;
        }

        const FVector ViewDirection = FVector(1.0, 1.0, 0.75).GetSafeNormal();
        const FVector Right = FVector(1.0, -1.0, 0.0).GetSafeNormal();
        const FVector Up = FVector::CrossProduct(Right, ViewDirection).GetSafeNormal();
        double MinU = TNumericLimits<double>::Max();
        double MaxU = TNumericLimits<double>::Lowest();
        double MinV = TNumericLimits<double>::Max();
        double MaxV = TNumericLimits<double>::Lowest();
        for (const FKasumiSplatPoint& Point : Points)
        {
            const double U = FVector::DotProduct(Point.Position, Right);
            const double V = FVector::DotProduct(Point.Position, Up);
            MinU = FMath::Min(MinU, U);
            MaxU = FMath::Max(MaxU, U);
            MinV = FMath::Min(MinV, V);
            MaxV = FMath::Max(MaxV, V);
        }

        const double CenterU = (MinU + MaxU) * 0.5;
        const double CenterV = (MinV + MaxV) * 0.5;
        const double Span = FMath::Max(FMath::Max(MaxU - MinU, MaxV - MinV), 1.0e-6);
        const int32 PreviewCount = FMath::Min(Points.Num(), KasumiThumbnailPointLimit);
        struct FPreviewCandidate
        {
            FKasumiSplatThumbnailPoint Point;
            double Depth = 0.0;
        };
        TArray<FPreviewCandidate> Candidates;
        Candidates.Reserve(PreviewCount);
        for (int32 PreviewIndex = 0; PreviewIndex < PreviewCount; ++PreviewIndex)
        {
            const int32 SourceIndex = int32((int64(PreviewIndex) * Points.Num()) / PreviewCount);
            const FKasumiSplatPoint& Source = Points[SourceIndex];
            FPreviewCandidate& Candidate = Candidates.AddDefaulted_GetRef();
            Candidate.Point.Position = FVector2f(
                float(0.5 + 0.9 * (FVector::DotProduct(Source.Position, Right) - CenterU) / Span),
                float(0.5 - 0.9 * (FVector::DotProduct(Source.Position, Up) - CenterV) / Span));
            Candidate.Point.Radius = float(FMath::Clamp(
                3.0 * Source.Sigma.GetAbs().GetMax() / Span,
                0.004,
                0.06));
            FLinearColor PreviewColor(
                FMath::Max(Source.Color.R, 0.0f),
                FMath::Max(Source.Color.G, 0.0f),
                FMath::Max(Source.Color.B, 0.0f),
                FMath::Clamp(Source.Color.A, 0.15f, 0.9f));
            Candidate.Point.Color = PreviewColor.ToFColorSRGB();
            Candidate.Depth = FVector::DotProduct(Source.Position, ViewDirection);
        }
        Candidates.StableSort([](const FPreviewCandidate& A, const FPreviewCandidate& B)
        {
            return A.Depth < B.Depth;
        });
        OutPoints.Reserve(Candidates.Num());
        for (FPreviewCandidate& Candidate : Candidates)
        {
            OutPoints.Add(MoveTemp(Candidate.Point));
        }
    }
#endif

    uint32 ExpandMortonBits(uint32 Value)
    {
        Value &= 0x000003ff;
        Value = (Value | (Value << 16)) & 0x030000ff;
        Value = (Value | (Value << 8)) & 0x0300f00f;
        Value = (Value | (Value << 4)) & 0x030c30c3;
        Value = (Value | (Value << 2)) & 0x09249249;
        return Value;
    }

    uint32 MortonKey(const FVector& Position, const FBox& Bounds)
    {
        const FVector Size = Bounds.GetSize();
        const FVector Normalized(
            Size.X > UE_SMALL_NUMBER ? (Position.X - Bounds.Min.X) / Size.X : 0.5,
            Size.Y > UE_SMALL_NUMBER ? (Position.Y - Bounds.Min.Y) / Size.Y : 0.5,
            Size.Z > UE_SMALL_NUMBER ? (Position.Z - Bounds.Min.Z) / Size.Z : 0.5);
        const uint32 X = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.X * 1023.0), 0, 1023));
        const uint32 Y = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.Y * 1023.0), 0, 1023));
        const uint32 Z = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.Z * 1023.0), 0, 1023));
        return ExpandMortonBits(X) | (ExpandMortonBits(Y) << 1) | (ExpandMortonBits(Z) << 2);
    }

    uint16 QuantizeRange(double Value, double Minimum, double Extent)
    {
        if (Extent <= UE_SMALL_NUMBER) return 0;
        return uint16(FMath::Clamp(FMath::RoundToInt((Value - Minimum) / Extent * 65535.0), 0, 65535));
    }

    double DecodeRange(uint16 Value, double Minimum, double Extent)
    {
        return Minimum + (double(Value) / 65535.0) * Extent;
    }

    uint32 EncodeSmallestThreeRotation(const FQuat& Value)
    {
        FQuat Rotation = Value.GetNormalized();
        double Components[4] = {Rotation.X, Rotation.Y, Rotation.Z, Rotation.W};
        int32 LargestIndex = 0;
        for (int32 Index = 1; Index < 4; ++Index)
        {
            if (FMath::Abs(Components[Index]) > FMath::Abs(Components[LargestIndex])) LargestIndex = Index;
        }
        if (Components[LargestIndex] < 0.0)
        {
            for (double& Component : Components) Component = -Component;
        }

        constexpr double Range = 0.7071067811865475244;
        uint32 Packed = uint32(LargestIndex);
        int32 StoredIndex = 0;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Index == LargestIndex) continue;
            const double Normalized = FMath::Clamp((Components[Index] + Range) / (2.0 * Range), 0.0, 1.0);
            const uint32 Encoded = uint32(FMath::Clamp(FMath::RoundToInt(Normalized * 1023.0), 0, 1023));
            Packed |= Encoded << (2 + StoredIndex * 10);
            ++StoredIndex;
        }
        return Packed;
    }

    FQuat DecodeSmallestThreeRotation(uint32 Packed)
    {
        constexpr double Range = 0.7071067811865475244;
        const int32 LargestIndex = int32(Packed & 3u);
        double Components[4] = {};
        double SumSquares = 0.0;
        int32 StoredIndex = 0;
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Index == LargestIndex) continue;
            const uint32 Encoded = (Packed >> (2 + StoredIndex * 10)) & 1023u;
            Components[Index] = (double(Encoded) / 1023.0) * (2.0 * Range) - Range;
            SumSquares += Components[Index] * Components[Index];
            ++StoredIndex;
        }
        Components[LargestIndex] = FMath::Sqrt(FMath::Max(0.0, 1.0 - SumSquares));
        return FQuat(Components[0], Components[1], Components[2], Components[3]).GetNormalized();
    }

    uint16 EncodeHalf(float Value)
    {
        FFloat16 Encoded;
        Encoded.SetClamped(FMath::IsFinite(Value) ? Value : 0.0f);
        return Encoded.Encoded;
    }

    float DecodeHalf(uint16 Value)
    {
        FFloat16 Decoded;
        Decoded.Encoded = Value;
        return Decoded.GetFloat();
    }

    int64 PointStrideForVersion(int32 Version)
    {
        return Version >= 2 ? sizeof(FQuantizedKasumiSplatPoint) : sizeof(FLegacyPackedKasumiSplatPoint);
    }

    int64 SHStrideForVersion(int32 Version)
    {
        return Version >= 2 ? sizeof(uint16) : sizeof(float);
    }

    void DecodeLegacyPackedPoints(
        const FLegacyPackedKasumiSplatPoint* Packed,
        int32 PointCount,
        TArray<FKasumiSplatPoint>& OutPoints)
    {
        const int32 OutputOffset = OutPoints.AddUninitialized(PointCount);
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            FKasumiSplatPoint& Point = OutPoints[OutputOffset + Index];
            Point.Position = FVector(Packed[Index].Position);
            Point.Rotation = FQuat(Packed[Index].Rotation.X, Packed[Index].Rotation.Y, Packed[Index].Rotation.Z, Packed[Index].Rotation.W);
            Point.Sigma = FVector(Packed[Index].Sigma);
            Point.Color = FLinearColor(Packed[Index].Color.X, Packed[Index].Color.Y, Packed[Index].Color.Z, Packed[Index].Color.W);
            Point.StableId = Packed[Index].StableId;
        }
    }

    void DecodeQuantizedPoints(
        const FQuantizedKasumiSplatPoint* Packed,
        int32 PointCount,
        const FKasumiSplatChunk& Chunk,
        TArray<FKasumiSplatPoint>& OutPoints)
    {
        const int32 OutputOffset = OutPoints.AddUninitialized(PointCount);
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            const FQuantizedKasumiSplatPoint& Source = Packed[Index];
            FKasumiSplatPoint& Point = OutPoints[OutputOffset + Index];
            Point.Position = FVector(
                DecodeRange(Source.Position[0], Chunk.QuantizedPositionMin.X, Chunk.QuantizedPositionExtent.X),
                DecodeRange(Source.Position[1], Chunk.QuantizedPositionMin.Y, Chunk.QuantizedPositionExtent.Y),
                DecodeRange(Source.Position[2], Chunk.QuantizedPositionMin.Z, Chunk.QuantizedPositionExtent.Z));
            Point.Rotation = DecodeSmallestThreeRotation(Source.Rotation);
            Point.Sigma = FVector(
                FMath::Exp(DecodeRange(Source.LogSigma[0], Chunk.QuantizedLogSigmaMin.X, Chunk.QuantizedLogSigmaExtent.X)),
                FMath::Exp(DecodeRange(Source.LogSigma[1], Chunk.QuantizedLogSigmaMin.Y, Chunk.QuantizedLogSigmaExtent.Y)),
                FMath::Exp(DecodeRange(Source.LogSigma[2], Chunk.QuantizedLogSigmaMin.Z, Chunk.QuantizedLogSigmaExtent.Z)));
            Point.Color = FLinearColor(
                DecodeHalf(Source.Color[0]),
                DecodeHalf(Source.Color[1]),
                DecodeHalf(Source.Color[2]),
                DecodeHalf(Source.Color[3]));
            Point.StableId = Source.StableId;
        }
    }

    void DecodeSHValues(const void* Data, int32 ValueCount, int32 Version, TArray<float>& OutValues)
    {
        if (Version >= 2)
        {
            const uint16* Source = static_cast<const uint16*>(Data);
            const int32 Offset = OutValues.AddUninitialized(ValueCount);
            for (int32 Index = 0; Index < ValueCount; ++Index)
            {
                OutValues[Offset + Index] = DecodeHalf(Source[Index]);
            }
        }
        else
        {
            OutValues.Append(static_cast<const float*>(Data), ValueCount);
        }
    }

    void PackSHValues(
        const void* Data,
        int32 PointCount,
        int32 CoefficientsPerPoint,
        int32 Version,
        TArray<uint32>& OutValues)
    {
        const int32 WordsPerPoint = FMath::DivideAndRoundUp(CoefficientsPerPoint, 2);
        const int32 OutputOffset = OutValues.AddZeroed(PointCount * WordsPerPoint);
        for (int32 PointIndex = 0; PointIndex < PointCount; ++PointIndex)
        {
            uint32* Target = OutValues.GetData() + OutputOffset + int64(PointIndex) * WordsPerPoint;
            for (int32 CoefficientIndex = 0; CoefficientIndex < CoefficientsPerPoint; CoefficientIndex += 2)
            {
                const int64 ValueIndex = int64(PointIndex) * CoefficientsPerPoint + CoefficientIndex;
                const uint32 Low = Version >= 2
                    ? static_cast<const uint16*>(Data)[ValueIndex]
                    : EncodeHalf(static_cast<const float*>(Data)[ValueIndex]);
                const uint32 High = CoefficientIndex + 1 < CoefficientsPerPoint
                    ? (Version >= 2
                        ? uint32(static_cast<const uint16*>(Data)[ValueIndex + 1])
                        : uint32(EncodeHalf(static_cast<const float*>(Data)[ValueIndex + 1])))
                    : 0u;
                Target[CoefficientIndex / 2] = Low | (High << 16u);
            }
        }
    }

    struct FKasumiChunkLoadState
    {
        explicit FKasumiChunkLoadState(UKasumiSplatAsset* InAsset)
            : Asset(InAsset)
        {
        }

        ~FKasumiChunkLoadState()
        {
            for (IBulkDataIORequest* Request : Requests)
            {
                delete Request;
            }
        }

        TStrongObjectPtr<UKasumiSplatAsset> Asset;
        UKasumiSplatAsset::FPackedChunkLoadCallback Callback;
        FCriticalSection Mutex;
        TArray<TArray<FKasumiSplatPoint>> Results;
        TArray<TArray<uint32>> SHResults;
        int32 SHCoefficientsPerPoint = 0;
        TArray<IBulkDataIORequest*> Requests;
        TAtomic<int32> Remaining = 0;
        TAtomic<bool> bSuccess = true;
    };

    void FinishChunkLoad(const TSharedRef<FKasumiChunkLoadState, ESPMode::ThreadSafe>& State)
    {
        if (--State->Remaining != 0)
        {
            return;
        }

        Async(EAsyncExecution::ThreadPool, [State]() mutable
        {
            TArray<FKasumiSplatPoint> Combined;
            TArray<uint32> CombinedSH;
            if (State->bSuccess.Load())
            {
                int32 Total = 0;
                for (const TArray<FKasumiSplatPoint>& Chunk : State->Results) Total += Chunk.Num();
                Combined.Reserve(Total);
                for (TArray<FKasumiSplatPoint>& Chunk : State->Results) Combined.Append(MoveTemp(Chunk));
                if (State->SHCoefficientsPerPoint > 0)
                {
                    const int32 WordsPerPoint = FMath::DivideAndRoundUp(State->SHCoefficientsPerPoint, 2);
                    CombinedSH.Reserve(Total * WordsPerPoint);
                    for (TArray<uint32>& Chunk : State->SHResults) CombinedSH.Append(MoveTemp(Chunk));
                }
            }
            AsyncTask(ENamedThreads::GameThread,
                [State, Combined = MoveTemp(Combined), CombinedSH = MoveTemp(CombinedSH)]() mutable
            {
                UKasumiSplatAsset::FPackedChunkLoadCallback Callback = MoveTemp(State->Callback);
                Callback(
                    State->bSuccess.Load(),
                    MoveTemp(Combined),
                    MoveTemp(CombinedSH),
                    State->SHCoefficientsPerPoint);
            });
        });
    }
}

UKasumiSplatAsset::UKasumiSplatAsset()
{
#if WITH_EDITORONLY_DATA
    AssetImportData = CreateEditorOnlyDefaultSubobject<UAssetImportData>(TEXT("AssetImportData"));
#endif
}

void UKasumiSplatAsset::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);
    // Version 1 assets ended after reflected properties and have no bulk payload.
    if (PointBulkDataVersion > 0)
    {
        PointBulkData.Serialize(Ar, this);
    }
    if (HigherOrderSHBulkDataVersion > 0)
    {
        HigherOrderSHBulkData.Serialize(Ar, this);
    }
}

bool UKasumiSplatAsset::SetImportedPoints(
    TArray<FKasumiSplatPoint>&& InPoints,
    const FString& InEncoding,
    double InUnitsToCentimeters,
    TArray<float>&& InHigherOrderSH,
    int32 InHigherOrderSHCoefficientsPerPoint,
    bool bInSpatiallySort,
    const FMatrix& InLocalToSHDirection,
    EKasumiSplatImportProfile InResolvedImportProfile,
    FKasumiSplatBuildProgress ProgressCallback)
{
    const int32 PointCount = InPoints.Num();
    const int32 SHCoefficientsPerPoint =
        InHigherOrderSHCoefficientsPerPoint > 0 &&
        int64(InHigherOrderSH.Num()) == int64(PointCount) * InHigherOrderSHCoefficientsPerPoint
            ? InHigherOrderSHCoefficientsPerPoint
            : 0;
    if (SHCoefficientsPerPoint == 0)
    {
        InHigherOrderSH.Reset();
    }

    const auto ReportProgress = [&ProgressCallback](
        EKasumiSplatBuildPhase Phase,
        int64 Processed,
        int64 Total)
    {
        return !ProgressCallback || ProgressCallback(Phase, Processed, Total);
    };
    const auto ShouldReport = [](int32 Index)
    {
        return (Index & 4095) == 0;
    };

    const bool bShouldSpatiallySort = bInSpatiallySort && PointCount > 1;
    TArray<int32> Order;
    if (bShouldSpatiallySort)
    {
        FBox SortBounds(ForceInit);
        if (!ReportProgress(EKasumiSplatBuildPhase::Bounds, 0, PointCount)) return false;
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            SortBounds += InPoints[Index].Position;
            if (ShouldReport(Index) &&
                !ReportProgress(EKasumiSplatBuildPhase::Bounds, Index, PointCount)) return false;
        }
        if (!ReportProgress(EKasumiSplatBuildPhase::Bounds, PointCount, PointCount)) return false;

        TArray<uint32> MortonKeys;
        MortonKeys.SetNumUninitialized(PointCount);
        Order.SetNumUninitialized(PointCount);
        if (!ReportProgress(EKasumiSplatBuildPhase::MortonKeys, 0, PointCount)) return false;
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            MortonKeys[Index] = MortonKey(InPoints[Index].Position, SortBounds);
            Order[Index] = Index;
            if (ShouldReport(Index) &&
                !ReportProgress(EKasumiSplatBuildPhase::MortonKeys, Index, PointCount)) return false;
        }
        if (!ReportProgress(EKasumiSplatBuildPhase::MortonKeys, PointCount, PointCount)) return false;

        // Stable least-significant-digit radix sort over the 30-bit Morton key.
        // Keys are calculated once; the original index resolves equal-key ordering.
        constexpr int32 BitsPerPass = 6;
        constexpr int32 BucketCount = 1 << BitsPerPass;
        constexpr int32 PassCount = 5;
        TArray<int32> ScratchOrder;
        ScratchOrder.SetNumUninitialized(PointCount);
        if (!ReportProgress(EKasumiSplatBuildPhase::Sort, 0, PassCount)) return false;
        for (int32 Pass = 0; Pass < PassCount; ++Pass)
        {
            int32 Counts[BucketCount] = {};
            const int32 Shift = Pass * BitsPerPass;
            for (const int32 SourceIndex : Order)
            {
                ++Counts[(MortonKeys[SourceIndex] >> Shift) & (BucketCount - 1)];
            }
            int32 Offsets[BucketCount];
            Offsets[0] = 0;
            for (int32 Bucket = 1; Bucket < BucketCount; ++Bucket)
            {
                Offsets[Bucket] = Offsets[Bucket - 1] + Counts[Bucket - 1];
            }
            for (const int32 SourceIndex : Order)
            {
                const uint32 Bucket = (MortonKeys[SourceIndex] >> Shift) & (BucketCount - 1);
                ScratchOrder[Offsets[Bucket]++] = SourceIndex;
            }
            Swap(Order, ScratchOrder);
            if (!ReportProgress(EKasumiSplatBuildPhase::Sort, Pass + 1, PassCount)) return false;
        }

        // Apply destination-to-source permutation in place. SH only needs one
        // point's coefficients as scratch instead of a second full SH array.
        TBitArray<> Visited(false, PointCount);
        TArray<float> SHScratch;
        if (SHCoefficientsPerPoint > 0) SHScratch.SetNumUninitialized(SHCoefficientsPerPoint);
        int32 ReorderedCount = 0;
        if (!ReportProgress(EKasumiSplatBuildPhase::Reorder, 0, PointCount)) return false;
        for (int32 Start = 0; Start < PointCount; ++Start)
        {
            if (Visited[Start]) continue;
            FKasumiSplatPoint PointScratch = MoveTemp(InPoints[Start]);
            if (SHCoefficientsPerPoint > 0)
            {
                FMemory::Memcpy(
                    SHScratch.GetData(),
                    InHigherOrderSH.GetData() + int64(Start) * SHCoefficientsPerPoint,
                    int64(SHCoefficientsPerPoint) * sizeof(float));
            }
            int32 Destination = Start;
            while (true)
            {
                Visited[Destination] = true;
                ++ReorderedCount;
                if (ShouldReport(ReorderedCount) &&
                    !ReportProgress(EKasumiSplatBuildPhase::Reorder, ReorderedCount, PointCount)) return false;
                const int32 Source = Order[Destination];
                if (Source == Start)
                {
                    InPoints[Destination] = MoveTemp(PointScratch);
                    if (SHCoefficientsPerPoint > 0)
                    {
                        FMemory::Memcpy(
                            InHigherOrderSH.GetData() + int64(Destination) * SHCoefficientsPerPoint,
                            SHScratch.GetData(),
                            int64(SHCoefficientsPerPoint) * sizeof(float));
                    }
                    break;
                }
                InPoints[Destination] = MoveTemp(InPoints[Source]);
                if (SHCoefficientsPerPoint > 0)
                {
                    FMemory::Memcpy(
                        InHigherOrderSH.GetData() + int64(Destination) * SHCoefficientsPerPoint,
                        InHigherOrderSH.GetData() + int64(Source) * SHCoefficientsPerPoint,
                        int64(SHCoefficientsPerPoint) * sizeof(float));
                }
                Destination = Source;
            }
        }
        if (!ReportProgress(EKasumiSplatBuildPhase::Reorder, PointCount, PointCount)) return false;
    }
    else
    {
        if (!ReportProgress(EKasumiSplatBuildPhase::Bounds, 1, 1) ||
            !ReportProgress(EKasumiSplatBuildPhase::MortonKeys, 1, 1) ||
            !ReportProgress(EKasumiSplatBuildPhase::Sort, 1, 1) ||
            !ReportProgress(EKasumiSplatBuildPhase::Reorder, 1, 1)) return false;
    }

    FBox NewLocalBounds(ForceInit);
    TArray<FKasumiSplatChunk> NewChunks;
    TArray<FQuantizedKasumiSplatPoint> Packed;
    Packed.SetNumUninitialized(PointCount);
    if (!ReportProgress(EKasumiSplatBuildPhase::Pack, 0, PointCount)) return false;
    for (int32 FirstPoint = 0; FirstPoint < PointCount; FirstPoint += KasumiChunkPointCount)
    {
        FKasumiSplatChunk& Chunk = NewChunks.AddDefaulted_GetRef();
        Chunk.FirstPoint = FirstPoint;
        Chunk.PointCount = FMath::Min(KasumiChunkPointCount, PointCount - FirstPoint);
        Chunk.LocalBounds.Init();
        FBox LogSigmaBounds(ForceInit);
        for (int32 LocalIndex = 0; LocalIndex < Chunk.PointCount; ++LocalIndex)
        {
            const FKasumiSplatPoint& Point = InPoints[FirstPoint + LocalIndex];
            Chunk.LocalBounds += Point.Position;
            NewLocalBounds += Point.Position;
            LogSigmaBounds += FVector(
                FMath::Loge(FMath::Max(Point.Sigma.X, 1.0e-12)),
                FMath::Loge(FMath::Max(Point.Sigma.Y, 1.0e-12)),
                FMath::Loge(FMath::Max(Point.Sigma.Z, 1.0e-12)));
        }
        Chunk.QuantizedPositionMin = Chunk.LocalBounds.Min;
        Chunk.QuantizedPositionExtent = Chunk.LocalBounds.GetSize();
        Chunk.QuantizedLogSigmaMin = LogSigmaBounds.Min;
        Chunk.QuantizedLogSigmaExtent = LogSigmaBounds.GetSize();
    }
    for (int32 Index = 0; Index < PointCount; ++Index)
    {
        const FKasumiSplatPoint& Point = InPoints[Index];
        const FKasumiSplatChunk& Chunk = NewChunks[Index / KasumiChunkPointCount];
        FQuantizedKasumiSplatPoint& Target = Packed[Index];
        Target.Position[0] = QuantizeRange(Point.Position.X, Chunk.QuantizedPositionMin.X, Chunk.QuantizedPositionExtent.X);
        Target.Position[1] = QuantizeRange(Point.Position.Y, Chunk.QuantizedPositionMin.Y, Chunk.QuantizedPositionExtent.Y);
        Target.Position[2] = QuantizeRange(Point.Position.Z, Chunk.QuantizedPositionMin.Z, Chunk.QuantizedPositionExtent.Z);
        Target.Rotation = EncodeSmallestThreeRotation(Point.Rotation);
        Target.LogSigma[0] = QuantizeRange(
            FMath::Loge(FMath::Max(Point.Sigma.X, 1.0e-12)),
            Chunk.QuantizedLogSigmaMin.X,
            Chunk.QuantizedLogSigmaExtent.X);
        Target.LogSigma[1] = QuantizeRange(
            FMath::Loge(FMath::Max(Point.Sigma.Y, 1.0e-12)),
            Chunk.QuantizedLogSigmaMin.Y,
            Chunk.QuantizedLogSigmaExtent.Y);
        Target.LogSigma[2] = QuantizeRange(
            FMath::Loge(FMath::Max(Point.Sigma.Z, 1.0e-12)),
            Chunk.QuantizedLogSigmaMin.Z,
            Chunk.QuantizedLogSigmaExtent.Z);
        Target.Color[0] = EncodeHalf(Point.Color.R);
        Target.Color[1] = EncodeHalf(Point.Color.G);
        Target.Color[2] = EncodeHalf(Point.Color.B);
        Target.Color[3] = EncodeHalf(Point.Color.A);
        Target.StableId = Point.StableId;
        if (ShouldReport(Index) && !ReportProgress(EKasumiSplatBuildPhase::Pack, Index, PointCount)) return false;
    }
    if (!ReportProgress(EKasumiSplatBuildPhase::Pack, PointCount, PointCount)) return false;

#if WITH_EDITORONLY_DATA
    TArray<FKasumiSplatThumbnailPoint> NewThumbnailPoints;
    if (!ReportProgress(EKasumiSplatBuildPhase::Thumbnail, 0, 1)) return false;
    BuildThumbnailPreview(InPoints, NewThumbnailPoints);
    if (!ReportProgress(EKasumiSplatBuildPhase::Thumbnail, 1, 1)) return false;
#else
    if (!ReportProgress(EKasumiSplatBuildPhase::Thumbnail, 1, 1)) return false;
#endif

    if (!ReportProgress(EKasumiSplatBuildPhase::BulkData, 0, 1)) return false;
    Modify();
    PackedPointCount = PointCount;
    PointBulkDataVersion = 2;
    SourceEncoding = InEncoding;
    ImportedUnitsToCentimeters = InUnitsToCentimeters;
    LocalToSHDirection = InLocalToSHDirection;
    ResolvedImportProfile = InResolvedImportProfile;
    HigherOrderSHCoefficientsPerPoint = SHCoefficientsPerPoint;
    bSpatiallySorted = bShouldSpatiallySort;
    DataVersion = CurrentDataVersion;
    ++Revision;
    LocalBounds = NewLocalBounds;
    Chunks = MoveTemp(NewChunks);
#if WITH_EDITORONLY_DATA
    ThumbnailPoints = MoveTemp(NewThumbnailPoints);
#endif

    PointBulkData.RemoveBulkData();
    PointBulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
    if (!Packed.IsEmpty())
    {
        void* Destination = PointBulkData.Lock(LOCK_READ_WRITE);
        Destination = PointBulkData.Realloc(int64(Packed.Num()) * sizeof(FQuantizedKasumiSplatPoint));
        FMemory::Memcpy(Destination, Packed.GetData(), int64(Packed.Num()) * sizeof(FQuantizedKasumiSplatPoint));
        PointBulkData.Unlock();
    }

    HigherOrderSHBulkData.RemoveBulkData();
    HigherOrderSHBulkDataVersion = HigherOrderSHCoefficientsPerPoint > 0 ? 2 : 0;
    if (!InHigherOrderSH.IsEmpty())
    {
        HigherOrderSHBulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
        void* Destination = HigherOrderSHBulkData.Lock(LOCK_READ_WRITE);
        Destination = HigherOrderSHBulkData.Realloc(int64(InHigherOrderSH.Num()) * sizeof(uint16));
        uint16* PackedSH = static_cast<uint16*>(Destination);
        for (int32 Index = 0; Index < InHigherOrderSH.Num(); ++Index)
        {
            PackedSH[Index] = EncodeHalf(InHigherOrderSH[Index]);
        }
        HigherOrderSHBulkData.Unlock();
    }

    Points.Reset();
    HigherOrderSH.Reset();
    ReportProgress(EKasumiSplatBuildPhase::BulkData, 1, 1);
    return true;
}

bool UKasumiSplatAsset::LoadPoints(TArray<FKasumiSplatPoint>& OutPoints) const
{
    if (PackedPointCount <= 0 || PointBulkData.GetBulkDataSize() <= 0)
    {
        OutPoints = Points;
        return !OutPoints.IsEmpty();
    }

    const int64 ExpectedSize = int64(PackedPointCount) * PointStrideForVersion(PointBulkDataVersion);
    if (PointBulkData.GetBulkDataSize() != ExpectedSize)
    {
        return false;
    }

    FScopeLock BulkDataLock(&BulkDataCriticalSection);
    const void* Packed = PointBulkData.LockReadOnly();
    if (!Packed)
    {
        return false;
    }
    OutPoints.Reset();
    OutPoints.Reserve(PackedPointCount);
    if (PointBulkDataVersion >= 2)
    {
        const FQuantizedKasumiSplatPoint* Quantized = static_cast<const FQuantizedKasumiSplatPoint*>(Packed);
        int32 DecodedCount = 0;
        for (const FKasumiSplatChunk& Chunk : Chunks)
        {
            if (Chunk.FirstPoint != DecodedCount || Chunk.PointCount < 0 ||
                int64(Chunk.FirstPoint) + Chunk.PointCount > PackedPointCount)
            {
                PointBulkData.Unlock();
                OutPoints.Reset();
                return false;
            }
            DecodeQuantizedPoints(Quantized + Chunk.FirstPoint, Chunk.PointCount, Chunk, OutPoints);
            DecodedCount += Chunk.PointCount;
        }
        if (DecodedCount != PackedPointCount)
        {
            PointBulkData.Unlock();
            OutPoints.Reset();
            return false;
        }
    }
    else
    {
        DecodeLegacyPackedPoints(
            static_cast<const FLegacyPackedKasumiSplatPoint*>(Packed),
            PackedPointCount,
            OutPoints);
    }
    PointBulkData.Unlock();
    return true;
}

int32 UKasumiSplatAsset::GetHigherOrderSHCoefficientsPerPoint() const
{
    if (HigherOrderSHCoefficientsPerPoint > 0)
    {
        return HigherOrderSHCoefficientsPerPoint;
    }
    const int32 PointCount = GetPointCount();
    return PointCount > 0 && HigherOrderSH.Num() % PointCount == 0
        ? FMath::Clamp(HigherOrderSH.Num() / PointCount, 0, 45)
        : 0;
}

bool UKasumiSplatAsset::LoadHigherOrderSH(TArray<float>& OutHigherOrderSH) const
{
    OutHigherOrderSH.Reset();
    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    if (CoefficientsPerPoint <= 0) return true;
    if (HigherOrderSHBulkDataVersion <= 0)
    {
        OutHigherOrderSH = HigherOrderSH;
        return OutHigherOrderSH.Num() == GetPointCount() * CoefficientsPerPoint;
    }
    const int64 ValueCount = int64(GetPointCount()) * CoefficientsPerPoint;
    if (ValueCount > MAX_int32) return false;
    if (HigherOrderSHBulkData.GetBulkDataSize() != ValueCount * SHStrideForVersion(HigherOrderSHBulkDataVersion)) return false;
    FScopeLock BulkDataLock(&BulkDataCriticalSection);
    const void* Source = HigherOrderSHBulkData.LockReadOnly();
    if (!Source) return false;
    DecodeSHValues(Source, int32(ValueCount), HigherOrderSHBulkDataVersion, OutHigherOrderSH);
    HigherOrderSHBulkData.Unlock();
    return true;
}

bool UKasumiSplatAsset::LoadHigherOrderSHPacked(TArray<uint32>& OutPackedHigherOrderSH) const
{
    OutPackedHigherOrderSH.Reset();
    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    const int32 PointCount = GetPointCount();
    if (CoefficientsPerPoint <= 0) return true;
    if (PointCount <= 0 || int64(PointCount) * FMath::DivideAndRoundUp(CoefficientsPerPoint, 2) > MAX_int32)
    {
        return false;
    }
    if (HigherOrderSHBulkDataVersion <= 0)
    {
        if (HigherOrderSH.Num() != PointCount * CoefficientsPerPoint) return false;
        PackSHValues(HigherOrderSH.GetData(), PointCount, CoefficientsPerPoint, 1, OutPackedHigherOrderSH);
        return true;
    }
    const int64 ValueCount = int64(PointCount) * CoefficientsPerPoint;
    if (HigherOrderSHBulkData.GetBulkDataSize() != ValueCount * SHStrideForVersion(HigherOrderSHBulkDataVersion))
    {
        return false;
    }
    FScopeLock BulkDataLock(&BulkDataCriticalSection);
    const void* Source = HigherOrderSHBulkData.LockReadOnly();
    if (!Source) return false;
    PackSHValues(Source, PointCount, CoefficientsPerPoint, HigherOrderSHBulkDataVersion, OutPackedHigherOrderSH);
    HigherOrderSHBulkData.Unlock();
    return true;
}

bool UKasumiSplatAsset::LoadPointChunks(
    const TArray<int32>& ChunkIndices,
    TArray<FKasumiSplatPoint>& OutPoints,
    TArray<float>* OutHigherOrderSH) const
{
    OutPoints.Reset();
    if (OutHigherOrderSH) OutHigherOrderSH->Reset();
    if (PointBulkDataVersion <= 0 || PackedPointCount <= 0 || Chunks.IsEmpty())
    {
        return LoadPoints(OutPoints);
    }

    const int64 PointStride = PointStrideForVersion(PointBulkDataVersion);
    if (PointBulkData.GetBulkDataSize() != int64(PackedPointCount) * PointStride)
    {
        return false;
    }

    int64 TotalPoints64 = 0;
    for (const int32 ChunkIndex : ChunkIndices)
    {
        if (!Chunks.IsValidIndex(ChunkIndex)) return false;
        const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
        if (Chunk.FirstPoint < 0 || Chunk.PointCount < 0 ||
            int64(Chunk.FirstPoint) + Chunk.PointCount > PackedPointCount)
        {
            return false;
        }
        TotalPoints64 += Chunk.PointCount;
        if (TotalPoints64 > MAX_int32) return false;
    }
    const int32 TotalPoints = int32(TotalPoints64);
    OutPoints.Reserve(TotalPoints);

    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    if (int64(TotalPoints) * CoefficientsPerPoint > MAX_int32) return false;
    if (OutHigherOrderSH && CoefficientsPerPoint > 0 && HigherOrderSHBulkDataVersion > 0)
    {
        const int64 ExpectedSHBytes = int64(PackedPointCount) * CoefficientsPerPoint *
            SHStrideForVersion(HigherOrderSHBulkDataVersion);
        if (HigherOrderSHBulkData.GetBulkDataSize() != ExpectedSHBytes) return false;
    }

    FScopeLock BulkDataLock(&BulkDataCriticalSection);

    const void* Packed = PointBulkData.LockReadOnly();
    if (!Packed) return false;
    for (const int32 ChunkIndex : ChunkIndices)
    {
        const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
        if (PointBulkDataVersion >= 2)
        {
            DecodeQuantizedPoints(
                static_cast<const FQuantizedKasumiSplatPoint*>(Packed) + Chunk.FirstPoint,
                Chunk.PointCount,
                Chunk,
                OutPoints);
        }
        else
        {
            DecodeLegacyPackedPoints(
                static_cast<const FLegacyPackedKasumiSplatPoint*>(Packed) + Chunk.FirstPoint,
                Chunk.PointCount,
                OutPoints);
        }
    }
    PointBulkData.Unlock();

    if (OutHigherOrderSH)
    {
        if (CoefficientsPerPoint > 0)
        {
            if (HigherOrderSHBulkDataVersion > 0)
            {
                const void* SH = HigherOrderSHBulkData.LockReadOnly();
                if (!SH) return false;
                OutHigherOrderSH->Reserve(TotalPoints * CoefficientsPerPoint);
                for (const int32 ChunkIndex : ChunkIndices)
                {
                    const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
                    const int64 ValueOffset = int64(Chunk.FirstPoint) * CoefficientsPerPoint;
                    const uint8* ByteSource = static_cast<const uint8*>(SH) +
                        ValueOffset * SHStrideForVersion(HigherOrderSHBulkDataVersion);
                    DecodeSHValues(
                        ByteSource,
                        Chunk.PointCount * CoefficientsPerPoint,
                        HigherOrderSHBulkDataVersion,
                        *OutHigherOrderSH);
                }
                HigherOrderSHBulkData.Unlock();
            }
            else
            {
                OutHigherOrderSH->Reserve(TotalPoints * CoefficientsPerPoint);
                for (const int32 ChunkIndex : ChunkIndices)
                {
                    const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
                    const int64 Offset = int64(Chunk.FirstPoint) * CoefficientsPerPoint;
                    if (Offset + int64(Chunk.PointCount) * CoefficientsPerPoint > HigherOrderSH.Num()) return false;
                    OutHigherOrderSH->Append(HigherOrderSH.GetData() + Offset, Chunk.PointCount * CoefficientsPerPoint);
                }
            }
        }
    }
    return true;
}

bool UKasumiSplatAsset::LoadPointChunksPacked(
    const TArray<int32>& ChunkIndices,
    TArray<FKasumiSplatPoint>& OutPoints,
    TArray<uint32>* OutPackedHigherOrderSH) const
{
    if (OutPackedHigherOrderSH) OutPackedHigherOrderSH->Reset();
    if (!LoadPointChunks(ChunkIndices, OutPoints, nullptr)) return false;
    if (!OutPackedHigherOrderSH) return true;

    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    if (CoefficientsPerPoint <= 0) return true;
    const int32 WordsPerPoint = FMath::DivideAndRoundUp(CoefficientsPerPoint, 2);
    if (int64(OutPoints.Num()) * WordsPerPoint > MAX_int32) return false;
    OutPackedHigherOrderSH->Reserve(OutPoints.Num() * WordsPerPoint);

    if (HigherOrderSHBulkDataVersion > 0)
    {
        const int64 ExpectedSHBytes = int64(PackedPointCount) * CoefficientsPerPoint *
            SHStrideForVersion(HigherOrderSHBulkDataVersion);
        if (HigherOrderSHBulkData.GetBulkDataSize() != ExpectedSHBytes) return false;
        FScopeLock BulkDataLock(&BulkDataCriticalSection);
        const void* SH = HigherOrderSHBulkData.LockReadOnly();
        if (!SH) return false;
        for (const int32 ChunkIndex : ChunkIndices)
        {
            if (!Chunks.IsValidIndex(ChunkIndex))
            {
                HigherOrderSHBulkData.Unlock();
                return false;
            }
            const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
            const int64 ValueOffset = int64(Chunk.FirstPoint) * CoefficientsPerPoint;
            const uint8* ByteSource = static_cast<const uint8*>(SH) +
                ValueOffset * SHStrideForVersion(HigherOrderSHBulkDataVersion);
            PackSHValues(
                ByteSource,
                Chunk.PointCount,
                CoefficientsPerPoint,
                HigherOrderSHBulkDataVersion,
                *OutPackedHigherOrderSH);
        }
        HigherOrderSHBulkData.Unlock();
        return true;
    }

    for (const int32 ChunkIndex : ChunkIndices)
    {
        if (!Chunks.IsValidIndex(ChunkIndex)) return false;
        const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
        const int64 Offset = int64(Chunk.FirstPoint) * CoefficientsPerPoint;
        if (Offset + int64(Chunk.PointCount) * CoefficientsPerPoint > HigherOrderSH.Num()) return false;
        PackSHValues(
            HigherOrderSH.GetData() + Offset,
            Chunk.PointCount,
            CoefficientsPerPoint,
            1,
            *OutPackedHigherOrderSH);
    }
    return true;
}

void UKasumiSplatAsset::LoadPointChunksAsync(
    const TArray<int32>& ChunkIndices,
    FChunkLoadCallback&& Callback) const
{
    LoadPointChunksPackedAsync(ChunkIndices,
        [Callback = MoveTemp(Callback)](
            bool bSuccess,
            TArray<FKasumiSplatPoint>&& Points,
            TArray<uint32>&& PackedSH,
            int32 CoefficientsPerPoint) mutable
    {
        TArray<float> ExpandedSH;
        if (bSuccess && CoefficientsPerPoint > 0)
        {
            const int32 WordsPerPoint = FMath::DivideAndRoundUp(CoefficientsPerPoint, 2);
            if (PackedSH.Num() != Points.Num() * WordsPerPoint)
            {
                bSuccess = false;
            }
            else
            {
                ExpandedSH.SetNumUninitialized(Points.Num() * CoefficientsPerPoint);
                for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
                {
                    for (int32 CoefficientIndex = 0; CoefficientIndex < CoefficientsPerPoint; ++CoefficientIndex)
                    {
                        const uint32 Word = PackedSH[int64(PointIndex) * WordsPerPoint + CoefficientIndex / 2];
                        ExpandedSH[int64(PointIndex) * CoefficientsPerPoint + CoefficientIndex] = DecodeHalf(
                            uint16(CoefficientIndex & 1 ? Word >> 16u : Word & 0xffffu));
                    }
                }
            }
        }
        Callback(bSuccess, MoveTemp(Points), MoveTemp(ExpandedSH), CoefficientsPerPoint);
    });
}

void UKasumiSplatAsset::LoadPointChunksPackedAsync(
    const TArray<int32>& ChunkIndices,
    FPackedChunkLoadCallback&& Callback) const
{
    TArray<int32> ValidIndices;
    ValidIndices.Reserve(ChunkIndices.Num());
    for (const int32 ChunkIndex : ChunkIndices)
    {
        if (Chunks.IsValidIndex(ChunkIndex)) ValidIndices.AddUnique(ChunkIndex);
    }

    bool bPayloadValid = PointBulkDataVersion > 0 && PackedPointCount > 0 &&
        PointBulkData.GetBulkDataSize() == int64(PackedPointCount) * PointStrideForVersion(PointBulkDataVersion);
    for (const int32 ChunkIndex : ValidIndices)
    {
        const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
        bPayloadValid &= Chunk.FirstPoint >= 0 && Chunk.PointCount >= 0 &&
            int64(Chunk.FirstPoint) + Chunk.PointCount <= PackedPointCount;
    }
    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    if (bPayloadValid && CoefficientsPerPoint > 0 && HigherOrderSHBulkDataVersion > 0)
    {
        bPayloadValid = HigherOrderSHBulkData.GetBulkDataSize() ==
            int64(PackedPointCount) * CoefficientsPerPoint * SHStrideForVersion(HigherOrderSHBulkDataVersion);
    }

    if (ValidIndices.IsEmpty() || !bPayloadValid)
    {
        TStrongObjectPtr<UKasumiSplatAsset> StrongAsset(const_cast<UKasumiSplatAsset*>(this));
        Async(EAsyncExecution::ThreadPool,
            [StrongAsset = MoveTemp(StrongAsset), ValidIndices = MoveTemp(ValidIndices), Callback = MoveTemp(Callback)]() mutable
        {
            TArray<FKasumiSplatPoint> Loaded;
            TArray<uint32> LoadedSH;
            const bool bLoaded = StrongAsset->LoadPointChunksPacked(ValidIndices, Loaded, &LoadedSH);
            const int32 SHCount = StrongAsset->GetHigherOrderSHCoefficientsPerPoint();
            AsyncTask(ENamedThreads::GameThread,
                [Callback = MoveTemp(Callback), bLoaded, Loaded = MoveTemp(Loaded), LoadedSH = MoveTemp(LoadedSH), SHCount]() mutable
            {
                Callback(bLoaded, MoveTemp(Loaded), MoveTemp(LoadedSH), SHCount);
            });
        });
        return;
    }

    TSharedRef<FKasumiChunkLoadState, ESPMode::ThreadSafe> State =
        MakeShared<FKasumiChunkLoadState, ESPMode::ThreadSafe>(const_cast<UKasumiSplatAsset*>(this));
    State->Callback = MoveTemp(Callback);
    State->Results.SetNum(ValidIndices.Num());
    State->SHCoefficientsPerPoint = CoefficientsPerPoint;
    State->SHResults.SetNum(ValidIndices.Num());
    const bool bStreamSH = State->SHCoefficientsPerPoint > 0 && HigherOrderSHBulkDataVersion > 0;
    State->Remaining = ValidIndices.Num() * (bStreamSH ? 2 : 1);

    if (PointBulkData.IsBulkDataLoaded() || (State->SHCoefficientsPerPoint > 0 && !bStreamSH) ||
        (bStreamSH && HigherOrderSHBulkData.IsBulkDataLoaded()))
    {
        Async(EAsyncExecution::ThreadPool, [State, ValidIndices = MoveTemp(ValidIndices)]() mutable
        {
            TArray<FKasumiSplatPoint> Loaded;
            TArray<uint32> LoadedSH;
            const bool bLoaded = State->Asset->LoadPointChunksPacked(ValidIndices, Loaded, &LoadedSH);
            State->bSuccess = bLoaded;
            if (bLoaded) State->Results[0] = MoveTemp(Loaded);
            if (bLoaded) State->SHResults[0] = MoveTemp(LoadedSH);
            State->Remaining = 1;
            FinishChunkLoad(State);
        });
        return;
    }

    State->Requests.Reserve(ValidIndices.Num());
    for (int32 RequestIndex = 0; RequestIndex < ValidIndices.Num(); ++RequestIndex)
    {
        const FKasumiSplatChunk Chunk = Chunks[ValidIndices[RequestIndex]];
        const int32 PointVersion = PointBulkDataVersion;
        const int64 PointStride = PointStrideForVersion(PointVersion);
        const int64 Offset = int64(Chunk.FirstPoint) * PointStride;
        const int64 Bytes = int64(Chunk.PointCount) * PointStride;
        FBulkDataIORequestCallBack RequestCallback =
            [State, RequestIndex, Chunk, PointVersion](bool bWasCancelled, IBulkDataIORequest* Request)
        {
            uint8* Memory = bWasCancelled ? nullptr : Request->GetReadResults();
            if (Memory)
            {
                if (PointVersion >= 2)
                {
                    DecodeQuantizedPoints(
                        reinterpret_cast<const FQuantizedKasumiSplatPoint*>(Memory),
                        Chunk.PointCount,
                        Chunk,
                        State->Results[RequestIndex]);
                }
                else
                {
                    DecodeLegacyPackedPoints(
                        reinterpret_cast<const FLegacyPackedKasumiSplatPoint*>(Memory),
                        Chunk.PointCount,
                        State->Results[RequestIndex]);
                }
                FMemory::Free(Memory);
            }
            else
            {
                State->bSuccess = false;
            }
            FinishChunkLoad(State);
        };
        IBulkDataIORequest* Request = PointBulkData.CreateStreamingRequest(
            Offset,
            Bytes,
            AIOP_Normal,
            &RequestCallback,
            nullptr);
        State->Requests.Add(Request);
        if (!Request)
        {
            State->bSuccess = false;
            FinishChunkLoad(State);
        }

        if (bStreamSH)
        {
            const int32 SHVersion = HigherOrderSHBulkDataVersion;
            const int64 SHStride = SHStrideForVersion(SHVersion);
            const int64 SHOffset = int64(Chunk.FirstPoint) * State->SHCoefficientsPerPoint * SHStride;
            const int64 SHBytes = int64(Chunk.PointCount) * State->SHCoefficientsPerPoint * SHStride;
            FBulkDataIORequestCallBack SHRequestCallback =
                [State, RequestIndex, SHVersion, ValueCount = Chunk.PointCount * State->SHCoefficientsPerPoint](bool bWasCancelled, IBulkDataIORequest* SHRequest)
            {
                uint8* Memory = bWasCancelled ? nullptr : SHRequest->GetReadResults();
                if (Memory)
                {
                    PackSHValues(
                        Memory,
                        ValueCount / State->SHCoefficientsPerPoint,
                        State->SHCoefficientsPerPoint,
                        SHVersion,
                        State->SHResults[RequestIndex]);
                    FMemory::Free(Memory);
                }
                else
                {
                    State->bSuccess = false;
                }
                FinishChunkLoad(State);
            };
            IBulkDataIORequest* SHRequest = HigherOrderSHBulkData.CreateStreamingRequest(
                SHOffset,
                SHBytes,
                AIOP_Normal,
                &SHRequestCallback,
                nullptr);
            State->Requests.Add(SHRequest);
            if (!SHRequest)
            {
                State->bSuccess = false;
                FinishChunkLoad(State);
            }
        }
    }
}
