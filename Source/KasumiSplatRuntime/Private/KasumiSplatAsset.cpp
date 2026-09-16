#include "KasumiSplatAsset.h"

#include "Async/Async.h"
#include "Misc/ScopeLock.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_EDITORONLY_DATA
#include "EditorFramework/AssetImportData.h"
#endif

namespace
{
    struct FPackedKasumiSplatPoint
    {
        FVector3f Position;
        FVector4f Rotation;
        FVector3f Sigma;
        FVector4f Color;
        int32 StableId;
    };

    constexpr int32 KasumiChunkPointCount = 65536;

#if WITH_EDITORONLY_DATA
    constexpr int32 KasumiThumbnailPointLimit = 2048;

    void BuildThumbnailPreview(
        const TArray<FKasumiSplatPoint>& Points,
        TArray<FKasumiSplatThumbnailPoint>& OutPoints)
    {
        OutPoints.Reset();
        if (Points.IsEmpty())
        {
            return;
        }

        const FVector ViewDirection = FVector(1.0, 1.0, 0.75).GetSafeNormal();
        const FVector Right = FVector(1.0, -1.0, 0.0).GetSafeNormal();
        const FVector Up = FVector::CrossProduct(Right, ViewDirection).GetSafeNormal();
        double MinU = TNumericLimits<double>::Max();
        double MaxU = TNumericLimits<double>::Lowest();
        double MinV = TNumericLimits<double>::Max();
        double MaxV = TNumericLimits<double>::Lowest();
        for (const FKasumiSplatPoint& Point : Points)
        {
            const double U = FVector::DotProduct(Point.Position, Right);
            const double V = FVector::DotProduct(Point.Position, Up);
            MinU = FMath::Min(MinU, U);
            MaxU = FMath::Max(MaxU, U);
            MinV = FMath::Min(MinV, V);
            MaxV = FMath::Max(MaxV, V);
        }

        const double CenterU = (MinU + MaxU) * 0.5;
        const double CenterV = (MinV + MaxV) * 0.5;
        const double Span = FMath::Max(FMath::Max(MaxU - MinU, MaxV - MinV), 1.0e-6);
        const int32 PreviewCount = FMath::Min(Points.Num(), KasumiThumbnailPointLimit);
        struct FPreviewCandidate
        {
            FKasumiSplatThumbnailPoint Point;
            double Depth = 0.0;
        };
        TArray<FPreviewCandidate> Candidates;
        Candidates.Reserve(PreviewCount);
        for (int32 PreviewIndex = 0; PreviewIndex < PreviewCount; ++PreviewIndex)
        {
            const int32 SourceIndex = int32((int64(PreviewIndex) * Points.Num()) / PreviewCount);
            const FKasumiSplatPoint& Source = Points[SourceIndex];
            FPreviewCandidate& Candidate = Candidates.AddDefaulted_GetRef();
            Candidate.Point.Position = FVector2f(
                float(0.5 + 0.9 * (FVector::DotProduct(Source.Position, Right) - CenterU) / Span),
                float(0.5 - 0.9 * (FVector::DotProduct(Source.Position, Up) - CenterV) / Span));
            Candidate.Point.Radius = float(FMath::Clamp(
                3.0 * Source.Sigma.GetAbs().GetMax() / Span,
                0.004,
                0.06));
            FLinearColor PreviewColor(
                FMath::Max(Source.Color.R, 0.0f),
                FMath::Max(Source.Color.G, 0.0f),
                FMath::Max(Source.Color.B, 0.0f),
                FMath::Clamp(Source.Color.A, 0.15f, 0.9f));
            Candidate.Point.Color = PreviewColor.ToFColorSRGB();
            Candidate.Depth = FVector::DotProduct(Source.Position, ViewDirection);
        }
        Candidates.StableSort([](const FPreviewCandidate& A, const FPreviewCandidate& B)
        {
            return A.Depth < B.Depth;
        });
        OutPoints.Reserve(Candidates.Num());
        for (FPreviewCandidate& Candidate : Candidates)
        {
            OutPoints.Add(MoveTemp(Candidate.Point));
        }
    }
#endif

    uint32 ExpandMortonBits(uint32 Value)
    {
        Value &= 0x000003ff;
        Value = (Value | (Value << 16)) & 0x030000ff;
        Value = (Value | (Value << 8)) & 0x0300f00f;
        Value = (Value | (Value << 4)) & 0x030c30c3;
        Value = (Value | (Value << 2)) & 0x09249249;
        return Value;
    }

    uint32 MortonKey(const FVector& Position, const FBox& Bounds)
    {
        const FVector Size = Bounds.GetSize();
        const FVector Normalized(
            Size.X > UE_SMALL_NUMBER ? (Position.X - Bounds.Min.X) / Size.X : 0.5,
            Size.Y > UE_SMALL_NUMBER ? (Position.Y - Bounds.Min.Y) / Size.Y : 0.5,
            Size.Z > UE_SMALL_NUMBER ? (Position.Z - Bounds.Min.Z) / Size.Z : 0.5);
        const uint32 X = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.X * 1023.0), 0, 1023));
        const uint32 Y = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.Y * 1023.0), 0, 1023));
        const uint32 Z = uint32(FMath::Clamp(FMath::FloorToInt(Normalized.Z * 1023.0), 0, 1023));
        return ExpandMortonBits(X) | (ExpandMortonBits(Y) << 1) | (ExpandMortonBits(Z) << 2);
    }

    void DecodePackedPoints(const FPackedKasumiSplatPoint* Packed, int32 PointCount, TArray<FKasumiSplatPoint>& OutPoints)
    {
        const int32 OutputOffset = OutPoints.AddUninitialized(PointCount);
        for (int32 Index = 0; Index < PointCount; ++Index)
        {
            FKasumiSplatPoint& Point = OutPoints[OutputOffset + Index];
            Point.Position = FVector(Packed[Index].Position);
            Point.Rotation = FQuat(Packed[Index].Rotation.X, Packed[Index].Rotation.Y, Packed[Index].Rotation.Z, Packed[Index].Rotation.W);
            Point.Sigma = FVector(Packed[Index].Sigma);
            Point.Color = FLinearColor(Packed[Index].Color.X, Packed[Index].Color.Y, Packed[Index].Color.Z, Packed[Index].Color.W);
            Point.StableId = Packed[Index].StableId;
        }
    }

    struct FKasumiChunkLoadState
    {
        explicit FKasumiChunkLoadState(UKasumiSplatAsset* InAsset)
            : Asset(InAsset)
        {
        }

        ~FKasumiChunkLoadState()
        {
            for (IBulkDataIORequest* Request : Requests)
            {
                delete Request;
            }
        }

        TStrongObjectPtr<UKasumiSplatAsset> Asset;
        UKasumiSplatAsset::FChunkLoadCallback Callback;
        FCriticalSection Mutex;
        TArray<TArray<FKasumiSplatPoint>> Results;
        TArray<TArray<float>> SHResults;
        int32 SHCoefficientsPerPoint = 0;
        TArray<IBulkDataIORequest*> Requests;
        TAtomic<int32> Remaining = 0;
        TAtomic<bool> bSuccess = true;
    };

    void FinishChunkLoad(const TSharedRef<FKasumiChunkLoadState, ESPMode::ThreadSafe>& State)
    {
        if (--State->Remaining != 0)
        {
            return;
        }

        Async(EAsyncExecution::ThreadPool, [State]() mutable
        {
            TArray<FKasumiSplatPoint> Combined;
            TArray<float> CombinedSH;
            if (State->bSuccess.Load())
            {
                int32 Total = 0;
                for (const TArray<FKasumiSplatPoint>& Chunk : State->Results) Total += Chunk.Num();
                Combined.Reserve(Total);
                for (TArray<FKasumiSplatPoint>& Chunk : State->Results) Combined.Append(MoveTemp(Chunk));
                if (State->SHCoefficientsPerPoint > 0)
                {
                    CombinedSH.Reserve(Total * State->SHCoefficientsPerPoint);
                    for (TArray<float>& Chunk : State->SHResults) CombinedSH.Append(MoveTemp(Chunk));
                }
            }
            AsyncTask(ENamedThreads::GameThread,
                [State, Combined = MoveTemp(Combined), CombinedSH = MoveTemp(CombinedSH)]() mutable
            {
                UKasumiSplatAsset::FChunkLoadCallback Callback = MoveTemp(State->Callback);
                Callback(
                    State->bSuccess.Load(),
                    MoveTemp(Combined),
                    MoveTemp(CombinedSH),
                    State->SHCoefficientsPerPoint);
            });
        });
    }
}

UKasumiSplatAsset::UKasumiSplatAsset()
{
#if WITH_EDITORONLY_DATA
    AssetImportData = CreateEditorOnlyDefaultSubobject<UAssetImportData>(TEXT("AssetImportData"));
#endif
}

void UKasumiSplatAsset::Serialize(FArchive& Ar)
{
    Super::Serialize(Ar);
    // Version 1 assets ended after reflected properties and have no bulk payload.
    if (PointBulkDataVersion > 0)
    {
        PointBulkData.Serialize(Ar, this);
    }
    if (HigherOrderSHBulkDataVersion > 0)
    {
        HigherOrderSHBulkData.Serialize(Ar, this);
    }
}

void UKasumiSplatAsset::SetImportedPoints(
    TArray<FKasumiSplatPoint>&& InPoints,
    const FString& InEncoding,
    double InUnitsToCentimeters,
    TArray<float>&& InHigherOrderSH,
    int32 InHigherOrderSHCoefficientsPerPoint,
    bool bInSpatiallySort,
    const FMatrix& InLocalToSHDirection,
    EKasumiSplatImportProfile InResolvedImportProfile)
{
    PackedPointCount = InPoints.Num();
    PointBulkDataVersion = 1;
    SourceEncoding = InEncoding;
    ImportedUnitsToCentimeters = InUnitsToCentimeters;
    LocalToSHDirection = InLocalToSHDirection;
    ResolvedImportProfile = InResolvedImportProfile;
    HigherOrderSHCoefficientsPerPoint =
        InHigherOrderSHCoefficientsPerPoint > 0 &&
        InHigherOrderSH.Num() == PackedPointCount * InHigherOrderSHCoefficientsPerPoint
            ? InHigherOrderSHCoefficientsPerPoint
            : 0;
    if (HigherOrderSHCoefficientsPerPoint == 0)
    {
        InHigherOrderSH.Reset();
    }
    bSpatiallySorted = bInSpatiallySort && InPoints.Num() > 1;
    if (bSpatiallySorted)
    {
        FBox SortBounds(ForceInit);
        for (const FKasumiSplatPoint& Point : InPoints) SortBounds += Point.Position;
        TArray<int32> Order;
        Order.SetNumUninitialized(InPoints.Num());
        for (int32 Index = 0; Index < Order.Num(); ++Index) Order[Index] = Index;
        Order.StableSort([&InPoints, &SortBounds](int32 A, int32 B)
        {
            return MortonKey(InPoints[A].Position, SortBounds) < MortonKey(InPoints[B].Position, SortBounds);
        });
        TArray<FKasumiSplatPoint> SortedPoints;
        SortedPoints.Reserve(InPoints.Num());
        TArray<float> SortedSH;
        if (HigherOrderSHCoefficientsPerPoint > 0) SortedSH.Reserve(InHigherOrderSH.Num());
        for (const int32 SourceIndex : Order)
        {
            SortedPoints.Add(MoveTemp(InPoints[SourceIndex]));
            if (HigherOrderSHCoefficientsPerPoint > 0)
            {
                SortedSH.Append(InHigherOrderSH.GetData() + int64(SourceIndex) * HigherOrderSHCoefficientsPerPoint, HigherOrderSHCoefficientsPerPoint);
            }
        }
        InPoints = MoveTemp(SortedPoints);
        InHigherOrderSH = MoveTemp(SortedSH);
    }
    DataVersion = CurrentDataVersion;
    ++Revision;

    LocalBounds.Init();
    Chunks.Reset();
    TArray<FPackedKasumiSplatPoint> Packed;
    Packed.SetNumUninitialized(InPoints.Num());
    for (int32 Index = 0; Index < InPoints.Num(); ++Index)
    {
        const FKasumiSplatPoint& Point = InPoints[Index];
        LocalBounds += Point.Position;
        FPackedKasumiSplatPoint& Target = Packed[Index];
        Target.Position = FVector3f(Point.Position);
        Target.Rotation = FVector4f(Point.Rotation.X, Point.Rotation.Y, Point.Rotation.Z, Point.Rotation.W);
        Target.Sigma = FVector3f(Point.Sigma);
        Target.Color = FVector4f(Point.Color.R, Point.Color.G, Point.Color.B, Point.Color.A);
        Target.StableId = Point.StableId;

        if (Index % KasumiChunkPointCount == 0)
        {
            FKasumiSplatChunk& Chunk = Chunks.AddDefaulted_GetRef();
            Chunk.FirstPoint = Index;
            Chunk.PointCount = FMath::Min(KasumiChunkPointCount, InPoints.Num() - Index);
            Chunk.LocalBounds.Init();
        }
        Chunks.Last().LocalBounds += Point.Position;
    }

#if WITH_EDITORONLY_DATA
    BuildThumbnailPreview(InPoints, ThumbnailPoints);
#endif

    PointBulkData.RemoveBulkData();
    PointBulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
    if (!Packed.IsEmpty())
    {
        void* Destination = PointBulkData.Lock(LOCK_READ_WRITE);
        Destination = PointBulkData.Realloc(int64(Packed.Num()) * sizeof(FPackedKasumiSplatPoint));
        FMemory::Memcpy(Destination, Packed.GetData(), int64(Packed.Num()) * sizeof(FPackedKasumiSplatPoint));
        PointBulkData.Unlock();
    }

    HigherOrderSHBulkData.RemoveBulkData();
    HigherOrderSHBulkDataVersion = HigherOrderSHCoefficientsPerPoint > 0 ? 1 : 0;
    if (!InHigherOrderSH.IsEmpty())
    {
        HigherOrderSHBulkData.SetBulkDataFlags(BULKDATA_Force_NOT_InlinePayload);
        void* Destination = HigherOrderSHBulkData.Lock(LOCK_READ_WRITE);
        Destination = HigherOrderSHBulkData.Realloc(int64(InHigherOrderSH.Num()) * sizeof(float));
        FMemory::Memcpy(Destination, InHigherOrderSH.GetData(), int64(InHigherOrderSH.Num()) * sizeof(float));
        HigherOrderSHBulkData.Unlock();
    }

    Points.Reset();
    HigherOrderSH.Reset();
}

bool UKasumiSplatAsset::LoadPoints(TArray<FKasumiSplatPoint>& OutPoints) const
{
    if (PackedPointCount <= 0 || PointBulkData.GetBulkDataSize() <= 0)
    {
        OutPoints = Points;
        return !OutPoints.IsEmpty();
    }

    const int64 ExpectedSize = int64(PackedPointCount) * sizeof(FPackedKasumiSplatPoint);
    if (PointBulkData.GetBulkDataSize() != ExpectedSize)
    {
        return false;
    }

    FScopeLock BulkDataLock(&BulkDataCriticalSection);
    const FPackedKasumiSplatPoint* Packed = static_cast<const FPackedKasumiSplatPoint*>(PointBulkData.LockReadOnly());
    if (!Packed)
    {
        return false;
    }
    OutPoints.Reset();
    OutPoints.Reserve(PackedPointCount);
    DecodePackedPoints(Packed, PackedPointCount, OutPoints);
    PointBulkData.Unlock();
    return true;
}

int32 UKasumiSplatAsset::GetHigherOrderSHCoefficientsPerPoint() const
{
    if (HigherOrderSHCoefficientsPerPoint > 0)
    {
        return HigherOrderSHCoefficientsPerPoint;
    }
    const int32 PointCount = GetPointCount();
    return PointCount > 0 && HigherOrderSH.Num() % PointCount == 0
        ? FMath::Clamp(HigherOrderSH.Num() / PointCount, 0, 45)
        : 0;
}

bool UKasumiSplatAsset::LoadHigherOrderSH(TArray<float>& OutHigherOrderSH) const
{
    OutHigherOrderSH.Reset();
    const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    if (CoefficientsPerPoint <= 0) return true;
    if (HigherOrderSHBulkDataVersion <= 0)
    {
        OutHigherOrderSH = HigherOrderSH;
        return OutHigherOrderSH.Num() == GetPointCount() * CoefficientsPerPoint;
    }
    const int64 ValueCount = int64(GetPointCount()) * CoefficientsPerPoint;
    if (HigherOrderSHBulkData.GetBulkDataSize() != ValueCount * sizeof(float)) return false;
    FScopeLock BulkDataLock(&BulkDataCriticalSection);
    const float* Source = static_cast<const float*>(HigherOrderSHBulkData.LockReadOnly());
    if (!Source) return false;
    OutHigherOrderSH.Append(Source, ValueCount);
    HigherOrderSHBulkData.Unlock();
    return true;
}

bool UKasumiSplatAsset::LoadPointChunks(
    const TArray<int32>& ChunkIndices,
    TArray<FKasumiSplatPoint>& OutPoints,
    TArray<float>* OutHigherOrderSH) const
{
    OutPoints.Reset();
    if (OutHigherOrderSH) OutHigherOrderSH->Reset();
    if (PointBulkDataVersion <= 0 || PackedPointCount <= 0 || Chunks.IsEmpty())
    {
        return LoadPoints(OutPoints);
    }

    FScopeLock BulkDataLock(&BulkDataCriticalSection);

    int32 TotalPoints = 0;
    for (const int32 ChunkIndex : ChunkIndices)
    {
        if (!Chunks.IsValidIndex(ChunkIndex)) return false;
        TotalPoints += Chunks[ChunkIndex].PointCount;
    }
    OutPoints.Reserve(TotalPoints);

    const FPackedKasumiSplatPoint* Packed = static_cast<const FPackedKasumiSplatPoint*>(PointBulkData.LockReadOnly());
    if (!Packed) return false;
    for (const int32 ChunkIndex : ChunkIndices)
    {
        const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
        DecodePackedPoints(Packed + Chunk.FirstPoint, Chunk.PointCount, OutPoints);
    }
    PointBulkData.Unlock();

    if (OutHigherOrderSH)
    {
        const int32 CoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
        if (CoefficientsPerPoint > 0)
        {
            if (HigherOrderSHBulkDataVersion > 0)
            {
                const float* SH = static_cast<const float*>(HigherOrderSHBulkData.LockReadOnly());
                if (!SH) return false;
                OutHigherOrderSH->Reserve(TotalPoints * CoefficientsPerPoint);
                for (const int32 ChunkIndex : ChunkIndices)
                {
                    const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
                    OutHigherOrderSH->Append(SH + int64(Chunk.FirstPoint) * CoefficientsPerPoint, Chunk.PointCount * CoefficientsPerPoint);
                }
                HigherOrderSHBulkData.Unlock();
            }
            else
            {
                OutHigherOrderSH->Reserve(TotalPoints * CoefficientsPerPoint);
                for (const int32 ChunkIndex : ChunkIndices)
                {
                    const FKasumiSplatChunk& Chunk = Chunks[ChunkIndex];
                    const int64 Offset = int64(Chunk.FirstPoint) * CoefficientsPerPoint;
                    if (Offset + int64(Chunk.PointCount) * CoefficientsPerPoint > HigherOrderSH.Num()) return false;
                    OutHigherOrderSH->Append(HigherOrderSH.GetData() + Offset, Chunk.PointCount * CoefficientsPerPoint);
                }
            }
        }
    }
    return true;
}

void UKasumiSplatAsset::LoadPointChunksAsync(
    const TArray<int32>& ChunkIndices,
    FChunkLoadCallback&& Callback) const
{
    TArray<int32> ValidIndices;
    ValidIndices.Reserve(ChunkIndices.Num());
    for (const int32 ChunkIndex : ChunkIndices)
    {
        if (Chunks.IsValidIndex(ChunkIndex)) ValidIndices.AddUnique(ChunkIndex);
    }

    if (ValidIndices.IsEmpty() || PointBulkDataVersion <= 0)
    {
        TStrongObjectPtr<UKasumiSplatAsset> StrongAsset(const_cast<UKasumiSplatAsset*>(this));
        Async(EAsyncExecution::ThreadPool,
            [StrongAsset = MoveTemp(StrongAsset), ValidIndices = MoveTemp(ValidIndices), Callback = MoveTemp(Callback)]() mutable
        {
            TArray<FKasumiSplatPoint> Loaded;
            TArray<float> LoadedSH;
            const bool bLoaded = StrongAsset->LoadPointChunks(ValidIndices, Loaded, &LoadedSH);
            const int32 SHCount = StrongAsset->GetHigherOrderSHCoefficientsPerPoint();
            AsyncTask(ENamedThreads::GameThread,
                [Callback = MoveTemp(Callback), bLoaded, Loaded = MoveTemp(Loaded), LoadedSH = MoveTemp(LoadedSH), SHCount]() mutable
            {
                Callback(bLoaded, MoveTemp(Loaded), MoveTemp(LoadedSH), SHCount);
            });
        });
        return;
    }

    TSharedRef<FKasumiChunkLoadState, ESPMode::ThreadSafe> State =
        MakeShared<FKasumiChunkLoadState, ESPMode::ThreadSafe>(const_cast<UKasumiSplatAsset*>(this));
    State->Callback = MoveTemp(Callback);
    State->Results.SetNum(ValidIndices.Num());
    State->SHCoefficientsPerPoint = GetHigherOrderSHCoefficientsPerPoint();
    State->SHResults.SetNum(ValidIndices.Num());
    const bool bStreamSH = State->SHCoefficientsPerPoint > 0 && HigherOrderSHBulkDataVersion > 0;
    State->Remaining = ValidIndices.Num() * (bStreamSH ? 2 : 1);

    if (PointBulkData.IsBulkDataLoaded() || (State->SHCoefficientsPerPoint > 0 && !bStreamSH) ||
        (bStreamSH && HigherOrderSHBulkData.IsBulkDataLoaded()))
    {
        Async(EAsyncExecution::ThreadPool, [State, ValidIndices = MoveTemp(ValidIndices)]() mutable
        {
            TArray<FKasumiSplatPoint> Loaded;
            TArray<float> LoadedSH;
            const bool bLoaded = State->Asset->LoadPointChunks(ValidIndices, Loaded, &LoadedSH);
            State->bSuccess = bLoaded;
            if (bLoaded) State->Results[0] = MoveTemp(Loaded);
            if (bLoaded) State->SHResults[0] = MoveTemp(LoadedSH);
            State->Remaining = 1;
            FinishChunkLoad(State);
        });
        return;
    }

    State->Requests.Reserve(ValidIndices.Num());
    for (int32 RequestIndex = 0; RequestIndex < ValidIndices.Num(); ++RequestIndex)
    {
        const FKasumiSplatChunk Chunk = Chunks[ValidIndices[RequestIndex]];
        const int64 Offset = int64(Chunk.FirstPoint) * sizeof(FPackedKasumiSplatPoint);
        const int64 Bytes = int64(Chunk.PointCount) * sizeof(FPackedKasumiSplatPoint);
        FBulkDataIORequestCallBack RequestCallback =
            [State, RequestIndex, PointCount = Chunk.PointCount](bool bWasCancelled, IBulkDataIORequest* Request)
        {
            uint8* Memory = bWasCancelled ? nullptr : Request->GetReadResults();
            if (Memory)
            {
                DecodePackedPoints(reinterpret_cast<const FPackedKasumiSplatPoint*>(Memory), PointCount, State->Results[RequestIndex]);
                FMemory::Free(Memory);
            }
            else
            {
                State->bSuccess = false;
            }
            FinishChunkLoad(State);
        };
        IBulkDataIORequest* Request = PointBulkData.CreateStreamingRequest(
            Offset,
            Bytes,
            AIOP_Normal,
            &RequestCallback,
            nullptr);
        State->Requests.Add(Request);
        if (!Request)
        {
            State->bSuccess = false;
            FinishChunkLoad(State);
        }

        if (bStreamSH)
        {
            const int64 SHOffset = int64(Chunk.FirstPoint) * State->SHCoefficientsPerPoint * sizeof(float);
            const int64 SHBytes = int64(Chunk.PointCount) * State->SHCoefficientsPerPoint * sizeof(float);
            FBulkDataIORequestCallBack SHRequestCallback =
                [State, RequestIndex, ValueCount = Chunk.PointCount * State->SHCoefficientsPerPoint](bool bWasCancelled, IBulkDataIORequest* SHRequest)
            {
                uint8* Memory = bWasCancelled ? nullptr : SHRequest->GetReadResults();
                if (Memory)
                {
                    const float* Values = reinterpret_cast<const float*>(Memory);
                    State->SHResults[RequestIndex].Append(Values, ValueCount);
                    FMemory::Free(Memory);
                }
                else
                {
                    State->bSuccess = false;
                }
                FinishChunkLoad(State);
            };
            IBulkDataIORequest* SHRequest = HigherOrderSHBulkData.CreateStreamingRequest(
                SHOffset,
                SHBytes,
                AIOP_Normal,
                &SHRequestCallback,
                nullptr);
            State->Requests.Add(SHRequest);
            if (!SHRequest)
            {
                State->bSuccess = false;
                FinishChunkLoad(State);
            }
        }
    }
}
