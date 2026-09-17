#pragma once

#include "CoreMinimal.h"
#include "KasumiSplatTypes.h"
#include "RHIResources.h"

class FSceneInterface;

/** Value-only snapshot. No UObject may be accessed from the rendering thread. */
struct FKasumiSplatPacket
{
    const FSceneInterface* Scene = nullptr;
    FTransform LocalToWorld;
    FBox LocalBounds = FBox(ForceInit);
    TSharedPtr<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe> Points;
    TSharedPtr<const TArray<float>, ESPMode::ThreadSafe> HigherOrderSH;
    TSharedPtr<const TArray<uint8>, ESPMode::ThreadSafe> TransitionClasses;
    uint32 HigherOrderSHCoefficientsPerPoint = 0;
    float TemporalTransitionAlpha = 1.0f;
    EKasumiSplatVelocityMode VelocityMode = EKasumiSplatVelocityMode::ActorAndCamera;
    uint64 FrameNumber = 0;
    bool bResetVelocityHistory = true;
    FMatrix44f LocalToSHDirection = FMatrix44f::Identity;
    FKasumiSplatStyle Style;
    FKasumiSplatAppearance Appearance;
    TArray<FKasumiSplatEffectLayer> EffectLayers;
    EKasumiSplatSortMode SortMode = EKasumiSplatSortMode::Auto;
    uint32 TileSizePixels = 32;
    uint32 MaxTilesPerSplat = 64;
    uint32 TiledPairBudgetMB = 256;
    EKasumiSplatTiledFallbackMode TiledFallbackMode = EKasumiSplatTiledFallbackMode::SameFrame;
    uint32 MaxVisibleSplats = 750000;
    float MinProjectedRadiusPixels = 0.25f;
    float MaxProjectedRadiusPixels = 1024.0f;
    float AntialiasingFilterVariance = 0.3f;
    EKasumiSplatAntialiasingMode AntialiasingMode = EKasumiSplatAntialiasingMode::AreaCompensated;
    float MaxAnisotropy = 32.0f;
    float MaxSplatSigmaCentimeters = 100.0f;
    float SceneDepthBias = 0.00001f;
    bool bUseSceneDepth = true;
    float DepthPreCullMaxRadiusPixels = 8.0f;
    EKasumiSplatExternalMask ExternalMaskMode = EKasumiSplatExternalMask::None;
    FVector ExternalMaskCenter = FVector::ZeroVector;
    FVector ExternalMaskExtent = FVector(100.0);
    float ExternalMaskStrength = 1.0f;
    bool bInvertExternalMask = false;
    FTextureRHIRef EffectMaskTexture;
    FTextureRHIRef EffectMaskVolume;
};

namespace KasumiSplat
{
    void StartRenderer();
    void StopRenderer();
    void Publish(uint32 Id, FKasumiSplatPacket Packet);
    void Remove(uint32 Id);
    FVector EvaluatePosition(const FKasumiSplatPoint& Point, const FKasumiSplatStyle& Style);
}
