#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatTypes.h"

struct FKasumiSplatPlyImportOptions
{
    double UnitsToCentimeters = 100.0;
    double DefaultSigmaCentimeters = 1.0;
    int64 MaxPointCount = 20'000'000;
    EKasumiSplatImportProfile Profile = EKasumiSplatImportProfile::Custom;
    bool bUseLumaAICoordinates = false;
    bool bSwapYZ = false;
    bool bFlipX = false;
    bool bFlipY = false;
    bool bFlipZ = false;
    bool bScaleValuesAreLogarithmic = true;
    bool bOpacityValuesAreLogits = true;
    bool bRgbValuesAreSRGB = false;
    bool bQuaternionWFirst = true;
    bool bSkipInvalidPoints = false;
    /** Called periodically while decoding vertices. Return false to cancel. */
    TFunction<bool(int64 ProcessedPointCount, int64 TotalPointCount)> ProgressCallback;
};

struct FKasumiSplatPlyImportResult
{
    TArray<FKasumiSplatPoint> Points;
    TArray<float> HigherOrderSH;
    FString Encoding;
    int32 HigherOrderSHCoefficientsPerPoint = 0;
    FMatrix LocalToSHDirection = FMatrix::Identity;
    EKasumiSplatImportProfile ResolvedProfile = EKasumiSplatImportProfile::Standard3DGS;
    int64 SkippedPointCount = 0;
};

class FKasumiSplatPlyParser
{
public:
    static bool Parse(
        const uint8* Data,
        int64 Size,
        const FKasumiSplatPlyImportOptions& Options,
        FKasumiSplatPlyImportResult& OutResult,
        FString& OutError);

    static bool Parse(
        TConstArrayView<uint8> Bytes,
        const FKasumiSplatPlyImportOptions& Options,
        FKasumiSplatPlyImportResult& OutResult,
        FString& OutError);
};
