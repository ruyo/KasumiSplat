#include "KasumiSplatConfiguration.h"

namespace KasumiSplatConfig
{
    FQualityValues GetQualityValues(EKasumiSplatQuality Preset)
    {
        switch (Preset)
        {
        case EKasumiSplatQuality::Low:
            return {100000, 0.75f, 16};
        case EKasumiSplatQuality::Medium:
            return {250000, 0.5f, 8};
        case EKasumiSplatQuality::High:
            return {750000, 0.25f, 4};
        case EKasumiSplatQuality::Cinematic:
            return {2000000, 0.0f, 2};
        case EKasumiSplatQuality::Custom:
        default:
            return {};
        }
    }

    FKasumiSplatAppearance GetAppearanceValues(EKasumiSplatAppearancePreset Preset)
    {
        FKasumiSplatAppearance Result;
        switch (Preset)
        {
        case EKasumiSplatAppearancePreset::Vivid:
            Result.ExposureEV = 0.25f;
            Result.Saturation = 1.2f;
            Result.Contrast = 1.08f;
            Result.SHStrength = 1.1f;
            Result.OpacityDensity = 1.2f;
            break;
        case EKasumiSplatAppearancePreset::Dense:
            Result.Saturation = 1.05f;
            Result.OpacityDensity = 1.6f;
            break;
        case EKasumiSplatAppearancePreset::Soft:
            Result.ExposureEV = -0.1f;
            Result.Saturation = 0.9f;
            Result.Contrast = 0.9f;
            Result.SHStrength = 0.85f;
            Result.OpacityDensity = 0.75f;
            break;
        case EKasumiSplatAppearancePreset::Reference:
        case EKasumiSplatAppearancePreset::Custom:
        default:
            break;
        }
        return Result;
    }

    void SanitizeRenderPacket(FKasumiSplatPacket& Packet)
    {
        if (Packet.EffectLayers.Num() > int32(MaxEffectLayers))
        {
            Packet.EffectLayers.SetNum(int32(MaxEffectLayers));
        }
        Packet.TileSizePixels = FMath::Clamp(
            FMath::RoundToInt(float(Packet.TileSizePixels) / float(TileSizeAlignment)) * int32(TileSizeAlignment),
            int32(MinTileSizePixels),
            int32(MaxTileSizePixels));
        Packet.MaxTilesPerSplat = FMath::Clamp(Packet.MaxTilesPerSplat, 1u, MaxTilesPerSplat);
        Packet.TiledPairBudgetMB = FMath::Clamp(Packet.TiledPairBudgetMB, 16u, 2048u);
        Packet.MaxVisibleSplats = FMath::Max(Packet.MaxVisibleSplats, 1u);
        Packet.MinProjectedRadiusPixels = FMath::Max(Packet.MinProjectedRadiusPixels, 0.0f);
        Packet.MaxProjectedRadiusPixels = FMath::Max(Packet.MaxProjectedRadiusPixels, 1.0f);
        Packet.AntialiasingFilterVariance = FMath::Clamp(Packet.AntialiasingFilterVariance, 0.0f, 4.0f);
        Packet.MaxAnisotropy = FMath::Max(Packet.MaxAnisotropy, 0.0f);
        Packet.MaxSplatSigmaCentimeters = FMath::Max(Packet.MaxSplatSigmaCentimeters, 0.0f);
        Packet.SceneDepthBias = FMath::Clamp(Packet.SceneDepthBias, 0.0f, 0.01f);
        Packet.DepthPreCullMaxRadiusPixels = FMath::Max(Packet.DepthPreCullMaxRadiusPixels, 0.0f);
        Packet.ExternalMaskExtent = Packet.ExternalMaskExtent.GetAbs().ComponentMax(FVector(0.001));
        Packet.ExternalMaskStrength = FMath::Clamp(Packet.ExternalMaskStrength, 0.0f, 1.0f);
        Packet.Appearance.ExposureEV = FMath::Clamp(Packet.Appearance.ExposureEV, -8.0f, 8.0f);
        Packet.Appearance.Saturation = FMath::Clamp(Packet.Appearance.Saturation, 0.0f, 4.0f);
        Packet.Appearance.Contrast = FMath::Clamp(Packet.Appearance.Contrast, 0.0f, 4.0f);
        Packet.Appearance.SHStrength = FMath::Clamp(Packet.Appearance.SHStrength, 0.0f, 4.0f);
        Packet.Appearance.OpacityDensity = FMath::Clamp(Packet.Appearance.OpacityDensity, 0.0f, 8.0f);
    }
}
