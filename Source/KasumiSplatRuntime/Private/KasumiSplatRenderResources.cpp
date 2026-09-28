#include "KasumiSplatRenderResources.h"

#include "Math/Float16.h"
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
    }
    else
    {
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
    }

    if (Entry.SHBuffer.IsValid())
    {
        Result.SHBuffer = GraphBuilder.RegisterExternalBuffer(Entry.SHBuffer, TEXT("KasumiSplat.HigherOrderSH"));
    }
    else
    {
        const TArray<FKasumiSplatPoint>& Points = *Entry.Packet.Points;
        TArray<uint32> PackedSH;
        const int32 SHCount = int32(Entry.Packet.HigherOrderSHCoefficientsPerPoint);
        const int32 SHWordsPerPoint = FMath::DivideAndRoundUp(SHCount, 2);
        if (SHCount > 0 && Entry.Packet.HigherOrderSH.IsValid() &&
            Entry.Packet.HigherOrderSH->Num() == Points.Num() * SHCount)
        {
            PackedSH.SetNumZeroed(Points.Num() * SHWordsPerPoint);
            for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
            {
                const float* Source = Entry.Packet.HigherOrderSH->GetData() + int64(PointIndex) * SHCount;
                uint32* Target = PackedSH.GetData() + int64(PointIndex) * SHWordsPerPoint;
                for (int32 CoefficientIndex = 0; CoefficientIndex < SHCount; CoefficientIndex += 2)
                {
                    const uint32 Low = FFloat16(Source[CoefficientIndex]).Encoded;
                    const uint32 High = CoefficientIndex + 1 < SHCount
                        ? uint32(FFloat16(Source[CoefficientIndex + 1]).Encoded)
                        : 0u;
                    Target[CoefficientIndex / 2] = Low | (High << 16u);
                }
            }
        }
        else
        {
            PackedSH.Add(0u);
        }

        Result.SHBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.SHUpload"), PackedSH);
        GraphBuilder.QueueBufferExtraction(Result.SHBuffer, &Entry.SHBuffer);
    }
    return Result;
}
