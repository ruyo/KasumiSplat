#pragma once

#include "KasumiSplatConfiguration.h"

inline constexpr uint32 KasumiDepthBucketCount = KasumiSplatConfig::DepthBucketCount;
inline constexpr uint32 KasumiCullGroupSize = KasumiSplatConfig::CullGroupSize;
inline constexpr uint32 KasumiAutoTiledThreshold = KasumiSplatConfig::AutoTiledThreshold;

uint32 GetKasumiMaximumTileOverlap(uint32 TileSizePixels, float MaxProjectedRadiusPixels);

EKasumiSplatSortMode ResolveKasumiSplatSortMode(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount);

EKasumiSplatSortMode ResolveKasumiSplatSortModeForView(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount,
    uint32 TileCount,
    uint32 TileSizePixels,
    uint32 MaxTilesPerSplat,
    float MaxProjectedRadiusPixels,
    uint32 TiledPairBudgetMB);

EKasumiSplatSortMode ResolveKasumiSplatRequestedSortMode(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount);
