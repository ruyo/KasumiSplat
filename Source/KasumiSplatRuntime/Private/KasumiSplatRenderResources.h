#pragma once

#include "KasumiSplatRenderer.h"
#include "RenderGraphResources.h"

struct FKasumiSplatRenderEntry
{
    uint32 ComponentId = 0;
    EKasumiSplatSortMode LastResolvedSortMode = EKasumiSplatSortMode::Auto;
    bool bHasResolvedSortMode = false;
    FKasumiSplatPacket Packet;
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
