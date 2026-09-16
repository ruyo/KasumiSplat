#include "KasumiSplatStreaming.h"

#include "KasumiSplatAsset.h"

namespace
{
    struct FKasumiCovariance3
    {
        double M[3][3] = {};
    };

    FKasumiCovariance3 BuildPointCovariance(const FKasumiSplatPoint& Point)
    {
        const FVector Axes[3] = {
            Point.Rotation.GetAxisX(),
            Point.Rotation.GetAxisY(),
            Point.Rotation.GetAxisZ()
        };
        const FVector Sigma = Point.Sigma.GetAbs();
        const double Variance[3] = {
            FMath::Square(Sigma.X),
            FMath::Square(Sigma.Y),
            FMath::Square(Sigma.Z)
        };
        FKasumiCovariance3 Result;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            for (int32 Row = 0; Row < 3; ++Row)
            {
                for (int32 Column = 0; Column < 3; ++Column)
                {
                    Result.M[Row][Column] += Variance[Axis] * Axes[Axis][Row] * Axes[Axis][Column];
                }
            }
        }
        return Result;
    }

    void DecomposeCovariance(
        const FKasumiCovariance3& Input,
        FQuat& OutRotation,
        FVector& OutSigma)
    {
        double A[3][3];
        double V[3][3] = {
            {1.0, 0.0, 0.0},
            {0.0, 1.0, 0.0},
            {0.0, 0.0, 1.0}
        };
        FMemory::Memcpy(A, Input.M, sizeof(A));

        for (int32 Iteration = 0; Iteration < 12; ++Iteration)
        {
            int32 P = 0;
            int32 Q = 1;
            double Largest = FMath::Abs(A[0][1]);
            if (FMath::Abs(A[0][2]) > Largest) { P = 0; Q = 2; Largest = FMath::Abs(A[0][2]); }
            if (FMath::Abs(A[1][2]) > Largest) { P = 1; Q = 2; Largest = FMath::Abs(A[1][2]); }
            if (Largest <= 1.0e-10)
            {
                break;
            }

            const double Angle = 0.5 * FMath::Atan2(2.0 * A[P][Q], A[Q][Q] - A[P][P]);
            const double C = FMath::Cos(Angle);
            const double S = FMath::Sin(Angle);
            const double App = A[P][P];
            const double Aqq = A[Q][Q];
            const double Apq = A[P][Q];
            A[P][P] = C * C * App - 2.0 * S * C * Apq + S * S * Aqq;
            A[Q][Q] = S * S * App + 2.0 * S * C * Apq + C * C * Aqq;
            A[P][Q] = A[Q][P] = 0.0;
            for (int32 K = 0; K < 3; ++K)
            {
                if (K != P && K != Q)
                {
                    const double Akp = A[K][P];
                    const double Akq = A[K][Q];
                    A[K][P] = A[P][K] = C * Akp - S * Akq;
                    A[K][Q] = A[Q][K] = S * Akp + C * Akq;
                }
                const double Vkp = V[K][P];
                const double Vkq = V[K][Q];
                V[K][P] = C * Vkp - S * Vkq;
                V[K][Q] = S * Vkp + C * Vkq;
            }
        }

        FVector AxisX(V[0][0], V[1][0], V[2][0]);
        FVector AxisY(V[0][1], V[1][1], V[2][1]);
        FVector AxisZ(V[0][2], V[1][2], V[2][2]);
        AxisX.Normalize();
        AxisY = (AxisY - AxisX * FVector::DotProduct(AxisX, AxisY)).GetSafeNormal();
        AxisZ = FVector::CrossProduct(AxisX, AxisY).GetSafeNormal();
        FMatrix Basis = FMatrix::Identity;
        Basis.SetAxes(&AxisX, &AxisY, &AxisZ);
        OutRotation = FQuat(Basis).GetNormalized();
        OutSigma = FVector(
            FMath::Sqrt(FMath::Max(A[0][0], 1.0e-6)),
            FMath::Sqrt(FMath::Max(A[1][1], 1.0e-6)),
            FMath::Sqrt(FMath::Max(A[2][2], 1.0e-6)));
    }

    int32 BuildMergedStableId(TConstArrayView<FKasumiSplatPoint> Points, int32 LODStride)
    {
        uint32 Hash = HashCombineFast(0x4b534c4fu, GetTypeHash(LODStride));
        for (const FKasumiSplatPoint& Point : Points)
        {
            Hash = HashCombineFast(Hash, GetTypeHash(Point.StableId));
        }
        Hash |= 0x80000000u;
        if (Hash == uint32(INDEX_NONE)) Hash ^= 0x01010101u;
        return int32(Hash);
    }
}

int32 ResolveKasumiSplatLODStride(
    double Distance,
    float LODStartDistance,
    int32 MaxLODStride,
    int32 PreviousLODStride)
{
    const int32 ClampedMaximum = FMath::Max(1, MaxLODStride);
    int32 Stride = FMath::Clamp(FMath::RoundUpToPowerOfTwo(FMath::Max(1, PreviousLODStride)), 1, ClampedMaximum);
    if (LODStartDistance <= 0.0f) return 1;

    constexpr double Hysteresis = 0.10;
    while (Stride < ClampedMaximum)
    {
        const double IncreaseThreshold = double(LODStartDistance) * double(Stride) * (1.0 + Hysteresis);
        if (Distance <= IncreaseThreshold) break;
        Stride = FMath::Min(Stride * 2, ClampedMaximum);
    }
    while (Stride > 1)
    {
        const double DecreaseThreshold = double(LODStartDistance) * double(Stride / 2) * (1.0 - Hysteresis);
        if (Distance >= DecreaseThreshold) break;
        Stride /= 2;
    }
    return Stride;
}

FKasumiSplatStreamingSelection BuildKasumiSplatStreamingSelection(
    const UKasumiSplatAsset& Asset,
    const FVector& LocalView,
    const FKasumiSplatStreamingSettings& Settings)
{
    struct FCandidate
    {
        int32 ChunkIndex = INDEX_NONE;
        double DistanceSquared = 0.0;
    };

    TArray<FCandidate> Candidates;
    Candidates.Reserve(Asset.Chunks.Num());
    const double MaxDistanceSquared = Settings.MaxDistance > 0.0f
        ? FMath::Square(double(Settings.MaxDistance))
        : TNumericLimits<double>::Max();
    for (int32 ChunkIndex = 0; ChunkIndex < Asset.Chunks.Num(); ++ChunkIndex)
    {
        const FKasumiSplatChunk& Chunk = Asset.Chunks[ChunkIndex];
        const double DistanceSquared = FVector::DistSquared(LocalView, Chunk.LocalBounds.GetClosestPointTo(LocalView));
        if (DistanceSquared <= MaxDistanceSquared)
        {
            Candidates.Add({ChunkIndex, DistanceSquared});
        }
    }
    Candidates.Sort([](const FCandidate& A, const FCandidate& B)
    {
        if (A.DistanceSquared != B.DistanceSquared) return A.DistanceSquared < B.DistanceSquared;
        return A.ChunkIndex < B.ChunkIndex;
    });

    const int32 SHCount = Asset.GetHigherOrderSHCoefficientsPerPoint();
    const int64 ApproxBytesPerPoint = sizeof(FKasumiSplatPoint) + 4 * sizeof(FVector4f) + int64(SHCount) * sizeof(float);
    const int64 BudgetBytes = int64(FMath::Max(16, Settings.MemoryBudgetMB)) * 1024 * 1024;
    const int32 BudgetPointCount = int32(FMath::Clamp<int64>(
        BudgetBytes / FMath::Max<int64>(ApproxBytesPerPoint, 1), 1, MAX_int32));
    const int32 PointLimit = FMath::Max(1, FMath::Min(Settings.MaxResidentSplats, BudgetPointCount));

    FKasumiSplatStreamingSelection Result;
    int32 SelectedPointCount = 0;
    for (const FCandidate& Candidate : Candidates)
    {
        const int32 ChunkPointCount = Asset.Chunks[Candidate.ChunkIndex].PointCount;
        if (!Result.ChunkIndices.IsEmpty() && SelectedPointCount + ChunkPointCount > PointLimit) continue;
        Result.ChunkIndices.Add(Candidate.ChunkIndex);
        SelectedPointCount += ChunkPointCount;
        if (SelectedPointCount >= PointLimit) break;
    }

    const double CaptureDistance = FVector::Dist(LocalView, Asset.LocalBounds.GetClosestPointTo(LocalView));
    Result.LODStride = ResolveKasumiSplatLODStride(
        CaptureDistance,
        Settings.LODStartDistance,
        Settings.MaxLODStride,
        Settings.PreviousLODStride);

    Result.Hash = GetTypeHash(Result.LODStride);
    for (const int32 ChunkIndex : Result.ChunkIndices)
    {
        Result.Hash = HashCombineFast(Result.Hash, GetTypeHash(ChunkIndex));
    }
    return Result;
}

void ApplyKasumiSplatLOD(
    int32 LODStride,
    int32 SHCoefficientsPerPoint,
    TArray<FKasumiSplatPoint>& Points,
    TArray<float>& HigherOrderSH)
{
    if (LODStride <= 1) return;

    const bool bHasMatchingSH = SHCoefficientsPerPoint > 0 &&
        HigherOrderSH.Num() == Points.Num() * SHCoefficientsPerPoint;
    TArray<FKasumiSplatPoint> MergedPoints;
    TArray<float> MergedSH;
    MergedPoints.Reserve(FMath::DivideAndRoundUp(Points.Num(), LODStride));
    if (bHasMatchingSH)
    {
        MergedSH.Reserve(FMath::DivideAndRoundUp(Points.Num(), LODStride) * SHCoefficientsPerPoint);
    }
    for (int32 GroupStart = 0; GroupStart < Points.Num(); GroupStart += LODStride)
    {
        const int32 GroupEnd = FMath::Min(GroupStart + LODStride, Points.Num());
        if (GroupEnd - GroupStart == 1)
        {
            MergedPoints.Add(Points[GroupStart]);
            if (bHasMatchingSH)
            {
                MergedSH.Append(
                    HigherOrderSH.GetData() + GroupStart * SHCoefficientsPerPoint,
                    SHCoefficientsPerPoint);
            }
            continue;
        }
        double WeightSum = 0.0;
        double Transmittance = 1.0;
        FVector Mean = FVector::ZeroVector;
        FVector3d WeightedColor = FVector3d::ZeroVector;
        for (int32 PointIndex = GroupStart; PointIndex < GroupEnd; ++PointIndex)
        {
            const FKasumiSplatPoint& Point = Points[PointIndex];
            const double Alpha = FMath::Clamp(double(Point.Color.A), 0.0, 1.0);
            const double Weight = FMath::Max(Alpha, 1.0e-6);
            WeightSum += Weight;
            Mean += Point.Position * Weight;
            WeightedColor += FVector3d(Point.Color.R, Point.Color.G, Point.Color.B) * Weight;
            Transmittance *= 1.0 - Alpha;
        }
        Mean /= WeightSum;

        FKasumiCovariance3 MergedCovariance;
        for (int32 PointIndex = GroupStart; PointIndex < GroupEnd; ++PointIndex)
        {
            const FKasumiSplatPoint& Point = Points[PointIndex];
            const double Weight = FMath::Max(FMath::Clamp(double(Point.Color.A), 0.0, 1.0), 1.0e-6);
            const FKasumiCovariance3 PointCovariance = BuildPointCovariance(Point);
            const FVector Delta = Point.Position - Mean;
            for (int32 Row = 0; Row < 3; ++Row)
            {
                for (int32 Column = 0; Column < 3; ++Column)
                {
                    MergedCovariance.M[Row][Column] += Weight *
                        (PointCovariance.M[Row][Column] + Delta[Row] * Delta[Column]);
                }
            }
        }
        for (int32 Row = 0; Row < 3; ++Row)
        {
            for (int32 Column = 0; Column < 3; ++Column)
            {
                MergedCovariance.M[Row][Column] /= WeightSum;
            }
        }

        FKasumiSplatPoint& Merged = MergedPoints.AddDefaulted_GetRef();
        Merged.StableId = BuildMergedStableId(
            MakeArrayView(Points).Slice(GroupStart, GroupEnd - GroupStart),
            LODStride);
        Merged.Position = Mean;
        DecomposeCovariance(MergedCovariance, Merged.Rotation, Merged.Sigma);
        const FVector3d AverageColor = WeightedColor / WeightSum;
        Merged.Color = FLinearColor(
            float(AverageColor.X),
            float(AverageColor.Y),
            float(AverageColor.Z),
            float(1.0 - Transmittance));

        if (bHasMatchingSH)
        {
            const int32 OutputStart = MergedSH.AddZeroed(SHCoefficientsPerPoint);
            for (int32 PointIndex = GroupStart; PointIndex < GroupEnd; ++PointIndex)
            {
                const double Weight = FMath::Max(FMath::Clamp(double(Points[PointIndex].Color.A), 0.0, 1.0), 1.0e-6);
                for (int32 CoefficientIndex = 0; CoefficientIndex < SHCoefficientsPerPoint; ++CoefficientIndex)
                {
                    MergedSH[OutputStart + CoefficientIndex] += float(
                        Weight * HigherOrderSH[PointIndex * SHCoefficientsPerPoint + CoefficientIndex] / WeightSum);
                }
            }
        }
    }
    Points = MoveTemp(MergedPoints);
    HigherOrderSH = MoveTemp(MergedSH);
}

FKasumiSplatResidentSnapshot BuildKasumiSplatTransitionSnapshot(
    const FKasumiSplatResidentSnapshot& PreviousTarget,
    const FKasumiSplatResidentSnapshot& NewTarget)
{
    if (!PreviousTarget.Points.IsValid() || PreviousTarget.Points->IsEmpty() ||
        !NewTarget.Points.IsValid())
    {
        return NewTarget;
    }

    const TArray<FKasumiSplatPoint>& PreviousPoints = *PreviousTarget.Points;
    const TArray<FKasumiSplatPoint>& NewPoints = *NewTarget.Points;
    TMap<int32, int32> PreviousIndexById;
    PreviousIndexById.Reserve(PreviousPoints.Num());
    for (int32 PreviousIndex = 0; PreviousIndex < PreviousPoints.Num(); ++PreviousIndex)
    {
        PreviousIndexById.FindOrAdd(PreviousPoints[PreviousIndex].StableId, PreviousIndex);
    }

    TArray<FKasumiSplatPoint> MergedPoints;
    TArray<uint8> TransitionClasses;
    MergedPoints.Reserve(PreviousPoints.Num() + NewPoints.Num());
    TransitionClasses.Reserve(PreviousPoints.Num() + NewPoints.Num());
    TBitArray<> PreviousPointUsed(false, PreviousPoints.Num());

    const int32 SHCount = NewTarget.HigherOrderSHCoefficientsPerPoint;
    const bool bCanMergeSH = SHCount > 0 &&
        PreviousTarget.HigherOrderSHCoefficientsPerPoint == SHCount &&
        PreviousTarget.HigherOrderSH.IsValid() && NewTarget.HigherOrderSH.IsValid() &&
        PreviousTarget.HigherOrderSH->Num() == PreviousPoints.Num() * SHCount &&
        NewTarget.HigherOrderSH->Num() == NewPoints.Num() * SHCount;
    TArray<float> MergedSH;
    if (bCanMergeSH)
    {
        MergedSH.Reserve((PreviousPoints.Num() + NewPoints.Num()) * SHCount);
    }

    for (int32 NewIndex = 0; NewIndex < NewPoints.Num(); ++NewIndex)
    {
        const int32* PreviousIndex = PreviousIndexById.Find(NewPoints[NewIndex].StableId);
        const bool bStable = PreviousIndex && !PreviousPointUsed[*PreviousIndex];
        if (bStable)
        {
            PreviousPointUsed[*PreviousIndex] = true;
        }
        MergedPoints.Add(NewPoints[NewIndex]);
        TransitionClasses.Add(uint8(bStable
            ? EKasumiSplatTransitionClass::Stable
            : EKasumiSplatTransitionClass::Entering));
        if (bCanMergeSH)
        {
            MergedSH.Append(NewTarget.HigherOrderSH->GetData() + NewIndex * SHCount, SHCount);
        }
    }

    for (int32 PreviousIndex = 0; PreviousIndex < PreviousPoints.Num(); ++PreviousIndex)
    {
        if (PreviousPointUsed[PreviousIndex])
        {
            continue;
        }
        MergedPoints.Add(PreviousPoints[PreviousIndex]);
        TransitionClasses.Add(uint8(EKasumiSplatTransitionClass::Leaving));
        if (bCanMergeSH)
        {
            MergedSH.Append(PreviousTarget.HigherOrderSH->GetData() + PreviousIndex * SHCount, SHCount);
        }
    }

    FKasumiSplatResidentSnapshot Result;
    Result.Points = MakeShared<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe>(MoveTemp(MergedPoints));
    Result.TransitionClasses = MakeShared<const TArray<uint8>, ESPMode::ThreadSafe>(MoveTemp(TransitionClasses));
    if (bCanMergeSH)
    {
        Result.HigherOrderSH = MakeShared<const TArray<float>, ESPMode::ThreadSafe>(MoveTemp(MergedSH));
        Result.HigherOrderSHCoefficientsPerPoint = SHCount;
    }
    return Result;
}
