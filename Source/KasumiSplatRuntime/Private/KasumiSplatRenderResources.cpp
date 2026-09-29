#include "KasumiSplatRenderResources.h"

#include "KasumiSplatConfiguration.h"
#include "Math/Float16.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"

namespace
{
    TMap<uint64, TWeakPtr<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe>> SharedResourceCache;

    bool AreResourcesReady(const FKasumiSplatSharedGPUResources& Resources)
    {
        return Resources.PointBuffer.IsValid() && Resources.SHBuffer.IsValid() &&
            Resources.ClusterBoundsBuffer.IsValid();
    }

    TSharedPtr<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe> ResolveResourceSet(
        FKasumiSplatRenderEntry& Entry)
    {
        const uint64 RequestedKey = Entry.Packet.GPUResourceKey;
        if (Entry.GPUResources.IsValid() && Entry.GPUResourceKey == RequestedKey)
        {
            return Entry.GPUResources;
        }

        Entry.GPUResources.Reset();
        Entry.GPUResourceKey = RequestedKey;
        if (RequestedKey != 0)
        {
            if (TWeakPtr<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe>* ExistingWeak =
                    SharedResourceCache.Find(RequestedKey))
            {
                if (TSharedPtr<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe> Existing = ExistingWeak->Pin())
                {
                    if (AreResourcesReady(*Existing))
                    {
                        Entry.GPUResources = Existing;
                        return Existing;
                    }
                    // The first owner is still uploading in this graph. Use a private set for
                    // this frame, then adopt the shared pooled buffers on the next publish.
                    Entry.GPUResourceKey = 0;
                    Entry.GPUResources = MakeShared<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe>();
                    return Entry.GPUResources;
                }
                SharedResourceCache.Remove(RequestedKey);
            }
        }

        Entry.GPUResources = MakeShared<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe>();
        if (RequestedKey != 0)
        {
            SharedResourceCache.Add(RequestedKey, Entry.GPUResources);
        }
        return Entry.GPUResources;
    }

    uint32 PackHalf2(float X, float Y)
    {
        return uint32(FFloat16(X).Encoded) | (uint32(FFloat16(Y).Encoded) << 16u);
    }

    uint32 PackColorAndTransition(const FLinearColor& Color, uint8 TransitionClass)
    {
        const uint32 R = uint32(FMath::RoundToInt(FMath::Clamp(Color.R, 0.0f, 1.0f) * 1023.0f));
        const uint32 G = uint32(FMath::RoundToInt(FMath::Clamp(Color.G, 0.0f, 1.0f) * 1023.0f));
        const uint32 B = uint32(FMath::RoundToInt(FMath::Clamp(Color.B, 0.0f, 1.0f) * 1023.0f));
        return R | (G << 10u) | (B << 20u) | (uint32(FMath::Min<uint8>(TransitionClass, 3u)) << 30u);
    }

    float ReadPackedHalf(const TArray<uint32>& Values, int64 PointBase, int32 CoefficientIndex)
    {
        const uint32 Packed = Values[PointBase + CoefficientIndex / 2];
        FFloat16 Value;
        Value.Encoded = uint16((Packed >> ((CoefficientIndex & 1) * 16)) & 0xffffu);
        return Value.GetFloat();
    }
}

FKasumiSplatGPUData GetOrCreateSplatGPUData(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatRenderEntry& Entry)
{
    FKasumiSplatGPUData Result;
    TSharedPtr<FKasumiSplatSharedGPUResources, ESPMode::ThreadSafe> ResourceSet = ResolveResourceSet(Entry);
    FKasumiSplatSharedGPUResources& Resources = *ResourceSet;
    const TArray<FKasumiSplatPoint>& Points = *Entry.Packet.Points;
    Result.ClusterCount = FMath::DivideAndRoundUp(uint32(Points.Num()), KasumiSplatConfig::ClusterPointCount);
    if (Resources.PointBuffer.IsValid())
    {
        Result.PointBuffer = GraphBuilder.RegisterExternalBuffer(Resources.PointBuffer, TEXT("KasumiSplat.Points"));
    }
    else
    {
        const bool bHasTransitionClasses = Entry.Packet.TransitionClasses.IsValid() &&
            Entry.Packet.TransitionClasses->Num() == Points.Num();
        TArray<uint32> PackedPoints;
        PackedPoints.SetNumUninitialized(Points.Num() * KasumiSplatConfig::GPUPointWordCount);
        for (int32 Index = 0; Index < Points.Num(); ++Index)
        {
            const FKasumiSplatPoint& Point = Points[Index];
            const int64 Base = int64(Index) * KasumiSplatConfig::GPUPointWordCount;
            const FVector3f Position(Point.Position);
            FMemory::Memcpy(&PackedPoints[Base + 0], &Position.X, sizeof(uint32));
            FMemory::Memcpy(&PackedPoints[Base + 1], &Position.Y, sizeof(uint32));
            FMemory::Memcpy(&PackedPoints[Base + 2], &Position.Z, sizeof(uint32));
            PackedPoints[Base + 3] = uint32(Point.StableId);
            PackedPoints[Base + 4] = PackHalf2(float(Point.Rotation.X), float(Point.Rotation.Y));
            PackedPoints[Base + 5] = PackHalf2(float(Point.Rotation.Z), float(Point.Rotation.W));
            PackedPoints[Base + 6] = PackHalf2(float(Point.Sigma.X), float(Point.Sigma.Y));
            PackedPoints[Base + 7] = PackHalf2(float(Point.Sigma.Z), Point.Color.A);
            PackedPoints[Base + 8] = PackColorAndTransition(
                Point.Color,
                bHasTransitionClasses ? (*Entry.Packet.TransitionClasses)[Index] : 0u);
        }

        Result.PointBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.PointUpload"), PackedPoints);
        GraphBuilder.QueueBufferExtraction(Result.PointBuffer, &Resources.PointBuffer);
    }

    if (Resources.ClusterBoundsBuffer.IsValid())
    {
        Result.ClusterBoundsBuffer = GraphBuilder.RegisterExternalBuffer(
            Resources.ClusterBoundsBuffer,
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
        GraphBuilder.QueueBufferExtraction(Result.ClusterBoundsBuffer, &Resources.ClusterBoundsBuffer);
    }

    if (Resources.SHBuffer.IsValid())
    {
        Result.SHBuffer = GraphBuilder.RegisterExternalBuffer(Resources.SHBuffer, TEXT("KasumiSplat.HigherOrderSH"));
        Result.SHWordsPerPoint = Resources.SHWordsPerPoint;
    }
    else
    {
        const int32 SHCount = int32(Entry.Packet.HigherOrderSHCoefficientsPerPoint);
        const int32 SourceSHWordsPerPoint = FMath::DivideAndRoundUp(SHCount, 2);
        Result.SHWordsPerPoint = KasumiSplatConfig::GetGPUHigherOrderSHWordsPerPoint(SHCount);
        if (SHCount > 0 && Entry.Packet.HigherOrderSH.IsValid() &&
            Entry.Packet.HigherOrderSH->Num() == Points.Num() * SourceSHWordsPerPoint)
        {
            TArray<uint32> QuantizedSH;
            QuantizedSH.SetNumZeroed(int64(Points.Num()) * Result.SHWordsPerPoint);
            for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
            {
                const int64 SourceBase = int64(PointIndex) * SourceSHWordsPerPoint;
                const int64 TargetBase = int64(PointIndex) * Result.SHWordsPerPoint;
                float MaximumMagnitude = 0.0f;
                for (int32 CoefficientIndex = 0; CoefficientIndex < SHCount; ++CoefficientIndex)
                {
                    MaximumMagnitude = FMath::Max(
                        MaximumMagnitude,
                        FMath::Abs(ReadPackedHalf(*Entry.Packet.HigherOrderSH, SourceBase, CoefficientIndex)));
                }
                const float Scale = MaximumMagnitude > 0.0f ? MaximumMagnitude / 127.0f : 0.0f;
                QuantizedSH[TargetBase] = uint32(FFloat16(Scale).Encoded);
                if (Scale <= 0.0f) continue;
                for (int32 CoefficientIndex = 0; CoefficientIndex < SHCount; ++CoefficientIndex)
                {
                    const float Value = ReadPackedHalf(
                        *Entry.Packet.HigherOrderSH,
                        SourceBase,
                        CoefficientIndex);
                    const int32 Quantized = FMath::Clamp(FMath::RoundToInt(Value / Scale), -127, 127);
                    QuantizedSH[TargetBase + 1 + CoefficientIndex / 4] |=
                        uint32(uint8(int8(Quantized))) << ((CoefficientIndex & 3) * 8);
                }
            }
            Result.SHBuffer = CreateStructuredBuffer(
                GraphBuilder,
                TEXT("KasumiSplat.SHUpload"),
                QuantizedSH);
        }
        else
        {
            const TArray<uint32> DummySH{0u};
            Result.SHBuffer = CreateStructuredBuffer(GraphBuilder, TEXT("KasumiSplat.SHUpload"), DummySH);
        }
        Resources.SHWordsPerPoint = Result.SHWordsPerPoint;
        GraphBuilder.QueueBufferExtraction(Result.SHBuffer, &Resources.SHBuffer);
    }
    return Result;
}
