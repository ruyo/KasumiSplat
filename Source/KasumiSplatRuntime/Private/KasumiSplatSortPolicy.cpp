#include "KasumiSplatSortPolicy.h"

#include "HAL/IConsoleManager.h"

namespace
{
    TAutoConsoleVariable<int32> CVarKasumiSplatSortMode(
        TEXT("r.KasumiSplat.SortMode"),
        -1,
        TEXT("Override component sort mode. -1: component, 0: bucket, 1: global radix, 2: tiled."),
        ECVF_RenderThreadSafe);
}

EKasumiSplatSortMode ResolveKasumiSplatRequestedSortMode(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount)
{
    return RequestedMode == EKasumiSplatSortMode::Auto
        ? (PointCount > KasumiAutoTiledThreshold
            ? EKasumiSplatSortMode::Tiled
            : EKasumiSplatSortMode::GlobalRadix)
        : RequestedMode;
}

EKasumiSplatSortMode ResolveKasumiSplatSortMode(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount)
{
    EKasumiSplatSortMode ResolvedMode = ResolveKasumiSplatRequestedSortMode(RequestedMode, PointCount);

    const int32 Override = CVarKasumiSplatSortMode.GetValueOnAnyThread();
    if (Override >= 0 && Override <= 2)
    {
        ResolvedMode = Override == 0
            ? EKasumiSplatSortMode::Bucket
            : (Override == 1 ? EKasumiSplatSortMode::GlobalRadix : EKasumiSplatSortMode::Tiled);
    }
    return ResolvedMode;
}

EKasumiSplatSortMode ResolveKasumiSplatSortModeForView(
    EKasumiSplatSortMode RequestedMode,
    uint32 PointCount,
    uint32 TileCount,
    uint32 TileSizePixels,
    uint32 MaxTilesPerSplat,
    float MaxProjectedRadiusPixels,
    uint32 TiledPairBudgetMB)
{
    const EKasumiSplatSortMode ResolvedMode = ResolveKasumiSplatSortMode(RequestedMode, PointCount);
    if (ResolvedMode == EKasumiSplatSortMode::Tiled)
    {
        // Pair capacity is budgeted at runtime. A GPU overflow flag selects the already-sorted
        // Global Radix fallback in the same frame, so configured maximum radius is not a reason
        // to silently resolve every Tiled request to Global Radix.
        if (TileCount == 0u || TileCount > 65536u || TiledPairBudgetMB < 16u || MaxTilesPerSplat == 0u)
        {
            return EKasumiSplatSortMode::GlobalRadix;
        }
    }
    return ResolvedMode;
}

uint32 GetKasumiMaximumTileOverlap(uint32 TileSizePixels, float MaxProjectedRadiusPixels)
{
    const uint32 SafeTileSize = FMath::Max(TileSizePixels, 1u);
    const uint32 DiameterInTiles = FMath::CeilToInt(
        2.0f * FMath::Max(MaxProjectedRadiusPixels, 0.0f) / float(SafeTileSize)) + 1u;
    return FMath::Max(1u, DiameterInTiles * DiameterInTiles);
}
