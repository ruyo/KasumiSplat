#pragma once

#include "KasumiSplatRenderer.h"

namespace KasumiSplatConfig
{
    inline constexpr uint32 MaxEffectLayers = 4;
    inline constexpr uint32 DepthBucketCount = 4096;
    inline constexpr uint32 CullGroupSize = 64;
    inline constexpr uint32 TileCapacity = 256;
    inline constexpr uint32 AutoTiledThreshold = 750000;
    inline constexpr uint32 MinTileSizePixels = 16;
    inline constexpr uint32 MaxTileSizePixels = 128;
    inline constexpr uint32 TileSizeAlignment = 8;
    inline constexpr uint32 MaxTilesPerSplat = 256;

    struct FQualityValues
    {
        int32 MaxVisibleSplats = 750000;
        float MinProjectedRadiusPixels = 0.25f;
        int32 MaxLODStride = 4;
    };

    FQualityValues GetQualityValues(EKasumiSplatQuality Preset);
    FKasumiSplatAppearance GetAppearanceValues(EKasumiSplatAppearancePreset Preset);
    void SanitizeRenderPacket(FKasumiSplatPacket& Packet);
}
