#include "KasumiSplatRenderResources.h"

#include "KasumiSplatConfiguration.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

FKasumiSplatGPUData GetOrCreateSplatGPUData(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatRenderEntry& Entry)
{
    FKasumiSplatGPUData Result;
    const TArray<FKasumiSplatPoint>& Points = *Entry.Packet.Points;
    Result.ClusterCount = FMath::DivideAndRoundUp(uint32(Points.Num()), KasumiSplatConfig::ClusterPointCount);
    if (Entry.PointBuffer.IsValid())
    {
        Result.PointBuffer = GraphBuilder.RegisterExternalBuffer(Entry.PointBuffer, TEXT("KasumiSplat.Points"));
    }
    else
    {
        const bool bHasTransitionClasses = Entry.Packet.TransitionClasses.IsValid() &&
            Entry.Packet.TransitionClasses->Num() == Points.Num();
        TArray<FVector4f> PackedPoints;
        PackedPoints.SetNumUninitialized(Points.Num() * 4);
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const FKasumiSplatPoint& Point = Points[Index];
            const uint32 StableId = uint32(Point.StableId);
            float StableIdBits = 0.0f;
            FMemory::Memcpy(&StableIdBits, &StableId, sizeof(uint32));
            PackedPoints[Index * 4 + 0] = FVector4f(FVector3f(Point.Position), StableIdBits);
            PackedPoints[Index * 4 + 1] = FVector4f(
                float(Point.Rotation.X),
                float(Point.Rotation.Y),
                float(Point.Rotation.Z),
                float(Point.Rotation.W));
            PackedPoints[Index * 4 + 2] = FVector4f(FVector3f(Point.Sigma), Point.Color.A);
            const float TransitionClass = bHasTransitionClasses
                ? float((*Entry.Packet.TransitionClasses)[Index])
                : 0.0f;
            PackedPoints[Index * 4 + 3] = FVector4f(Point.Color.R, Point.Color.G, Point.Color.B, TransitionClass);
        }

        Result.PointBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.PointUpload"), PackedPoints);
        GraphBuilder.QueueBufferExtraction(Result.PointBuffer, &Entry.PointBuffer);
    }

    if (Entry.ClusterBoundsBuffer.IsValid())
    {
        Result.ClusterBoundsBuffer = GraphBuilder.RegisterExternalBuffer(
            Entry.ClusterBoundsBuffer,
            TEXT("KasumiSplat.ClusterBounds"));
    }
    else
    {
        TArray<FVector4f> ClusterBounds;
        ClusterBounds.SetNumUninitialized(FMath::Max(Result.ClusterCount, 1u));
        for (uint32 ClusterIndex = 0; ClusterIndex < Result.ClusterCount; ++ClusterIndex)
        {
            const int32 FirstPoint = int32(ClusterIndex * KasumiSplatConfig::ClusterPointCount);
            const int32 EndPoint = FMath::Min(
                FirstPoint + int32(KasumiSplatConfig::ClusterPointCount),
                Points.Num());
            FBox PositionBounds(ForceInit);
            for (int32 PointIndex = FirstPoint; PointIndex < EndPoint; ++PointIndex)
            {
                PositionBounds += Points[PointIndex].Position;
            }
            const FVector Center = PositionBounds.GetCenter();
            double Radius = 0.0;
            for (int32 PointIndex = FirstPoint; PointIndex < EndPoint; ++PointIndex)
            {
                const FKasumiSplatPoint& Point = Points[PointIndex];
                const double SplatRadius = 3.0 * Point.Sigma.GetAbs().GetMax();
                Radius = FMath::Max(Radius, FVector::Distance(Center, Point.Position) + SplatRadius);
            }
            ClusterBounds[ClusterIndex] = FVector4f(FVector3f(Center), float(Radius));
        }
        if (Result.ClusterCount == 0)
        {
            ClusterBounds[0] = FVector4f::Zero();
        }
        Result.ClusterBoundsBuffer = CreateStructuredBuffer(
            GraphBuilder,
            TEXT("KasumiSplat.ClusterBoundsUpload"),
            ClusterBounds);
        GraphBuilder.QueueBufferExtraction(Result.ClusterBoundsBuffer, &Entry.ClusterBoundsBuffer);
    }

    if (Entry.SHBuffer.IsValid())
    {
        Result.SHBuffer = GraphBuilder.RegisterExternalBuffer(Entry.SHBuffer, TEXT("KasumiSplat.HigherOrderSH"));
    }
    else
    {
        const int32 SHCount = int32(Entry.Packet.HigherOrderSHCoefficientsPerPoint);
        const int32 SHWordsPerPoint = FMath::DivideAndRoundUp(SHCount, 2);
        if (SHCount > 0 && Entry.Packet.HigherOrderSH.IsValid() &&
            Entry.Packet.HigherOrderSH->Num() == Points.Num() * SHWordsPerPoint)
        {
            Result.SHBuffer = CreateStructuredBuffer(
                GraphBuilder,
                TEXT("KasumiSplat.SHUpload"),
                *Entry.Packet.HigherOrderSH);
        }
        else
        {
            const TArray<uint32> DummySH{0u};
            Result.SHBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.SHUpload"), DummySH);
        }
        GraphBuilder.QueueBufferExtraction(Result.SHBuffer, &Entry.SHBuffer);
    }
    return Result;
}
