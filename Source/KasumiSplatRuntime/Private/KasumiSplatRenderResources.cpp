#include "KasumiSplatRenderResources.h"

#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

FKasumiSplatGPUData GetOrCreateSplatGPUData(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatRenderEntry& Entry)
{
    FKasumiSplatGPUData Result;
    if (Entry.PointBuffer.IsValid())
    {
        Result.PointBuffer = GraphBuilder.RegisterExternalBuffer(Entry.PointBuffer, TEXT("KasumiSplat.Points"));
        Result.SHBuffer = GraphBuilder.RegisterExternalBuffer(Entry.SHBuffer, TEXT("KasumiSplat.HigherOrderSH"));
        return Result;
    }

    const TArray<FKasumiSplatPoint>& Points = *Entry.Packet.Points;
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

    TArray<FVector4f> PackedSH;
    const uint32 SHCount = Entry.Packet.HigherOrderSHCoefficientsPerPoint;
    if (SHCount > 0 && Entry.Packet.HigherOrderSH.IsValid() &&
        Entry.Packet.HigherOrderSH->Num() == Points.Num() * int32(SHCount))
    {
        constexpr int32 SHFloat4sPerPoint = 12;
        PackedSH.SetNumZeroed(Points.Num() * SHFloat4sPerPoint);
        for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
        {
            FMemory::Memcpy(
                reinterpret_cast<float*>(PackedSH.GetData() + PointIndex * SHFloat4sPerPoint),
                Entry.Packet.HigherOrderSH->GetData() + PointIndex * SHCount,
                sizeof(float) * FMath::Min<uint32>(SHCount, 45));
        }
    }
    else
    {
        PackedSH.Add(FVector4f::Zero());
    }

    Result.SHBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.SHUpload"), PackedSH);
    GraphBuilder.QueueBufferExtraction(Result.SHBuffer, &Entry.SHBuffer);
    return Result;
}
