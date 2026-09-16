#include "KasumiSplatThumbnailRenderer.h"

#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "KasumiSplatAsset.h"

namespace
{
    void AddSoftPoint(
        TArray<FCanvasUVTri>& Triangles,
        const FVector2D& Center,
        float Radius,
        const FLinearColor& CenterColor)
    {
        constexpr int32 SegmentCount = 8;
        const FLinearColor EdgeColor(CenterColor.R, CenterColor.G, CenterColor.B, 0.0f);
        for (int32 Segment = 0; Segment < SegmentCount; ++Segment)
        {
            const float Angle0 = UE_TWO_PI * float(Segment) / float(SegmentCount);
            const float Angle1 = UE_TWO_PI * float(Segment + 1) / float(SegmentCount);
            FCanvasUVTri& Triangle = Triangles.AddDefaulted_GetRef();
            Triangle.V0_Pos = Center;
            Triangle.V0_UV = FVector2D(0.5, 0.5);
            Triangle.V0_Color = CenterColor;
            Triangle.V1_Pos = Center + FVector2D(FMath::Cos(Angle0), FMath::Sin(Angle0)) * Radius;
            Triangle.V1_UV = FVector2D::ZeroVector;
            Triangle.V1_Color = EdgeColor;
            Triangle.V2_Pos = Center + FVector2D(FMath::Cos(Angle1), FMath::Sin(Angle1)) * Radius;
            Triangle.V2_UV = FVector2D::ZeroVector;
            Triangle.V2_Color = EdgeColor;
        }
    }
}

bool UKasumiSplatThumbnailRenderer::CanVisualizeAsset(UObject* Object)
{
    const UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(Object);
#if WITH_EDITORONLY_DATA
    return Asset && !Asset->ThumbnailPoints.IsEmpty();
#else
    return false;
#endif
}

void UKasumiSplatThumbnailRenderer::Draw(
    UObject* Object,
    int32 X,
    int32 Y,
    uint32 Width,
    uint32 Height,
    FRenderTarget* RenderTarget,
    FCanvas* Canvas,
    bool bAdditionalViewFamily)
{
    const UKasumiSplatAsset* Asset = Cast<UKasumiSplatAsset>(Object);
    if (!Asset || !Canvas || Width == 0 || Height == 0)
    {
        return;
    }

    FCanvasTileItem Background(
        FVector2D(X, Y),
        FVector2D(Width, Height),
        FLinearColor(0.012f, 0.018f, 0.032f, 1.0f));
    Background.BlendMode = SE_BLEND_Opaque;
    Canvas->DrawItem(Background);

#if WITH_EDITORONLY_DATA
    const float ShortEdge = float(FMath::Min(Width, Height));
    TArray<FCanvasUVTri> Triangles;
    Triangles.Reserve(Asset->ThumbnailPoints.Num() * 8);
    for (const FKasumiSplatThumbnailPoint& Point : Asset->ThumbnailPoints)
    {
        const FVector2D Center(
            double(X) + double(Point.Position.X) * double(Width),
            double(Y) + double(Point.Position.Y) * double(Height));
        const float Radius = FMath::Clamp(Point.Radius * ShortEdge, 1.0f, ShortEdge * 0.12f);
        AddSoftPoint(Triangles, Center, Radius, FLinearColor(Point.Color));
    }
    if (!Triangles.IsEmpty())
    {
        FCanvasTriangleItem Points(Triangles, nullptr);
        Points.BlendMode = SE_BLEND_Translucent;
        //Canvas->DrawItem(Points);
    }
#endif
}

EThumbnailRenderFrequency UKasumiSplatThumbnailRenderer::GetThumbnailRenderFrequency(UObject* Object) const
{
    return EThumbnailRenderFrequency::OnPropertyChange;
}
