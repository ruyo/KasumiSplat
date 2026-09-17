#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatTypes.generated.h"

/** Source data is never modified by a frame effect. Units: centimeters; scale: one sigma. */
USTRUCT(BlueprintType)
struct KASUMISPLATRUNTIME_API FKasumiSplatPoint
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Splat")
    int32 StableId = 0;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Splat")
    FVector Position = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Splat")
    FQuat Rotation = FQuat::Identity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Splat")
    FVector Sigma = FVector(10.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Splat")
    FLinearColor Color = FLinearColor::White;
};

/** Describes the source conventions applied while decoding a PLY file. */
UENUM(BlueprintType)
enum class EKasumiSplatImportProfile : uint8
{
    AutoDetect UMETA(DisplayName="Auto Detect"),
    Standard3DGS UMETA(DisplayName="Standard 3D Gaussian Splatting"),
    LumaAI UMETA(DisplayName="Luma AI"),
    RGBPointCloud UMETA(DisplayName="RGB Point Cloud (sRGB)"),
    Custom UMETA(DisplayName="Custom")
};

/** Serializable importer choices used to reproduce a reimport. */
USTRUCT()
struct KASUMISPLATRUNTIME_API FKasumiSplatImportSettingsSnapshot
{
    GENERATED_BODY()

    UPROPERTY()
    EKasumiSplatImportProfile Profile = EKasumiSplatImportProfile::AutoDetect;

    UPROPERTY()
    double UnitsToCentimeters = 100.0;

    UPROPERTY()
    double DefaultSigmaCentimeters = 1.0;

    UPROPERTY()
    int64 MaxPointCount = 20000000;

    UPROPERTY()
    bool bSwapYZ = false;

    UPROPERTY()
    bool bFlipX = false;

    UPROPERTY()
    bool bFlipY = false;

    UPROPERTY()
    bool bFlipZ = false;

    UPROPERTY()
    bool bScaleValuesAreLogarithmic = true;

    UPROPERTY()
    bool bOpacityValuesAreLogits = true;

    UPROPERTY()
    bool bRgbValuesAreSRGB = false;

    UPROPERTY()
    bool bQuaternionWFirst = true;

    UPROPERTY()
    bool bSkipInvalidPoints = false;

    UPROPERTY()
    bool bSpatiallySortForStreaming = true;
};

UENUM(BlueprintType)
enum class EKasumiSplatQuality : uint8
{
    Low,
    Medium,
    High,
    Cinematic,
    Custom
};

UENUM(BlueprintType)
enum class EKasumiSplatAppearancePreset : uint8
{
    Reference,
    Vivid,
    Dense,
    Soft,
    Custom
};

/** Color and density controls applied after view-dependent SH evaluation. */
USTRUCT(BlueprintType)
struct KASUMISPLATRUNTIME_API FKasumiSplatAppearance
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Appearance", meta=(ClampMin="-8", ClampMax="8", UIMin="-4", UIMax="4"))
    float ExposureEV = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Appearance", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="2"))
    float Saturation = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Appearance", meta=(ClampMin="0", ClampMax="4", UIMin="0.5", UIMax="2"))
    float Contrast = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Appearance", meta=(ClampMin="0", ClampMax="4", UIMin="0", UIMax="2"))
    float SHStrength = 1.0f;

    /** Converts source alpha as 1 - (1 - alpha)^Density. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Appearance", meta=(ClampMin="0", ClampMax="8", UIMin="0", UIMax="4"))
    float OpacityDensity = 1.0f;
};

UENUM(BlueprintType)
enum class EKasumiSplatSortMode : uint8
{
    Auto,
    Bucket,
    GlobalRadix,
    Tiled
};

/** Selects whether a potentially overflowing Tiled sort prepares its Global Radix fallback immediately. */
UENUM(BlueprintType)
enum class EKasumiSplatTiledFallbackMode : uint8
{
    SameFrame UMETA(DisplayName="Same Frame (Stable)"),
    Delayed UMETA(DisplayName="Delayed (Lower GPU Cost)")
};

/** Controls the screen-space Gaussian low-pass filter and its opacity normalization. */
UENUM(BlueprintType)
enum class EKasumiSplatAntialiasingMode : uint8
{
    Disabled UMETA(DisplayName="Disabled"),
    LegacyFilter UMETA(DisplayName="Legacy Filter"),
    AreaCompensated UMETA(DisplayName="Area Compensated")
};

/** Controls whether Kasumi Splat contributes motion vectors for temporal rendering. */
UENUM(BlueprintType)
enum class EKasumiSplatVelocityMode : uint8
{
    Disabled UMETA(DisplayName="Disabled"),
    ActorAndCamera UMETA(DisplayName="Actor and Camera")
};

UENUM(BlueprintType)
enum class EKasumiSplatEffectType : uint8
{
    Displace,
    Tint,
    Opacity,
    Scale,
    Rotate
};

UENUM(BlueprintType)
enum class EKasumiSplatMaskShape : uint8
{
    None,
    Sphere,
    Box
};

UENUM(BlueprintType)
enum class EKasumiSplatExternalMask : uint8
{
    None,
    Texture2D,
    VolumeTexture
};

/** A serializable effect layer. Layers are evaluated in array order. */
USTRUCT(BlueprintType)
struct KASUMISPLATRUNTIME_API FKasumiSplatEffectLayer
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    bool bEnabled = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    EKasumiSplatEffectType Type = EKasumiSplatEffectType::Displace;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect", meta=(ClampMin="0", ClampMax="1"))
    float Weight = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    EKasumiSplatMaskShape MaskShape = EKasumiSplatMaskShape::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask")
    FVector MaskCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask", meta=(ClampMin="0", Units="cm"))
    FVector MaskExtent = FVector(100.0);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask", meta=(ClampMin="0", Units="cm"))
    float Feather = 25.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    FVector Vector = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    float Scalar = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    int32 Seed = 17;
};

USTRUCT(BlueprintType)
struct KASUMISPLATRUNTIME_API FKasumiSplatStyle
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Style")
    FLinearColor Tint = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Style", meta=(ClampMin="0", ClampMax="1"))
    float Opacity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Style", meta=(ClampMin="0", ClampMax="10"))
    float Scale = 1.0f;

    /** 0 = source position; 1 = fully dispersed. Stateless and reversible. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect", meta=(ClampMin="0", ClampMax="1"))
    float Progress = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect", meta=(ClampMin="0", ClampMax="1000", Units="cm"))
    float Displacement = 150.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect")
    int32 Seed = 17;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect|Color")
    FLinearColor EffectTint = FLinearColor::White;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect|Opacity", meta=(ClampMin="0", ClampMax="1"))
    float TargetOpacity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect|Scale", meta=(ClampMin="0", ClampMax="10"))
    float TargetScale = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Interp, Category="Effect|Rotation", meta=(Units="deg"))
    float RotationDegrees = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask")
    FVector MaskCenter = FVector::ZeroVector;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask", meta=(ClampMin="0", Units="cm"))
    float MaskRadius = 0.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Mask", meta=(ClampMin="0", Units="cm"))
    float MaskFeather = 50.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Effect|Noise", meta=(ClampMin="0", ClampMax="1"))
    float NoiseAmount = 0.0f;
};

USTRUCT(BlueprintType)
struct KASUMISPLATRUNTIME_API FKasumiSplatChunk
{
    GENERATED_BODY()

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Streaming")
    int32 FirstPoint = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Streaming")
    int32 PointCount = 0;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Streaming")
    FBox LocalBounds = FBox(ForceInit);
};
