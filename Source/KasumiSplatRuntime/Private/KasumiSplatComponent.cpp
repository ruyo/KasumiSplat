#include "KasumiSplatComponent.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatConfiguration.h"
#include "KasumiSplatRenderer.h"
#include "KasumiSplatSettings.h"
#include "KasumiSplatStreaming.h"
#include "Engine/World.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"
#include "Curves/CurveFloat.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"
#include "RHI.h"
#include "Engine/Texture2D.h"
#include "Engine/VolumeTexture.h"
#include "TextureResource.h"
#include "HAL/IConsoleManager.h"
#include "Async/Async.h"

namespace
{
    TAutoConsoleVariable<int32> CVarKasumiSplatQualityProbe(
        TEXT("r.KasumiSplat.QualityProbe"),
        0,
        TEXT("Replace component data with a deterministic dense overlap probe for visual validation."),
        ECVF_Default);

    bool IsFullQualityEnabled(bool bComponentSetting)
    {
        const IConsoleVariable* Override = IConsoleManager::Get().FindConsoleVariable(TEXT("r.KasumiSplat.FullQuality"));
        return bComponentSetting || (Override && Override->GetInt() != 0);
    }

    void BuildQualityProbePoints(const UKasumiSplatComponent& Component, TArray<FKasumiSplatPoint>& Points)
    {
        Points.Reset();
        constexpr int32 GridSize = 24;
        constexpr int32 LayerCount = 4;
        constexpr float Spacing = 7.0f;
        FVector LocalCenter = FVector::ZeroVector;
        FVector LocalForward = FVector::ForwardVector;
        FVector LocalRight = FVector::RightVector;
        FVector LocalUp = FVector::UpVector;
        if (const UWorld* World = Component.GetWorld())
        {
            if (const APlayerController* Controller = World->GetFirstPlayerController())
            {
                if (const APlayerCameraManager* Camera = Controller->PlayerCameraManager)
                {
                    const FTransform& ComponentTransform = Component.GetComponentTransform();
                    const FRotationMatrix CameraBasis(Camera->GetCameraRotation());
                    LocalCenter = ComponentTransform.InverseTransformPosition(
                        Camera->GetCameraLocation() + CameraBasis.GetScaledAxis(EAxis::X) * 600.0f);
                    LocalForward = ComponentTransform.InverseTransformVectorNoScale(
                        CameraBasis.GetScaledAxis(EAxis::X)).GetSafeNormal();
                    LocalRight = ComponentTransform.InverseTransformVectorNoScale(
                        CameraBasis.GetScaledAxis(EAxis::Y)).GetSafeNormal();
                    LocalUp = ComponentTransform.InverseTransformVectorNoScale(
                        CameraBasis.GetScaledAxis(EAxis::Z)).GetSafeNormal();
                }
            }
        }
        int32 StableId = 0;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            for (int32 Layer = 0; Layer < LayerCount; ++Layer)
            {
                for (int32 V = 0; V < GridSize; ++V)
                {
                    for (int32 U = 0; U < GridSize; ++U)
                    {
                        const float A = (float(U) - 0.5f * float(GridSize - 1)) * Spacing;
                        const float B = (float(V) - 0.5f * float(GridSize - 1)) * Spacing;
                        const float Depth = (float(Layer) - 0.5f * float(LayerCount - 1)) * 9.0f +
                            FMath::Sin(A * 0.045f) * 4.0f;
                        FKasumiSplatPoint& Point = Points.AddDefaulted_GetRef();
                        Point.StableId = StableId++;
                        Point.Position = LocalCenter + (Axis == 0
                            ? LocalForward * Depth + LocalRight * A + LocalUp * B
                            : (Axis == 1
                                ? LocalForward * A + LocalRight * Depth + LocalUp * B
                                : LocalForward * A + LocalRight * B + LocalUp * Depth));
                        Point.Sigma = FVector(4.0f);
                        Point.Rotation = FRotator(Axis * 31.0f, Layer * 17.0f, (U + V) % 9).Quaternion();
                        const FLinearColor AxisTint = Axis == 0 ? FLinearColor(0.95f, 0.20f, 0.12f) :
                            (Axis == 1 ? FLinearColor(0.12f, 0.85f, 0.30f) : FLinearColor(0.15f, 0.35f, 1.0f));
                        const float Checker = ((U / 3 + V / 3 + Layer) & 1) ? 0.72f : 1.0f;
                        Point.Color = FLinearColor(
                            AxisTint.R * Checker,
                            AxisTint.G * Checker,
                            AxisTint.B * Checker,
                            0.28f + 0.08f * float(Layer));
                    }
                }
            }
        }
    }
}

UKasumiSplatComponent::UKasumiSplatComponent()
{
    StreamingState = new FKasumiSplatStreamingState();
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = true;
    if (const UKasumiSplatSettings* Settings = GetDefault<UKasumiSplatSettings>())
    {
        StreamingMemoryBudgetMB = Settings->DefaultStreamingMemoryBudgetMB;
        MaxResidentSplats = Settings->DefaultMaxResidentSplats;
        bUseSceneDepth = Settings->bEnableSceneDepthByDefault;
    }
}

UKasumiSplatComponent::~UKasumiSplatComponent()
{
    delete StreamingState;
    StreamingState = nullptr;
}

int32 UKasumiSplatComponent::GetLoadedPointCount() const
{
    return StreamingState->SourcePoints.Num();
}

int32 UKasumiSplatComponent::GetLoadedHigherOrderSHValueCount() const
{
    return StreamingState->SourceHigherOrderSH.Num();
}

int32 UKasumiSplatComponent::GetResidentRevision() const
{
    return StreamingState->ResidentRevision;
}

TSharedPtr<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe>
UKasumiSplatComponent::GetResidentPointSnapshot() const
{
    return StreamingState->TargetSnapshot.Points.IsValid()
        ? StreamingState->TargetSnapshot.Points
        : StreamingState->RenderSnapshot.Points;
}

void UKasumiSplatComponent::ResetSyntheticPoints()
{
    StreamingState->SourcePoints.Reset();
    StreamingState->SourceHigherOrderSH.Reset();
    for (int32 Z = 0; Z < 3; ++Z)
    for (int32 Y = 0; Y < 3; ++Y)
    for (int32 X = 0; X < 3; ++X)
    {
        FKasumiSplatPoint& P = StreamingState->SourcePoints.AddDefaulted_GetRef();
        P.StableId = X + Y * 3 + Z * 9;
        P.Position = FVector(X - 1, Y - 1, Z - 1) * 40.0;
        P.Sigma = FVector(8.0, 15.0, 6.0);
        P.Rotation = FRotator(Z * 20.0, X * 25.0, Y * 30.0).Quaternion();
        P.Color = FLinearColor(0.2f + X * 0.35f, 0.2f + Y * 0.35f, 0.2f + Z * 0.35f, 0.85f);
    }
    RebuildSharedSourcePoints();
    Publish();
}

void UKasumiSplatComponent::OnRegister()
{
    Super::OnRegister();
    const int32 CurrentAssetRevision = Asset ? Asset->Revision : INDEX_NONE;
    if (!StreamingState->RenderSnapshot.Points.IsValid() || StreamingState->LoadedAsset.Get() != Asset || StreamingState->LoadedAssetRevision != CurrentAssetRevision)
    {
        ReloadPoints();
    }
    else
    {
        Publish();
    }
}

void UKasumiSplatComponent::OnUnregister()
{
    KasumiSplat::Remove(GetUniqueID());
    Super::OnUnregister();
}

void UKasumiSplatComponent::OnUpdateTransform(EUpdateTransformFlags UpdateTransformFlags, ETeleportType Teleport)
{
    Super::OnUpdateTransform(UpdateTransformFlags, Teleport);
    bResetVelocityHistory |= Teleport != ETeleportType::None;
    Publish();
}

void UKasumiSplatComponent::SetProgress(float Progress)
{
    Style.Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
    Publish();
}

void UKasumiSplatComponent::SetEffectTint(FLinearColor Tint)
{
    Style.EffectTint = Tint;
    Publish();
}

void UKasumiSplatComponent::SetEffectMask(FVector Center, float Radius, float Feather)
{
    Style.MaskCenter = Center;
    Style.MaskRadius = FMath::Max(0.0f, Radius);
    Style.MaskFeather = FMath::Max(0.0f, Feather);
    Publish();
}

void UKasumiSplatComponent::ApplyQualityPreset(EKasumiSplatQuality Preset)
{
    QualityPreset = Preset;
    if (Preset != EKasumiSplatQuality::Custom)
    {
        const KasumiSplatConfig::FQualityValues Values = KasumiSplatConfig::GetQualityValues(Preset);
        MaxVisibleSplats = Values.MaxVisibleSplats;
        MinProjectedRadiusPixels = Values.MinProjectedRadiusPixels;
        MaxLODStride = Values.MaxLODStride;
    }
    UpdateStreamingWorkingSet(true);
    Publish();
}

void UKasumiSplatComponent::ApplyAppearancePreset(EKasumiSplatAppearancePreset Preset)
{
    AppearancePreset = Preset;
    if (Preset != EKasumiSplatAppearancePreset::Custom)
    {
        Appearance = KasumiSplatConfig::GetAppearanceValues(Preset);
    }
    Publish();
}

int64 UKasumiSplatComponent::GetApproximateResidentBytes() const
{
    const int64 CpuBytes = StreamingState->SourcePoints.GetAllocatedSize() + StreamingState->SourceHigherOrderSH.GetAllocatedSize();
    const int64 GpuBytes = int64(StreamingState->SourcePoints.Num()) * 4 * sizeof(FVector4f) +
        FMath::DivideAndRoundUp<int64>(StreamingState->SourceHigherOrderSH.Num(), 4) * sizeof(FVector4f);
    return CpuBytes + GpuBytes;
}

bool UKasumiSplatComponent::IsRenderingSupported() const
{
    return GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6;
}

void UKasumiSplatComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const bool bFullQuality = IsFullQualityEnabled(bFullQualityReference);
    const bool bQualityProbe = CVarKasumiSplatQualityProbe.GetValueOnGameThread() != 0;
    if (StreamingState->bLastFullQualityMode != bFullQuality ||
        StreamingState->bLastQualityProbeMode != bQualityProbe)
    {
        ReloadPoints();
    }
    if (Asset && StreamingState->LoadedAssetRevision != Asset->Revision)
    {
        ReloadPoints();
    }
    if (MaterialParameterCollection)
    {
        if (UMaterialParameterCollectionInstance* Instance = GetWorld()->GetParameterCollectionInstance(MaterialParameterCollection))
        {
            float Progress = 0.0f;
            if (!MPCProgressParameter.IsNone() && Instance->GetScalarParameterValue(MPCProgressParameter, Progress))
            {
                Style.Progress = FMath::Clamp(Progress, 0.0f, 1.0f);
            }
            FLinearColor Tint;
            if (!MPCTintParameter.IsNone() && Instance->GetVectorParameterValue(MPCTintParameter, Tint))
            {
                Style.EffectTint = Tint;
            }
        }
    }
    StreamingState->UpdateAccumulator += DeltaTime;
    if (StreamingState->TemporalTransitionAlpha < 1.0f)
    {
        const float Duration = FMath::Max(TemporalTransitionDuration, KINDA_SMALL_NUMBER);
        StreamingState->TemporalTransitionAlpha = FMath::Min(1.0f, StreamingState->TemporalTransitionAlpha + DeltaTime / Duration);
        if (StreamingState->TemporalTransitionAlpha >= 1.0f)
        {
            StreamingState->RenderSnapshot = StreamingState->TargetSnapshot;
            ++StreamingState->ResidentRevision;
        }
    }
    if (StreamingState->UpdateAccumulator >= FMath::Max(0.05f, StreamingUpdateInterval))
    {
        StreamingState->UpdateAccumulator = 0.0;
        UpdateStreamingWorkingSet(false);
    }
    Publish();

    if (bDrawDebugBounds && Asset && Asset->LocalBounds.IsValid)
    {
        const FBox WorldBounds = Asset->LocalBounds.TransformBy(GetComponentTransform());
        DrawDebugBox(GetWorld(), WorldBounds.GetCenter(), WorldBounds.GetExtent(), FColor::Cyan, false, 0.0f, 0, 1.5f);
    }
    if (bDrawDebugChunks && Asset)
    {
        for (const FKasumiSplatChunk& Chunk : Asset->Chunks)
        {
            if (!Chunk.LocalBounds.IsValid) continue;
            const FBox WorldBounds = Chunk.LocalBounds.TransformBy(GetComponentTransform());
            DrawDebugBox(GetWorld(), WorldBounds.GetCenter(), WorldBounds.GetExtent(), FColor::Emerald, false, 0.0f, 0, 0.5f);
        }
    }
}

void UKasumiSplatComponent::ReloadPoints()
{
    const bool bFullQuality = IsFullQualityEnabled(bFullQualityReference);
    const bool bQualityProbe = CVarKasumiSplatQualityProbe.GetValueOnGameThread() != 0;
    const bool bResetTransitionSource = StreamingState->LoadedAsset.Get() != Asset ||
        StreamingState->bLastQualityProbeMode != bQualityProbe;
    StreamingState->bLastFullQualityMode = bFullQuality;
    StreamingState->bLastQualityProbeMode = bQualityProbe;
    if (bResetTransitionSource)
    {
        StreamingState->RenderSnapshot = FKasumiSplatResidentSnapshot();
        StreamingState->TargetSnapshot = FKasumiSplatResidentSnapshot();
        StreamingState->TemporalTransitionAlpha = 1.0f;
    }
    StreamingState->SourcePoints.Reset();
    StreamingState->SourceHigherOrderSH.Reset();
    StreamingState->SelectionHash = 0;
    StreamingState->bRequestPending = false;
    ++StreamingState->RequestSerial;
    StreamingState->LoadedAssetRevision = Asset ? Asset->Revision : INDEX_NONE;
    StreamingState->LoadedAsset = Asset;
    if (bQualityProbe)
    {
        BuildQualityProbePoints(*this, StreamingState->SourcePoints);
        RebuildSharedSourcePoints();
        Publish();
        return;
    }
    if (Asset && bEnableChunkStreaming && !bFullQuality && !Asset->Chunks.IsEmpty() && Asset->GetPointCount() > 0)
    {
        UpdateStreamingWorkingSet(true);
        return;
    }
    if (Asset && Asset->GetPointCount() > 0 && Asset->LoadPoints(StreamingState->SourcePoints))
    {
        Asset->LoadHigherOrderSH(StreamingState->SourceHigherOrderSH);
        RebuildSharedSourcePoints();
        Publish();
        return;
    }

    if (bUseSyntheticFallback)
    {
        ResetSyntheticPoints();
    }
    else
    {
        RebuildSharedSourcePoints();
        Publish();
    }
}

void UKasumiSplatComponent::RebuildSharedSourcePoints()
{
    FKasumiSplatResidentSnapshot NewTarget;
    NewTarget.Points = MakeShared<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe>(StreamingState->SourcePoints);
    NewTarget.HigherOrderSHCoefficientsPerPoint = Asset ? Asset->GetHigherOrderSHCoefficientsPerPoint() : 0;
    if (NewTarget.HigherOrderSHCoefficientsPerPoint > 0 &&
        StreamingState->SourceHigherOrderSH.Num() == StreamingState->SourcePoints.Num() * NewTarget.HigherOrderSHCoefficientsPerPoint)
    {
        NewTarget.HigherOrderSH = MakeShared<const TArray<float>, ESPMode::ThreadSafe>(StreamingState->SourceHigherOrderSH);
    }
    else
    {
        NewTarget.HigherOrderSHCoefficientsPerPoint = 0;
    }

    const bool bCanTransition = !IsFullQualityEnabled(bFullQualityReference) &&
        bTemporalStabilization && TemporalTransitionDuration > 0.0f &&
        StreamingState->TargetSnapshot.Points.IsValid() &&
        !StreamingState->TargetSnapshot.Points->IsEmpty() &&
        StreamingState->TargetSnapshot.HigherOrderSHCoefficientsPerPoint ==
            NewTarget.HigherOrderSHCoefficientsPerPoint &&
        (NewTarget.HigherOrderSHCoefficientsPerPoint == 0 ||
            (StreamingState->TargetSnapshot.HigherOrderSH.IsValid() && NewTarget.HigherOrderSH.IsValid()));
    StreamingState->RenderSnapshot = bCanTransition
        ? BuildKasumiSplatTransitionSnapshot(StreamingState->TargetSnapshot, NewTarget)
        : NewTarget;
    StreamingState->TargetSnapshot = MoveTemp(NewTarget);
    StreamingState->TemporalTransitionAlpha = bCanTransition ? 0.0f : 1.0f;
    ++StreamingState->ResidentRevision;
}

FVector UKasumiSplatComponent::GetStreamingViewLocation() const
{
    const UWorld* World = GetWorld();
    if (World)
    {
        if (const APlayerController* Controller = World->GetFirstPlayerController())
        {
            if (const APlayerCameraManager* Camera = Controller->PlayerCameraManager)
            {
                return Camera->GetCameraLocation();
            }
        }
        if (!World->ViewLocationsRenderedLastFrame.IsEmpty())
        {
            return World->ViewLocationsRenderedLastFrame[0];
        }
    }
    return GetComponentLocation();
}

void UKasumiSplatComponent::UpdateStreamingWorkingSet(bool bForce)
{
    if (!Asset || !bEnableChunkStreaming || IsFullQualityEnabled(bFullQualityReference) || Asset->Chunks.IsEmpty() || StreamingState->bRequestPending)
    {
        return;
    }

    const FVector LocalView = GetComponentTransform().InverseTransformPosition(GetStreamingViewLocation());
    FKasumiSplatStreamingSettings Settings;
    Settings.MemoryBudgetMB = StreamingMemoryBudgetMB;
    Settings.MaxResidentSplats = MaxResidentSplats;
    Settings.MaxDistance = MaxStreamingDistance;
    Settings.LODStartDistance = LODStartDistance;
    Settings.MaxLODStride = MaxLODStride;
    Settings.PreviousLODStride = StreamingState->LastLODStride;
    FKasumiSplatStreamingSelection Selection =
        BuildKasumiSplatStreamingSelection(*Asset, LocalView, Settings);
    StreamingState->LastLODStride = Selection.LODStride;

    if (!bForce && Selection.Hash == StreamingState->SelectionHash)
    {
        return;
    }
    StreamingState->SelectionHash = Selection.Hash;

    if (Selection.ChunkIndices.IsEmpty())
    {
        StreamingState->SourcePoints.Reset();
        StreamingState->SourceHigherOrderSH.Reset();
        RebuildSharedSourcePoints();
        Publish();
        return;
    }

    StreamingState->bRequestPending = true;
    const uint32 RequestSerial = ++StreamingState->RequestSerial;
    const int32 LODStride = Selection.LODStride;
    TWeakObjectPtr<UKasumiSplatComponent> WeakThis(this);
    Asset->LoadPointChunksAsync(Selection.ChunkIndices,
        [WeakThis, RequestSerial, LODStride](
            bool bSuccess,
            TArray<FKasumiSplatPoint>&& LoadedPoints,
            TArray<float>&& LoadedSH,
            int32 SHCoefficientsPerPoint) mutable
    {
        UKasumiSplatComponent* Component = WeakThis.Get();
        if (!Component || Component->StreamingState->RequestSerial != RequestSerial) return;
        if (!bSuccess)
        {
            Component->StreamingState->bRequestPending = false;
            return;
        }

        Async(EAsyncExecution::ThreadPool,
            [WeakThis, RequestSerial, LODStride, SHCoefficientsPerPoint,
                LoadedPoints = MoveTemp(LoadedPoints), LoadedSH = MoveTemp(LoadedSH)]() mutable
        {
            ApplyKasumiSplatLOD(LODStride, SHCoefficientsPerPoint, LoadedPoints, LoadedSH);
            AsyncTask(ENamedThreads::GameThread,
                [WeakThis, RequestSerial,
                    LoadedPoints = MoveTemp(LoadedPoints), LoadedSH = MoveTemp(LoadedSH)]() mutable
            {
                UKasumiSplatComponent* Component = WeakThis.Get();
                if (!Component || Component->StreamingState->RequestSerial != RequestSerial) return;
                Component->StreamingState->bRequestPending = false;
                Component->StreamingState->SourcePoints = MoveTemp(LoadedPoints);
                Component->StreamingState->SourceHigherOrderSH = MoveTemp(LoadedSH);
                Component->RebuildSharedSourcePoints();
                Component->Publish();
            });
        });
    });
}

#if WITH_EDITOR
void UKasumiSplatComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    const FName PropertyName = PropertyChangedEvent.GetMemberPropertyName();
    const bool bRequiresPointReload =
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, Asset) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, bUseSyntheticFallback) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, bEnableChunkStreaming) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, bFullQualityReference) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, MaxResidentSplats) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, StreamingMemoryBudgetMB) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, MaxStreamingDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, LODStartDistance) ||
        PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, MaxLODStride);

    if (bRequiresPointReload)
    {
        ReloadPoints();
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, QualityPreset))
    {
        ApplyQualityPreset(QualityPreset);
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, AppearancePreset))
    {
        ApplyAppearancePreset(AppearancePreset);
    }
    else if (PropertyName == GET_MEMBER_NAME_CHECKED(UKasumiSplatComponent, Appearance))
    {
        AppearancePreset = EKasumiSplatAppearancePreset::Custom;
        Publish();
    }
    else
    {
        Publish();
    }
}
#endif

void UKasumiSplatComponent::Publish()
{
    const UWorld* World = GetWorld();
    if (!IsRegistered() || !World || !World->Scene) return;
    if (!bRenderSplats || !IsVisible())
    {
        KasumiSplat::Remove(GetUniqueID());
        return;
    }
    FKasumiSplatPacket Packet;
    Packet.Scene = World->Scene;
    Packet.LocalToWorld = GetComponentTransform();
    if (Asset && !StreamingState->bLastQualityProbeMode)
    {
        Packet.LocalBounds = Asset->LocalBounds;
    }
    else
    {
        if (StreamingState->RenderSnapshot.Points.IsValid())
        {
            for (const FKasumiSplatPoint& Point : *StreamingState->RenderSnapshot.Points)
            {
                Packet.LocalBounds += Point.Position;
            }
        }
        if (!Packet.LocalBounds.IsValid) Packet.LocalBounds = FBox(FVector(-1), FVector(1));
    }
    if (!StreamingState->RenderSnapshot.Points.IsValid())
    {
        RebuildSharedSourcePoints();
    }
    Packet.Points = StreamingState->RenderSnapshot.Points;
    Packet.HigherOrderSH = StreamingState->RenderSnapshot.HigherOrderSH;
    Packet.TransitionClasses = StreamingState->RenderSnapshot.TransitionClasses;
    Packet.HigherOrderSHCoefficientsPerPoint = uint32(StreamingState->RenderSnapshot.HigherOrderSHCoefficientsPerPoint);
    Packet.TemporalTransitionAlpha = StreamingState->TemporalTransitionAlpha;
    Packet.VelocityMode = VelocityMode;
    Packet.FrameNumber = GFrameCounter;
    Packet.bResetVelocityHistory = bResetVelocityHistory;
    Packet.LocalToSHDirection = Asset ? FMatrix44f(Asset->LocalToSHDirection) : FMatrix44f::Identity;
    Packet.Style = Style;
    Packet.Appearance = Appearance;
    Packet.EffectLayers = EffectLayers;
    Packet.SortMode = SortMode;
    Packet.TileSizePixels = TileSizePixels > 0 ? uint32(TileSizePixels) : 0u;
    Packet.MaxTilesPerSplat = uint32(FMath::Max(MaxTilesPerSplat, 0));
    Packet.TiledPairBudgetMB = uint32(FMath::Max(TiledPairBudgetMB, 16));
    Packet.TiledFallbackMode = TiledFallbackMode;
    if (ProgressCurve)
    {
        Packet.Style.Progress = FMath::Clamp(ProgressCurve->GetFloatValue(Style.Progress), 0.0f, 1.0f);
    }
    const bool bFullQuality = IsFullQualityEnabled(bFullQualityReference);
    Packet.MaxVisibleSplats = bFullQuality && Packet.Points.IsValid()
        ? uint32(Packet.Points->Num())
        : uint32(FMath::Max(MaxVisibleSplats, 0));
    Packet.MinProjectedRadiusPixels = bFullQuality ? 0.0f : MinProjectedRadiusPixels;
    Packet.MaxProjectedRadiusPixels = MaxProjectedRadiusPixels;
    Packet.AntialiasingFilterVariance = AntialiasingFilterVariance;
    Packet.AntialiasingMode = AntialiasingMode;
    Packet.MaxAnisotropy = MaxAnisotropy;
    Packet.MaxSplatSigmaCentimeters = MaxSplatSigmaCentimeters;
    Packet.SceneDepthBias = SceneDepthBias;
    Packet.bUseSceneDepth = bUseSceneDepth;
    Packet.DepthPreCullMaxRadiusPixels = bFullQuality ? 0.0f : DepthPreCullMaxRadiusPixels;
    Packet.ExternalMaskMode = ExternalMaskMode;
    Packet.ExternalMaskCenter = ExternalMaskCenter;
    Packet.ExternalMaskExtent = ExternalMaskExtent;
    Packet.ExternalMaskStrength = ExternalMaskStrength;
    Packet.bInvertExternalMask = bInvertExternalMask;
    if (EffectMaskTexture && EffectMaskTexture->GetResource())
    {
        Packet.EffectMaskTexture = EffectMaskTexture->GetResource()->GetTextureRHI();
    }
    if (EffectMaskVolume && EffectMaskVolume->GetResource())
    {
        Packet.EffectMaskVolume = EffectMaskVolume->GetResource()->GetTextureRHI();
    }
    KasumiSplatConfig::SanitizeRenderPacket(Packet);

    KasumiSplat::Publish(GetUniqueID(), MoveTemp(Packet));
    bResetVelocityHistory = false;
}
