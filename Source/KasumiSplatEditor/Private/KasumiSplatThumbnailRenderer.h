#pragma once

#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "KasumiSplatThumbnailRenderer.generated.h"

/** Draws the compact preview embedded in a KasumiSplat asset without accessing its BulkData. */
UCLASS()
class UKasumiSplatThumbnailRenderer : public UThumbnailRenderer
{
    GENERATED_BODY()

public:
    virtual bool CanVisualizeAsset(UObject* Object) override;
    virtual void Draw(
        UObject* Object,
        int32 X,
        int32 Y,
        uint32 Width,
        uint32 Height,
        FRenderTarget* RenderTarget,
        FCanvas* Canvas,
        bool bAdditionalViewFamily) override;
    virtual EThumbnailRenderFrequency GetThumbnailRenderFrequency(UObject* Object) const override;
};
