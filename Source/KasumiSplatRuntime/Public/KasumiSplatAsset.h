#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatTypes.h"
#include "Serialization/BulkData.h"
#include "KasumiSplatAsset.generated.h"

class UAssetImportData;

/** Imported, immutable source data for a Gaussian splat capture. */
UCLASS(BlueprintType)
class KASUMISPLATRUNTIME_API UKasumiSplatAsset : public UObject
{
    GENERATED_BODY()

public:
    UKasumiSplatAsset();

    static constexpr int32 CurrentDataVersion = 6;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    int32 DataVersion = CurrentDataVersion;

    /** Incremented after each successful import so registered components can refresh. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    int32 Revision = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    FString SourceEncoding;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    double ImportedUnitsToCentimeters = 100.0;

    /** Stable key derived from source metadata, importer settings, and the asset data version. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Import")
    FString ImportFingerprint;

    /** Profile actually used to interpret the most recent import. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Import")
    EKasumiSplatImportProfile ResolvedImportProfile = EKasumiSplatImportProfile::Standard3DGS;

    /** Importer choices restored when this asset is reimported. */
    UPROPERTY(VisibleAnywhere, Category="KasumiSplat|Import")
    FKasumiSplatImportSettingsSnapshot ImportSettings;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    FBox LocalBounds = FBox(ForceInit);

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    TArray<FKasumiSplatPoint> Points;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Streaming")
    int32 PackedPointCount = 0;

    /** Zero on legacy assets; positive values declare that a serialized bulk payload follows. */
    UPROPERTY()
    int32 PointBulkDataVersion = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Streaming")
    TArray<FKasumiSplatChunk> Chunks;

    /** True when import order was converted to Morton order for spatial streaming. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Streaming")
    bool bSpatiallySorted = false;

    /** Flattened f_rest coefficients retained for future view-dependent SH evaluation. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|SphericalHarmonics")
    TArray<float> HigherOrderSH;

    /** Number of flattened f_rest values stored for each point. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|SphericalHarmonics")
    int32 HigherOrderSHCoefficientsPerPoint = 0;

    /** Converts a local Unreal view direction back to the coordinate system used by imported SH coefficients. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|SphericalHarmonics")
    FMatrix LocalToSHDirection = FMatrix::Identity;

    /** Zero on assets that still keep SH values in the legacy reflected array. */
    UPROPERTY()
    int32 HigherOrderSHBulkDataVersion = 0;

#if WITH_EDITORONLY_DATA
    UPROPERTY(VisibleAnywhere, Instanced, Category="ImportSettings")
    TObjectPtr<UAssetImportData> AssetImportData;
#endif

    UFUNCTION(BlueprintPure, Category="KasumiSplat")
    int32 GetPointCount() const { return PackedPointCount > 0 ? PackedPointCount : Points.Num(); }

    UFUNCTION(BlueprintPure, Category="KasumiSplat|SphericalHarmonics")
    int32 GetHigherOrderSHCoefficientsPerPoint() const;

    virtual void Serialize(FArchive& Ar) override;

    void SetImportedPoints(
        TArray<FKasumiSplatPoint>&& InPoints,
        const FString& InEncoding,
        double InUnitsToCentimeters,
        TArray<float>&& InHigherOrderSH = {},
        int32 InHigherOrderSHCoefficientsPerPoint = 0,
        bool bInSpatiallySort = true,
        const FMatrix& InLocalToSHDirection = FMatrix::Identity,
        EKasumiSplatImportProfile InResolvedImportProfile = EKasumiSplatImportProfile::Standard3DGS);
    bool LoadPoints(TArray<FKasumiSplatPoint>& OutPoints) const;
    bool LoadHigherOrderSH(TArray<float>& OutHigherOrderSH) const;
    bool LoadPointChunks(
        const TArray<int32>& ChunkIndices,
        TArray<FKasumiSplatPoint>& OutPoints,
        TArray<float>* OutHigherOrderSH = nullptr) const;

    using FChunkLoadCallback = TFunction<void(
        bool bSuccess,
        TArray<FKasumiSplatPoint>&& Points,
        TArray<float>&& HigherOrderSH,
        int32 HigherOrderSHCoefficientsPerPoint)>;

    /** Loads only the requested packed ranges. The callback is always dispatched on the game thread. */
    void LoadPointChunksAsync(const TArray<int32>& ChunkIndices, FChunkLoadCallback&& Callback) const;

private:
    mutable FCriticalSection BulkDataCriticalSection;
    FByteBulkData PointBulkData;
    FByteBulkData HigherOrderSHBulkData;
};
