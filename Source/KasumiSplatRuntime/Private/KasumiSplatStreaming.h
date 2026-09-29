#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatTypes.h"

class UKasumiSplatAsset;

enum class EKasumiSplatTransitionClass : uint8
{
    Stable = 0,
    Entering = 1,
    Leaving = 2
};

struct FKasumiSplatResidentSnapshot
{
    TSharedPtr<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe> Points;
    /** Per-point GPU layout: two FP16 coefficients per uint32, padded to a word boundary. */
    TSharedPtr<const TArray<uint32>, ESPMode::ThreadSafe> HigherOrderSH;
    TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> TransitionClasses;
    int32 HigherOrderSHCoefficientsPerPoint = 0;
};

struct FKasumiSplatStreamingState
{
    TArray<FKasumiSplatPoint> SourcePoints;
    TArray<uint32> SourceHigherOrderSH;
    int32 SourceHigherOrderSHCoefficientsPerPoint = 0;
    FKasumiSplatResidentSnapshot RenderSnapshot;
    FKasumiSplatResidentSnapshot TargetSnapshot;
    TWeakObjectPtr<UKasumiSplatAsset> LoadedAsset;
    int32 LoadedAssetRevision = INDEX_NONE;
    int32 ResidentRevision = 0;
    uint32 SelectionHash = 0;
    uint32 RequestSerial = 0;
    int32 LastLODStride = 1;
    double UpdateAccumulator = 0.0;
    float TemporalTransitionAlpha = 1.0f;
    bool bRequestPending = false;
    bool bLastFullQualityMode = false;
    bool bLastQualityProbeMode = false;
};

struct FKasumiSplatStreamingSettings
{
    int32 MemoryBudgetMB = 256;
    int32 MaxResidentSplats = 1000000;
    int32 HigherOrderSHCoefficientsPerPoint = INDEX_NONE;
    EKasumiSplatMemoryPressurePolicy MemoryPressurePolicy = EKasumiSplatMemoryPressurePolicy::PreserveDetail;
    float MaxDistance = 0.0f;
    float LODStartDistance = 2500.0f;
    int32 MaxLODStride = 4;
    int32 PreviousLODStride = 1;
};

struct FKasumiSplatStreamingSelection
{
    TArray<int32> ChunkIndices;
    int32 LODStride = 1;
    uint32 Hash = 0;
};

FKasumiSplatStreamingSelection BuildKasumiSplatStreamingSelection(
    const UKasumiSplatAsset& Asset,
    const FVector& LocalView,
    const FKasumiSplatStreamingSettings& Settings);

int64 EstimateKasumiSplatResidentBytesPerPoint(int32 HigherOrderSHCoefficientsPerPoint);

int32 ResolveKasumiSplatSHCoefficientsPerPoint(
    EKasumiSplatSHDegree Degree,
    int32 SourceCoefficientsPerPoint);

void ReduceKasumiSplatSHDegree(
    EKasumiSplatSHDegree Degree,
    int32 PointCount,
    int32& InOutCoefficientsPerPoint,
    TArray<float>& InOutHigherOrderSH);

void PackKasumiSplatSH(
    TConstArrayView<float> HigherOrderSH,
    int32 PointCount,
    int32 CoefficientsPerPoint,
    TArray<uint32>& OutPackedHigherOrderSH);

float ReadKasumiSplatPackedSH(
    TConstArrayView<uint32> PackedHigherOrderSH,
    int32 PointIndex,
    int32 CoefficientIndex,
    int32 CoefficientsPerPoint);

void ReduceKasumiSplatPackedSHDegree(
    EKasumiSplatSHDegree Degree,
    int32 PointCount,
    int32& InOutCoefficientsPerPoint,
    TArray<uint32>& InOutPackedHigherOrderSH);

int32 ResolveKasumiSplatLODStride(
    double Distance,
    float LODStartDistance,
    int32 MaxLODStride,
    int32 PreviousLODStride);

void ApplyKasumiSplatLOD(
    int32 LODStride,
    int32 SHCoefficientsPerPoint,
    TArray<FKasumiSplatPoint>& Points,
    TArray<float>& HigherOrderSH);

void ApplyKasumiSplatPackedLOD(
    int32 LODStride,
    int32 SHCoefficientsPerPoint,
    TArray<FKasumiSplatPoint>& Points,
    TArray<uint32>& PackedHigherOrderSH);

FKasumiSplatResidentSnapshot BuildKasumiSplatTransitionSnapshot(
    const FKasumiSplatResidentSnapshot& PreviousTarget,
    const FKasumiSplatResidentSnapshot& NewTarget);
