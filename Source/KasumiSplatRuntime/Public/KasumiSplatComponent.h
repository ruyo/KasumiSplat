#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "KasumiSplatTypes.h"
#include "KasumiSplatComponent.generated.h"

class UKasumiSplatAsset;
class UCurveFloat;
class UMaterialParameterCollection;
class UTexture2D;
class UVolumeTexture;
struct FKasumiSplatStreamingState;

/** Scene component that publishes immutable source splats to the GPU renderer. */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class KASUMISPLATRUNTIME_API UKasumiSplatComponent : public USceneComponent
{
    GENERATED_BODY()
public:
    UKasumiSplatComponent();
    virtual ~UKasumiSplatComponent() override;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat")
    FKasumiSplatStyle Style;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="KasumiSplat|Appearance")
    EKasumiSplatAppearancePreset AppearancePreset = EKasumiSplatAppearancePreset::Reference;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Appearance")
    FKasumiSplatAppearance Appearance;

    /** Reusable ordered layers reserved for material, Niagara and Sequencer-driven looks. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect")
    TArray<FKasumiSplatEffectLayer> EffectLayers;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="KasumiSplat|Performance")
    EKasumiSplatQuality QualityPreset = EKasumiSplatQuality::High;

    /** Selects the GPU visibility and transparency ordering path. Auto currently chooses global radix sorting up to the configured point budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering")
    EKasumiSplatSortMode SortMode = EKasumiSplatSortMode::Auto;

    /** Screen-tile edge length used by Tiled sorting. Values are rounded to a multiple of 8. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering|Tiled", meta=(ClampMin="16", ClampMax="128", UIMin="16", UIMax="128"))
    int32 TileSizePixels = 32;

    /** Maximum number of tiles emitted by one splat in Tiled mode. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering|Tiled", meta=(ClampMin="1", ClampMax="256", UIMin="1", UIMax="256"))
    int32 MaxTilesPerSplat = 64;

    /** Maximum memory used by variable-length tiled key/value pairs. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering|Tiled", meta=(ClampMin="16", ClampMax="2048", Units="MB"))
    int32 TiledPairBudgetMB = 256;

    /** Optional deterministic remapping of normalized Progress for Sequencer and Blueprint scrubbing. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect")
    TObjectPtr<UCurveFloat> ProgressCurve;

    /** Optional live control source shared with materials, Blueprint and Sequencer. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|MPC")
    TObjectPtr<UMaterialParameterCollection> MaterialParameterCollection;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|MPC")
    FName MPCProgressParameter = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|MPC")
    FName MPCTintParameter = NAME_None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask")
    EKasumiSplatExternalMask ExternalMaskMode = EKasumiSplatExternalMask::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask")
    TObjectPtr<UTexture2D> EffectMaskTexture;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask")
    TObjectPtr<UVolumeTexture> EffectMaskVolume;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask")
    FVector ExternalMaskCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask", meta=(ClampMin="0.001", Units="cm"))
    FVector ExternalMaskExtent = FVector(100.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask", meta=(ClampMin="0", ClampMax="1"))
    float ExternalMaskStrength = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Effect|TextureMask")
    bool bInvertExternalMask = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat")
    TObjectPtr<UKasumiSplatAsset> Asset;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat")
    bool bUseSyntheticFallback = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat")
    bool bRenderSplats = true;

    /** Maximum source points considered by the GPU visibility pass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Performance", meta=(ClampMin="1", UIMin="1"))
    int32 MaxVisibleSplats = 750000;

    /** Loads and renders every source point for fixed-camera quality comparisons. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Quality")
    bool bFullQualityReference = false;

    /** Stream only the nearest asset chunks into the CPU/GPU resident working set. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming")
    bool bEnableChunkStreaming = true;

    /** Hard upper bound for points retained by this component after LOD. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="1", UIMin="1000"))
    int32 MaxResidentSplats = 1000000;

    /** Approximate combined CPU and GPU working-set budget. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="16", UIMin="16", UIMax="4096", Units="MB"))
    int32 StreamingMemoryBudgetMB = 256;

    /** Chunks beyond this distance are unloaded. Zero keeps all distances eligible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="0", Units="cm"))
    float MaxStreamingDistance = 0.0f;

    /** Distance at which deterministic source-order LOD decimation begins. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="1", Units="cm"))
    float LODStartDistance = 2500.0f;

    /** Largest source-order LOD stride. Use powers of two for stable transitions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="1", ClampMax="64"))
    int32 MaxLODStride = 4;

    /** Minimum time between camera-driven streaming decisions. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Streaming", meta=(ClampMin="0.05", ClampMax="5.0", Units="s"))
    float StreamingUpdateInterval = 0.25f;

    /** Splats smaller than this projected radius are removed by the GPU visibility pass. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Performance", meta=(ClampMin="0.0", UIMin="0.0", UIMax="4.0"))
    float MinProjectedRadiusPixels = 0.25f;

    /** Prevents near-camera splats from expanding to an unbounded screen size. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Performance", meta=(ClampMin="1.0", UIMin="32.0", UIMax="2048.0"))
    float MaxProjectedRadiusPixels = 1024.0f;

    /** Screen-space variance added to each Gaussian to reduce sub-pixel flicker. Zero disables filtering. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering", meta=(ClampMin="0.0", ClampMax="4.0", UIMin="0.0", UIMax="1.0"))
    float AntialiasingFilterVariance = 0.3f;

    /** Selects legacy filtering, area-preserving filtering, or an unfiltered reference. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering")
    EKasumiSplatAntialiasingMode AntialiasingMode = EKasumiSplatAntialiasingMode::AreaCompensated;

    /** Cross-fades resident-set changes to suppress visible streaming and LOD pops. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering|Temporal")
    bool bTemporalStabilization = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering|Temporal", meta=(ClampMin="0.0", ClampMax="2.0", Units="s"))
    float TemporalTransitionDuration = 0.15f;

    /** Limits needle-like source splats. The longest principal axis may be this many times the middle axis. Zero disables the limit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering", meta=(ClampMin="0.0", UIMin="0.0", UIMax="256.0"))
    float MaxAnisotropy = 32.0f;

    /** Limits an individual principal-axis sigma before component scaling. Zero disables the limit. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering", meta=(ClampMin="0.0", UIMin="0.0", UIMax="1000.0", Units="cm"))
    float MaxSplatSigmaCentimeters = 100.0f;

    /** Small device-depth tolerance used when splats meet opaque geometry. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering", meta=(ClampMin="0.0", ClampMax="0.01", UIMin="0.0", UIMax="0.001"))
    float SceneDepthBias = 0.00001f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering")
    bool bUseSceneDepth = true;

    /** Center-depth pre-cull is limited to small splats to avoid removing large, partially visible ellipses. Zero disables it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Rendering", meta=(ClampMin="0.0", ClampMax="64.0"))
    float DepthPreCullMaxRadiusPixels = 8.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Debug")
    bool bDrawDebugBounds = false;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Debug")
    bool bDrawDebugChunks = false;

    UFUNCTION(BlueprintCallable, CallInEditor, Category="KasumiSplat")
    void ResetSyntheticPoints();

    UFUNCTION(BlueprintCallable, Category="KasumiSplat")
    void ReloadPoints();

    UFUNCTION(BlueprintCallable, Category="KasumiSplat")
    void SetProgress(float Progress);

    UFUNCTION(BlueprintCallable, Category="KasumiSplat")
    void SetEffectTint(FLinearColor Tint);

    UFUNCTION(BlueprintCallable, Category="KasumiSplat")
    void SetEffectMask(FVector Center, float Radius, float Feather);

    UFUNCTION(BlueprintCallable, Category="KasumiSplat|Performance")
    void ApplyQualityPreset(EKasumiSplatQuality Preset);

    UFUNCTION(BlueprintCallable, Category="KasumiSplat|Appearance")
    void ApplyAppearancePreset(EKasumiSplatAppearancePreset Preset);

    UFUNCTION(BlueprintPure, Category="KasumiSplat")
    int32 GetLoadedPointCount() const;

    UFUNCTION(BlueprintPure, Category="KasumiSplat|Streaming")
    int32 GetLoadedHigherOrderSHValueCount() const;

    UFUNCTION(BlueprintPure, Category="KasumiSplat|Streaming")
    int64 GetApproximateResidentBytes() const;

    UFUNCTION(BlueprintPure, Category="KasumiSplat|Streaming")
    int32 GetResidentRevision() const;

    UFUNCTION(BlueprintPure, Category="KasumiSplat|Rendering")
    bool IsRenderingSupported() const;

    /** Copies one immutable resident snapshot for integrations such as Niagara. */
    TSharedPtr<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe> GetResidentPointSnapshot() const;

protected:
    virtual void OnRegister() override;
    virtual void OnUnregister() override;
    virtual void OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
    virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
    FKasumiSplatStreamingState* StreamingState = nullptr;
    void RebuildSharedSourcePoints();
    void UpdateStreamingWorkingSet(bool bForce);
    FVector GetStreamingViewLocation() const;
    void Publish();
};
