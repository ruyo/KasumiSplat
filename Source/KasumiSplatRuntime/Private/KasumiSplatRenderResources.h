#pragma once

#include "KasumiSplatRenderer.h"
#include "RenderGraphResources.h"
#include "RHIGPUReadback.h"

struct FKasumiSplatRenderEntry
{
    uint32 ComponentId = 0;
    EKasumiSplatSortMode LastResolvedSortMode = EKasumiSplatSortMode::Auto;
    bool bHasResolvedSortMode = false;
    FKasumiSplatPacket Packet;
    FTransform PreviousLocalToWorld = FTransform::Identity;
    uint64 LastPublishFrame = MAX_uint64;
    bool bVelocityHistoryValid = false;
    TUniquePtr<FRHIGPUBufferReadback> TiledOverflowReadback;
    bool bTiledOverflowReadbackPending = false;
    bool bTiledOverflowReadbackRelevant = false;
    bool bTiledOverflowRecoveryProbePending = false;
    bool bDelayedTiledFallbackActive = false;
    uint32 DelayedTiledFallbackFramesRemaining = 0;
    uint64 LastDelayedTiledFallbackFrame = MAX_uint64;
    TRefCountPtr<FRDGPooledBuffer> PointBuffer;
    TRefCountPtr<FRDGPooledBuffer> SHBuffer;
};

struct FKasumiSplatGPUData
{
    FRDGBufferRef PointBuffer = nullptr;
    FRDGBufferRef SHBuffer = nullptr;
};

FKasumiSplatGPUData GetOrCreateSplatGPUData(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatRenderEntry& Entry);
