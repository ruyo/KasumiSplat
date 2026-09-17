#include "KasumiSplatRenderer.h"
#include "KasumiSplatRenderResources.h"
#include "KasumiSplatShaders.h"
#include "KasumiSplatSortPolicy.h"
#include "EngineModule.h"
#include "GlobalShader.h"
#include "GPUSort.h"
#include "FXRenderingUtils.h"
#include "PipelineStateCache.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "RenderGraphUtils.h"
#include "RenderResource.h"
#include "RendererInterface.h"
#include "RenderingThread.h"
#include "RHIStaticStates.h"
#include "SceneInterface.h"
#include "SceneView.h"
#include "SceneViewExtension.h"
#include "ScreenPass.h"
#include "SystemTextures.h"

DEFINE_LOG_CATEGORY_STATIC(LogKasumiSplatRenderer, Log, All);

namespace
{
    constexpr uint32 KasumiDelayedFallbackHoldFrames = 60u;

    const TCHAR* GetSortModeName(EKasumiSplatSortMode Mode)
    {
        switch (Mode)
        {
        case EKasumiSplatSortMode::Auto: return TEXT("Auto");
        case EKasumiSplatSortMode::Bucket: return TEXT("Bucket");
        case EKasumiSplatSortMode::GlobalRadix: return TEXT("GlobalRadix");
        case EKasumiSplatSortMode::Tiled: return TEXT("Tiled");
        default: return TEXT("Unknown");
        }
    }

    TAutoConsoleVariable<int32> CVarKasumiSplatMaxVisibleSplats(
        TEXT("r.KasumiSplat.MaxVisibleSplats"),
        -1,
        TEXT("Override the per-component submitted-splat budget. -1 uses the component setting."),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarKasumiSplatFullQuality(
        TEXT("r.KasumiSplat.FullQuality"),
        0,
        TEXT("Render every resident splat and disable projected-radius culling."),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<int32> CVarKasumiSplatAntialiasingMode(
        TEXT("r.KasumiSplat.AntialiasingMode"),
        -1,
        TEXT("Override AA mode: -1 component, 0 disabled, 1 legacy filter, 2 area compensated."),
        ECVF_RenderThreadSafe);

    TAutoConsoleVariable<float> CVarKasumiSplatMaxProjectedRadius(
        TEXT("r.KasumiSplat.MaxProjectedRadiusPixels"),
        -1.0f,
        TEXT("Override the component maximum projected radius. Negative values use the component setting."),
        ECVF_RenderThreadSafe);
}

BEGIN_SHADER_PARAMETER_STRUCT(FKasumiSplatDrawParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FKasumiSplatInstanceVS::FParameters, VS)
    SHADER_PARAMETER_STRUCT_INCLUDE(FKasumiSplatInstancePS::FParameters, PS)
    RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FKasumiSplatVelocityDrawParameters, )
    SHADER_PARAMETER_STRUCT_INCLUDE(FKasumiSplatInstanceVS::FParameters, VS)
    SHADER_PARAMETER_STRUCT_INCLUDE(FKasumiSplatVelocityPS::FParameters, PS)
    RDG_BUFFER_ACCESS(IndirectArgs, ERHIAccess::IndirectArgs)
    RENDER_TARGET_BINDING_SLOTS()
END_SHADER_PARAMETER_STRUCT()

BEGIN_SHADER_PARAMETER_STRUCT(FKasumiSplatSortPassParameters, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, KeysSRV0)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, KeysSRV1)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, KeysUAV0)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, KeysUAV1)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ValuesSRV0)
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ValuesSRV1)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ValuesUAV0)
    SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, ValuesUAV1)
END_SHADER_PARAMETER_STRUCT()

static void AddKasumiRadixSortPass(
    FRDGBuilder& GraphBuilder,
    FRDGBufferRef Keys0,
    FRDGBufferRef Keys1,
    FRDGBufferRef Values0,
    FRDGBufferRef Values1,
    uint32 ElementCount,
    ERHIFeatureLevel::Type FeatureLevel,
    const TCHAR* EventLabel)
{
    FKasumiSplatSortPassParameters* Parameters = GraphBuilder.AllocParameters<FKasumiSplatSortPassParameters>();
    Parameters->KeysSRV0 = GraphBuilder.CreateSRV(Keys0, PF_R32_UINT);
    Parameters->KeysSRV1 = GraphBuilder.CreateSRV(Keys1, PF_R32_UINT);
    Parameters->KeysUAV0 = GraphBuilder.CreateUAV(Keys0, PF_R32_UINT);
    Parameters->KeysUAV1 = GraphBuilder.CreateUAV(Keys1, PF_R32_UINT);
    Parameters->ValuesSRV0 = GraphBuilder.CreateSRV(Values0, PF_R32_UINT);
    Parameters->ValuesSRV1 = GraphBuilder.CreateSRV(Values1, PF_R32_UINT);
    Parameters->ValuesUAV0 = GraphBuilder.CreateUAV(Values0, PF_R32_UINT);
    Parameters->ValuesUAV1 = GraphBuilder.CreateUAV(Values1, PF_R32_UINT);
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("KasumiSplat.%s(%u)", EventLabel, ElementCount),
        Parameters,
        ERDGPassFlags::Compute | ERDGPassFlags::NeverCull,
        [Parameters, ElementCount, FeatureLevel](FRHICommandList& RHICmdList)
        {
            FGPUSortBuffers SortBuffers;
            SortBuffers.RemoteKeySRVs[0] = Parameters->KeysSRV0->GetRHI();
            SortBuffers.RemoteKeySRVs[1] = Parameters->KeysSRV1->GetRHI();
            SortBuffers.RemoteKeyUAVs[0] = Parameters->KeysUAV0->GetRHI();
            SortBuffers.RemoteKeyUAVs[1] = Parameters->KeysUAV1->GetRHI();
            SortBuffers.RemoteValueSRVs[0] = Parameters->ValuesSRV0->GetRHI();
            SortBuffers.RemoteValueSRVs[1] = Parameters->ValuesSRV1->GetRHI();
            SortBuffers.RemoteValueUAVs[0] = Parameters->ValuesUAV0->GetRHI();
            SortBuffers.RemoteValueUAVs[1] = Parameters->ValuesUAV1->GetRHI();
            const int32 ResultBufferIndex = SortGPUBuffers(
                RHICmdList,
                SortBuffers,
                0,
                0xffffffffu,
                int32(ElementCount),
                FeatureLevel);
            check(ResultBufferIndex == 0);
        });
}

static void AddKasumiDrawPass(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatDrawParameters* Parameters,
    TShaderMapRef<FKasumiSplatInstanceVS> VertexShader,
    TShaderMapRef<FKasumiSplatInstancePS> PixelShader,
    const FIntRect& ViewRect,
    const TCHAR* EventLabel)
{
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("KasumiSplat.%s", EventLabel),
        Parameters,
        ERDGPassFlags::Raster,
        [Parameters, VertexShader, PixelShader, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
        {
            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            GraphicsPSOInit.BlendState = TStaticBlendState<
                CW_RGBA,
                BO_Add, BF_One, BF_InverseSourceAlpha,
                BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
            RHICmdList.SetViewport(
                ViewRect.Min.X,
                ViewRect.Min.Y,
                0.0f,
                ViewRect.Max.X,
                ViewRect.Max.Y,
                1.0f);
            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Parameters->VS);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters->PS);
            RHICmdList.SetStreamSource(0, nullptr, 0);
            RHICmdList.DrawPrimitiveIndirect(Parameters->IndirectArgs->GetIndirectRHICallBuffer(), 0);
        });
}

static void AddKasumiVelocityPass(
    FRDGBuilder& GraphBuilder,
    FKasumiSplatVelocityDrawParameters* Parameters,
    TShaderMapRef<FKasumiSplatInstanceVS> VertexShader,
    TShaderMapRef<FKasumiSplatVelocityPS> PixelShader,
    const FIntRect& ViewRect,
    const TCHAR* EventLabel)
{
    GraphBuilder.AddPass(
        RDG_EVENT_NAME("KasumiSplat.%s", EventLabel),
        Parameters,
        ERDGPassFlags::Raster,
        [Parameters, VertexShader, PixelShader, ViewRect](FRDGAsyncTask, FRHICommandList& RHICmdList)
        {
            FGraphicsPipelineStateInitializer GraphicsPSOInit;
            RHICmdList.ApplyCachedRenderTargets(GraphicsPSOInit);
            GraphicsPSOInit.BlendState = TStaticBlendState<CW_RGBA>::GetRHI();
            GraphicsPSOInit.RasterizerState = TStaticRasterizerState<FM_Solid, CM_None>::GetRHI();
            GraphicsPSOInit.DepthStencilState = TStaticDepthStencilState<false, CF_Always>::GetRHI();
            GraphicsPSOInit.BoundShaderState.VertexDeclarationRHI = GEmptyVertexDeclaration.VertexDeclarationRHI;
            GraphicsPSOInit.BoundShaderState.VertexShaderRHI = VertexShader.GetVertexShader();
            GraphicsPSOInit.BoundShaderState.PixelShaderRHI = PixelShader.GetPixelShader();
            GraphicsPSOInit.PrimitiveType = PT_TriangleList;
            SetGraphicsPipelineState(RHICmdList, GraphicsPSOInit, 0);
            RHICmdList.SetViewport(
                ViewRect.Min.X,
                ViewRect.Min.Y,
                0.0f,
                ViewRect.Max.X,
                ViewRect.Max.Y,
                1.0f);
            SetShaderParameters(RHICmdList, VertexShader, VertexShader.GetVertexShader(), Parameters->VS);
            SetShaderParameters(RHICmdList, PixelShader, PixelShader.GetPixelShader(), Parameters->PS);
            RHICmdList.SetStreamSource(0, nullptr, 0);
            RHICmdList.DrawPrimitiveIndirect(Parameters->IndirectArgs->GetIndirectRHICallBuffer(), 0);
        });
}

class FKasumiSplatViewExtension : public FSceneViewExtensionBase
{
public:
    FKasumiSplatViewExtension(const FAutoRegister& AutoRegister)
        : FSceneViewExtensionBase(AutoRegister)
    {
    }

    TMap<uint32, TSharedPtr<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>> Entries;

    virtual void SetupViewFamily(FSceneViewFamily&) override {}
    virtual void SetupView(FSceneViewFamily&, FSceneView&) override {}
    virtual void BeginRenderViewFamily(FSceneViewFamily&) override {}

    virtual void SubscribeToPostProcessingPass(
        EPostProcessingPass Pass,
        const FSceneView& View,
        FPostProcessingPassDelegateArray& Callbacks,
        bool bEnabled) override
    {
        if (Pass == EPostProcessingPass::BeforeDOF && bEnabled && View.GetFeatureLevel() >= ERHIFeatureLevel::SM6)
        {
            Callbacks.Add(FPostProcessingPassDelegate::CreateRaw(this, &FKasumiSplatViewExtension::Render));
        }
    }

    FScreenPassTexture Render(
        FRDGBuilder& GraphBuilder,
        const FSceneView& View,
        const FPostProcessMaterialInputs& Inputs)
    {
        const FScreenPassTexture Input = FScreenPassTexture::CopyFromSlice(
            GraphBuilder,
            Inputs.GetInput(EPostProcessMaterialInput::SceneColor));

        TArray<TSharedPtr<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>, TInlineAllocator<4>> VisibleEntries;
        for (const auto& Pair : Entries)
        {
            const TSharedPtr<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>& Entry = Pair.Value;
            if (Entry.IsValid() &&
                Entry->Packet.Scene == View.Family->Scene &&
                Entry->Packet.Points.IsValid() &&
                !Entry->Packet.Points->IsEmpty() &&
                Entry->Packet.Style.Scale > 0.0f &&
                Entry->Packet.Style.Opacity > 0.0f)
            {
                VisibleEntries.Add(Entry);
            }
        }

        if (VisibleEntries.IsEmpty())
        {
            return Inputs.ReturnUntouchedSceneColorForPostProcessing(GraphBuilder);
        }

        const FVector ViewOrigin = View.ViewMatrices.GetViewOrigin();
        const FVector ViewDirection = View.GetViewDirection();
        VisibleEntries.Sort([&ViewOrigin, &ViewDirection](const auto& A, const auto& B)
        {
            const FBox WorldBoundsA = A->Packet.LocalBounds.TransformBy(A->Packet.LocalToWorld);
            const FBox WorldBoundsB = B->Packet.LocalBounds.TransformBy(B->Packet.LocalToWorld);
            const FVector ExtentA = WorldBoundsA.GetExtent();
            const FVector ExtentB = WorldBoundsB.GetExtent();
            const double FarDepthA = FVector::DotProduct(WorldBoundsA.GetCenter() - ViewOrigin, ViewDirection) +
                FVector::DotProduct(ExtentA, ViewDirection.GetAbs());
            const double FarDepthB = FVector::DotProduct(WorldBoundsB.GetCenter() - ViewOrigin, ViewDirection) +
                FVector::DotProduct(ExtentB, ViewDirection.GetAbs());
            if (!FMath::IsNearlyEqual(FarDepthA, FarDepthB)) return FarDepthA > FarDepthB;
            return A->ComponentId < B->ComponentId;
        });

        FScreenPassRenderTarget Output = Inputs.OverrideOutput;
        if (!Output.IsValid())
        {
            Output = FScreenPassRenderTarget::CreateFromInput(
                GraphBuilder,
                Input,
                View.GetOverwriteLoadAction(),
                TEXT("KasumiSplat.DepthIntegrated"));
        }

        if (Input.Texture != Output.Texture || Input.ViewRect != Output.ViewRect)
        {
            AddDrawTexturePass(GraphBuilder, View, Input, Output);
        }

        const FVector2f ViewSize(Output.ViewRect.Width(), Output.ViewRect.Height());
        // BeforeDOF executes before temporal upscaling in UE 5.8. Use the jittered matrix so splats,
        // scene depth, and the TSR input sample share the same projection.
        const FMatrix44f WorldToClip(View.ViewMatrices.GetWorldToClip());
        const FMatrix44f PreviousWorldToClip(
            View.bCameraCut
                ? View.ViewMatrices.GetWorldToClip()
                : GetRendererModule().GetPreviousViewMatrices(View).GetWorldToClip());
        TShaderMapRef<FKasumiSplatCullCS> CullShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatFinalizeTiledCS> FinalizeTiledShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatPrefixCS> PrefixShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatScatterCS> ScatterShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatInstanceVS> VertexShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatInstancePS> PixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        TShaderMapRef<FKasumiSplatVelocityPS> VelocityPixelShader(GetGlobalShaderMap(View.GetFeatureLevel()));
        const FRDGSystemTextures& SystemTextures = FRDGSystemTextures::IsValid(GraphBuilder)
            ? FRDGSystemTextures::Get(GraphBuilder)
            : FRDGSystemTextures::Create(GraphBuilder);
        FRDGTextureRef VelocityTexture = UE::FXRenderingUtils::GetSceneVelocityTexture(View);
        FRDGTextureRef SceneDepthTexture = Inputs.SceneTextures.SceneTextures
            ? Inputs.SceneTextures.SceneTextures->GetParameters()->SceneDepthTexture
            : SystemTextures.Black;

        for (const TSharedPtr<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>& Entry : VisibleEntries)
        {
            const FKasumiSplatGPUData GPUData = GetOrCreateSplatGPUData(GraphBuilder, *Entry);
            FRDGBufferRef PointBuffer = GPUData.PointBuffer;
            FRDGBufferRef SHBuffer = GPUData.SHBuffer;

            const uint32 SourcePointCount = uint32(Entry->Packet.Points->Num());
            const int32 BudgetOverride = CVarKasumiSplatMaxVisibleSplats.GetValueOnRenderThread();
            const bool bFullQuality = CVarKasumiSplatFullQuality.GetValueOnRenderThread() != 0;
            const uint32 VisibleBudget = bFullQuality
                ? SourcePointCount
                : BudgetOverride > 0
                ? uint32(BudgetOverride)
                : Entry->Packet.MaxVisibleSplats;
            const uint32 PointCount = FMath::Min(SourcePointCount, FMath::Max(VisibleBudget, 1u));
            if (PointCount == 0)
            {
                continue;
            }

            const uint32 TileSize = FMath::Clamp(Entry->Packet.TileSizePixels, 16u, 128u);
            const uint32 TileCountX = FMath::DivideAndRoundUp(uint32(Output.ViewRect.Width()), TileSize);
            const uint32 TileCountY = FMath::DivideAndRoundUp(uint32(Output.ViewRect.Height()), TileSize);
            const uint32 TileCount = FMath::Max(1u, TileCountX * TileCountY);
            const float RadiusOverride = CVarKasumiSplatMaxProjectedRadius.GetValueOnRenderThread();
            const float MaxProjectedRadiusPixels = RadiusOverride >= 1.0f
                ? RadiusOverride
                : Entry->Packet.MaxProjectedRadiusPixels;
            const bool bTiledWasRequested =
                ResolveKasumiSplatSortMode(Entry->Packet.SortMode, PointCount) == EKasumiSplatSortMode::Tiled;
            if (Entry->bTiledOverflowReadbackPending &&
                Entry->TiledOverflowReadback.IsValid() &&
                Entry->TiledOverflowReadback->IsReady())
            {
                const uint32 OverflowCount = *static_cast<const uint32*>(
                    Entry->TiledOverflowReadback->Lock(sizeof(uint32)));
                Entry->TiledOverflowReadback->Unlock();
                Entry->bTiledOverflowReadbackPending = false;
                const bool bRelevantReadback = Entry->bTiledOverflowReadbackRelevant;
                const bool bRecoveryProbe = Entry->bTiledOverflowRecoveryProbePending;
                Entry->bTiledOverflowReadbackRelevant = false;
                Entry->bTiledOverflowRecoveryProbePending = false;

                if (bRelevantReadback &&
                    Entry->Packet.TiledFallbackMode == EKasumiSplatTiledFallbackMode::Delayed)
                {
                    if (OverflowCount > 0u)
                    {
                        Entry->bDelayedTiledFallbackActive = true;
                        Entry->DelayedTiledFallbackFramesRemaining = KasumiDelayedFallbackHoldFrames;
                        Entry->LastDelayedTiledFallbackFrame = Entry->Packet.FrameNumber;
                    }
                    else if (bRecoveryProbe)
                    {
                        Entry->bDelayedTiledFallbackActive = false;
                        Entry->DelayedTiledFallbackFramesRemaining = 0u;
                    }
                }
            }

            const bool bUseDelayedTiledFallback =
                Entry->Packet.TiledFallbackMode == EKasumiSplatTiledFallbackMode::Delayed;
            if (!bUseDelayedTiledFallback)
            {
                Entry->bDelayedTiledFallbackActive = false;
                Entry->DelayedTiledFallbackFramesRemaining = 0u;
                Entry->bTiledOverflowReadbackRelevant = false;
            }
            else if (Entry->bDelayedTiledFallbackActive &&
                !Entry->bTiledOverflowReadbackPending &&
                Entry->DelayedTiledFallbackFramesRemaining > 0u &&
                Entry->LastDelayedTiledFallbackFrame != Entry->Packet.FrameNumber)
            {
                --Entry->DelayedTiledFallbackFramesRemaining;
                Entry->LastDelayedTiledFallbackFrame = Entry->Packet.FrameNumber;
            }

            const EKasumiSplatSortMode PolicyResolvedSortMode =
                ResolveKasumiSplatSortModeForView(
                    Entry->Packet.SortMode,
                    PointCount,
                    TileCount,
                    TileSize,
                    Entry->Packet.MaxTilesPerSplat,
                    MaxProjectedRadiusPixels,
                    Entry->Packet.TiledPairBudgetMB);
            const bool bDelayedRecoveryProbe = bUseDelayedTiledFallback &&
                Entry->bDelayedTiledFallbackActive &&
                !Entry->bTiledOverflowReadbackPending &&
                Entry->DelayedTiledFallbackFramesRemaining == 0u &&
                PolicyResolvedSortMode == EKasumiSplatSortMode::Tiled;
            const EKasumiSplatSortMode ResolvedSortMode =
                bUseDelayedTiledFallback &&
                Entry->bDelayedTiledFallbackActive &&
                !bDelayedRecoveryProbe &&
                PolicyResolvedSortMode == EKasumiSplatSortMode::Tiled
                    ? EKasumiSplatSortMode::GlobalRadix
                    : PolicyResolvedSortMode;
            if (!Entry->bHasResolvedSortMode || Entry->LastResolvedSortMode != ResolvedSortMode)
            {
                UE_LOG(
                    LogKasumiSplatRenderer,
                    Display,
                    TEXT("Component %u resolved sort mode %s (requested %s, submitted %u splats, view %s, rect %dx%d)."),
                    Entry->ComponentId,
                    GetSortModeName(ResolvedSortMode),
                    GetSortModeName(Entry->Packet.SortMode),
                    PointCount,
                    *View.ViewMatrices.GetViewOrigin().ToCompactString(),
                    Output.ViewRect.Width(),
                    Output.ViewRect.Height());
                if (bTiledWasRequested && ResolvedSortMode == EKasumiSplatSortMode::GlobalRadix)
                {
                    UE_LOG(
                        LogKasumiSplatRenderer,
                        Display,
                        TEXT("Tiled fallback reason: %s."),
                        Entry->bDelayedTiledFallbackActive
                            ? TEXT("a delayed GPU overflow fallback is active")
                            : TEXT("the view tile count or Tiled configuration is unsupported"));
                }
                Entry->LastResolvedSortMode = ResolvedSortMode;
                Entry->bHasResolvedSortMode = true;
            }
            const bool bBucketPath = ResolvedSortMode == EKasumiSplatSortMode::Bucket;
            const bool bGlobalPath = ResolvedSortMode == EKasumiSplatSortMode::GlobalRadix;
            const bool bTiledPath = ResolvedSortMode == EKasumiSplatSortMode::Tiled;
            constexpr uint64 BytesPerTiledPair = sizeof(uint32) * 4u;
            const uint64 PairBudgetBytes = uint64(Entry->Packet.TiledPairBudgetMB) * 1024u * 1024u;
            const uint32 MaximumTileOverlap = GetKasumiMaximumTileOverlap(
                TileSize,
                MaxProjectedRadiusPixels);
            const uint32 EffectiveMaxTilesPerSplat = FMath::Clamp(
                Entry->Packet.MaxTilesPerSplat,
                1u,
                256u);
            const uint32 BoundedTileOverlap = FMath::Min(
                MaximumTileOverlap,
                EffectiveMaxTilesPerSplat);
            const uint64 MaximumConfiguredPairs = uint64(PointCount) *
                uint64(BoundedTileOverlap);
            const uint32 TileEntryCount = bTiledPath
                ? uint32(FMath::Clamp<uint64>(
                    FMath::Min(MaximumConfiguredPairs, PairBudgetBytes / BytesPerTiledPair),
                    1u,
                    uint64(MAX_int32)))
                : 1u;
            const bool bTiledFallbackPossible = bTiledPath &&
                (MaximumTileOverlap > EffectiveMaxTilesPerSplat ||
                    MaximumConfiguredPairs > uint64(TileEntryCount));
            const bool bSameFrameTiledFallback = bTiledFallbackPossible &&
                (!bUseDelayedTiledFallback || bDelayedRecoveryProbe);
            const bool bReadBackDelayedTiledOverflow = bTiledFallbackPossible &&
                bUseDelayedTiledFallback;
            if (bDelayedRecoveryProbe && !bTiledFallbackPossible)
            {
                Entry->bDelayedTiledFallbackActive = false;
                Entry->DelayedTiledFallbackFramesRemaining = 0u;
            }
            const uint32 TileBits = FMath::Clamp(FMath::CeilLogTwo(FMath::Max(TileCount, 2u)), 1u, 16u);
            const uint32 TileDepthBits = 32u - TileBits;
            const FUintVector4 TileConfig(TileSize, TileCountX, TileCountY, TileDepthBits);
            const uint32 BucketPointBufferCount = bBucketPath ? PointCount : 1u;
            const uint32 BucketTableCount = bBucketPath ? KasumiDepthBucketCount : 1u;
            const uint32 ExactKeyBufferCount = (bGlobalPath || bSameFrameTiledFallback) ? PointCount : 1u;
            const uint32 ExactValueBufferCount = (bBucketPath || bGlobalPath || bSameFrameTiledFallback)
                ? PointCount
                : 1u;

            FRDGBufferRef VisibilityBuckets = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BucketPointBufferCount),
                TEXT("KasumiSplat.VisibilityBuckets"));
            FRDGBufferRef BucketCounts = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BucketTableCount),
                TEXT("KasumiSplat.BucketCounts"));
            FRDGBufferRef BucketOffsets = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BucketTableCount),
                TEXT("KasumiSplat.BucketOffsets"));
            FRDGBufferRef BucketWriteOffsets = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BucketTableCount),
                TEXT("KasumiSplat.BucketWriteOffsets"));
            FRDGBufferRef SortedIndices = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), BucketPointBufferCount),
                TEXT("KasumiSplat.SortedIndices"));
            FRDGBufferRef ExactKeys0 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ExactKeyBufferCount),
                TEXT("KasumiSplat.ExactKeys0"));
            FRDGBufferRef ExactKeys1 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ExactKeyBufferCount),
                TEXT("KasumiSplat.ExactKeys1"));
            FRDGBufferRef ExactValues0 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ExactValueBufferCount),
                TEXT("KasumiSplat.ExactValues0"));
            FRDGBufferRef ExactValues1 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), ExactValueBufferCount),
                TEXT("KasumiSplat.ExactValues1"));
            FRDGBufferRef DrawIndirectArgs = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateIndirectDesc(4),
                TEXT("KasumiSplat.DrawIndirectArgs"));
            FRDGBufferRef TileDrawIndirectArgs = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateIndirectDesc(4),
                TEXT("KasumiSplat.TileDrawIndirectArgs"));
            FRDGBufferRef TileOverflow = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), 1),
                TEXT("KasumiSplat.TileOverflow"));
            FRDGBufferRef TileKeys0 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileEntryCount),
                TEXT("KasumiSplat.TileKeys0"));
            FRDGBufferRef TileKeys1 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileEntryCount),
                TEXT("KasumiSplat.TileKeys1"));
            FRDGBufferRef TileValues0 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileEntryCount),
                TEXT("KasumiSplat.TileValues0"));
            FRDGBufferRef TileValues1 = GraphBuilder.CreateBuffer(
                FRDGBufferDesc::CreateBufferDesc(sizeof(uint32), TileEntryCount),
                TEXT("KasumiSplat.TileValues1"));

            if (bBucketPath)
            {
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(BucketCounts, PF_R32_UINT), 0u);
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(SortedIndices, PF_R32_UINT), 0u);
            }
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(DrawIndirectArgs, PF_R32_UINT), 0u);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileDrawIndirectArgs, PF_R32_UINT), 0u);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileOverflow, PF_R32_UINT), 0u);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileKeys0, PF_R32_UINT), 0xffffffffu);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileKeys1, PF_R32_UINT), 0xffffffffu);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileValues0, PF_R32_UINT), 0xffffffffu);
            AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(TileValues1, PF_R32_UINT), 0xffffffffu);
            if (bGlobalPath || bSameFrameTiledFallback)
            {
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ExactKeys1, PF_R32_UINT), 0xffffffffu);
                AddClearUAVPass(GraphBuilder, GraphBuilder.CreateUAV(ExactValues1, PF_R32_UINT), 0xffffffffu);
            }

            const FMatrix44f LocalToWorld(Entry->Packet.LocalToWorld.ToMatrixWithScale());
            const FVector4f StyleValues(
                FMath::Clamp(Entry->Packet.Style.Scale, 0.0f, 10.0f),
                FMath::Clamp(Entry->Packet.Style.Opacity, 0.0f, 1.0f),
                FMath::Clamp(Entry->Packet.Style.Progress, 0.0f, 1.0f),
                FMath::Clamp(Entry->Packet.Style.Displacement, 0.0f, 1000.0f));
            const FVector4f AppearanceValues(
                Entry->Packet.Appearance.ExposureEV,
                Entry->Packet.Appearance.Saturation,
                Entry->Packet.Appearance.Contrast,
                Entry->Packet.Appearance.SHStrength);
            const float OpacityDensity = Entry->Packet.Appearance.OpacityDensity;
            const FVector4f EffectTint(
                Entry->Packet.Style.EffectTint.R,
                Entry->Packet.Style.EffectTint.G,
                Entry->Packet.Style.EffectTint.B,
                Entry->Packet.Style.EffectTint.A);
            const FVector4f EffectValues(
                FMath::Clamp(Entry->Packet.Style.TargetScale, 0.0f, 10.0f),
                FMath::Clamp(Entry->Packet.Style.TargetOpacity, 0.0f, 1.0f),
                Entry->Packet.Style.RotationDegrees,
                FMath::Clamp(Entry->Packet.Style.NoiseAmount, 0.0f, 1.0f));
            const FVector4f MaskCenterRadius(
                FVector3f(Entry->Packet.Style.MaskCenter),
                FMath::Max(0.0f, Entry->Packet.Style.MaskRadius));
            const FVector2f MaskValues(FMath::Max(0.0f, Entry->Packet.Style.MaskFeather), 0.0f);
            FVector4f LayerTypeWeight[KasumiMaxEffectLayers] = {FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero()};
            FVector4f LayerMaskCenterFeather[KasumiMaxEffectLayers] = {FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero()};
            FVector4f LayerMaskExtentEnabled[KasumiMaxEffectLayers] = {FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero()};
            FVector4f LayerTint[KasumiMaxEffectLayers] = {FVector4f(1), FVector4f(1), FVector4f(1), FVector4f(1)};
            FVector4f LayerVectorSeed[KasumiMaxEffectLayers] = {FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero(), FVector4f::Zero()};
            const uint32 LayerCount = FMath::Min<uint32>(Entry->Packet.EffectLayers.Num(), KasumiMaxEffectLayers);
            for (uint32 LayerIndex = 0; LayerIndex < LayerCount; ++LayerIndex)
            {
                const FKasumiSplatEffectLayer& Layer = Entry->Packet.EffectLayers[LayerIndex];
                LayerTypeWeight[LayerIndex] = FVector4f(
                    float(Layer.Type), FMath::Clamp(Layer.Weight, 0.0f, 1.0f), float(Layer.MaskShape), Layer.Scalar);
                LayerMaskCenterFeather[LayerIndex] = FVector4f(FVector3f(Layer.MaskCenter), FMath::Max(0.0f, Layer.Feather));
                LayerMaskExtentEnabled[LayerIndex] = FVector4f(FVector3f(Layer.MaskExtent.GetAbs()), Layer.bEnabled ? 1.0f : 0.0f);
                LayerTint[LayerIndex] = FVector4f(Layer.Tint.R, Layer.Tint.G, Layer.Tint.B, Layer.Tint.A);
                LayerVectorSeed[LayerIndex] = FVector4f(FVector3f(Layer.Vector), float(Layer.Seed));
            }
            uint32 ExternalMaskMode = uint32(Entry->Packet.ExternalMaskMode);
            if ((ExternalMaskMode == uint32(EKasumiSplatExternalMask::Texture2D) && !Entry->Packet.EffectMaskTexture) ||
                (ExternalMaskMode == uint32(EKasumiSplatExternalMask::VolumeTexture) && !Entry->Packet.EffectMaskVolume))
            {
                ExternalMaskMode = 0;
            }
            const FVector4f ExternalMaskCenterMode(FVector3f(Entry->Packet.ExternalMaskCenter), float(ExternalMaskMode));
            const FVector4f ExternalMaskExtentValues(
                FVector3f(Entry->Packet.ExternalMaskExtent),
                Entry->Packet.bInvertExternalMask ? -Entry->Packet.ExternalMaskStrength : Entry->Packet.ExternalMaskStrength);
            FRDGTextureRef MaskTexture2D = Entry->Packet.EffectMaskTexture
                ? GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Entry->Packet.EffectMaskTexture, TEXT("KasumiSplat.ExternalMask2D")))
                : SystemTextures.White;
            FRDGTextureRef MaskTexture3D = Entry->Packet.EffectMaskVolume
                ? GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Entry->Packet.EffectMaskVolume, TEXT("KasumiSplat.ExternalMask3D")))
                : SystemTextures.VolumetricBlack;
            FSamplerStateRHIRef MaskSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();

            FKasumiSplatCullCS::FParameters* CullParameters = GraphBuilder.AllocParameters<FKasumiSplatCullCS::FParameters>();
            CullParameters->PointData = GraphBuilder.CreateSRV(PointBuffer);
            CullParameters->LocalToWorld = LocalToWorld;
            CullParameters->WorldToClip = WorldToClip;
            CullParameters->ViewSize = ViewSize;
            CullParameters->Tint = FVector4f(
                Entry->Packet.Style.Tint.R,
                Entry->Packet.Style.Tint.G,
                Entry->Packet.Style.Tint.B,
                Entry->Packet.Style.Tint.A);
            CullParameters->StyleValues = StyleValues;
            CullParameters->AppearanceValues = AppearanceValues;
            CullParameters->OpacityDensity = OpacityDensity;
            CullParameters->EffectTint = EffectTint;
            CullParameters->EffectValues = EffectValues;
            CullParameters->MaskCenterRadius = MaskCenterRadius;
            CullParameters->MaskValues = MaskValues;
            CullParameters->ProjectedRadiusRange = FVector2f(
                bFullQuality ? 0.0f : Entry->Packet.MinProjectedRadiusPixels,
                MaxProjectedRadiusPixels);
            CullParameters->AntialiasingFilterVariance = Entry->Packet.AntialiasingFilterVariance;
            const int32 AntialiasingOverride = CVarKasumiSplatAntialiasingMode.GetValueOnRenderThread();
            CullParameters->AntialiasingMode = AntialiasingOverride >= 0
                ? uint32(FMath::Clamp(AntialiasingOverride, 0, 2))
                : uint32(Entry->Packet.AntialiasingMode);
            CullParameters->TemporalTransitionAlpha = Entry->Packet.TemporalTransitionAlpha;
            CullParameters->ShapeLimits = FVector2f(
                Entry->Packet.MaxAnisotropy,
                Entry->Packet.MaxSplatSigmaCentimeters);
            CullParameters->SceneTextures = Inputs.SceneTextures;
            CullParameters->ViewRectMin = FVector2f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y);
            CullParameters->SceneDepthBias = Entry->Packet.SceneDepthBias;
            CullParameters->DepthPreCullMaxRadiusPixels = bFullQuality ? 0.0f : Entry->Packet.DepthPreCullMaxRadiusPixels;
            CullParameters->UseSceneDepth = Entry->Packet.bUseSceneDepth ? 1u : 0u;
            CullParameters->Seed = uint32(Entry->Packet.Style.Seed);
            CullParameters->PointCount = PointCount;
            CullParameters->SourcePointCount = SourcePointCount;
            CullParameters->UseLogDepthSort = View.IsPerspectiveProjection() ? 1u : 0u;
            CullParameters->SortPath = bBucketPath ? 0u : (bGlobalPath ? 1u : 2u);
            CullParameters->EnableTiledFallback = bSameFrameTiledFallback ? 1u : 0u;
            CullParameters->MaxTilesPerSplat = EffectiveMaxTilesPerSplat;
            CullParameters->TilePairCapacity = TileEntryCount;
            CullParameters->TileDepthBits = TileDepthBits;
            CullParameters->TileConfig = TileConfig;
            CullParameters->LayerCount = LayerCount;
            FMemory::Memcpy(CullParameters->LayerTypeWeight.GetData(), LayerTypeWeight, sizeof(LayerTypeWeight));
            FMemory::Memcpy(CullParameters->LayerMaskCenterFeather.GetData(), LayerMaskCenterFeather, sizeof(LayerMaskCenterFeather));
            FMemory::Memcpy(CullParameters->LayerMaskExtentEnabled.GetData(), LayerMaskExtentEnabled, sizeof(LayerMaskExtentEnabled));
            FMemory::Memcpy(CullParameters->LayerTint.GetData(), LayerTint, sizeof(LayerTint));
            FMemory::Memcpy(CullParameters->LayerVectorSeed.GetData(), LayerVectorSeed, sizeof(LayerVectorSeed));
            CullParameters->ExternalMaskTexture2D = MaskTexture2D;
            CullParameters->ExternalMaskTexture3D = MaskTexture3D;
            CullParameters->ExternalMaskSampler = MaskSampler;
            CullParameters->ExternalMaskCenterMode = ExternalMaskCenterMode;
            CullParameters->ExternalMaskExtentValues = ExternalMaskExtentValues;
            CullParameters->RWVisibilityBuckets = GraphBuilder.CreateUAV(VisibilityBuckets, PF_R32_UINT);
            CullParameters->RWBucketCounts = GraphBuilder.CreateUAV(BucketCounts, PF_R32_UINT);
            CullParameters->RWExactKeys = GraphBuilder.CreateUAV(ExactKeys0, PF_R32_UINT);
            CullParameters->RWExactValues = GraphBuilder.CreateUAV(ExactValues0, PF_R32_UINT);
            CullParameters->RWDrawIndirectArgs = GraphBuilder.CreateUAV(DrawIndirectArgs, PF_R32_UINT);
            CullParameters->RWTileDrawIndirectArgs = GraphBuilder.CreateUAV(TileDrawIndirectArgs, PF_R32_UINT);
            CullParameters->RWTileOverflow = GraphBuilder.CreateUAV(TileOverflow, PF_R32_UINT);
            CullParameters->RWTileKeys = GraphBuilder.CreateUAV(TileKeys0, PF_R32_UINT);
            CullParameters->RWTileValues = GraphBuilder.CreateUAV(TileValues0, PF_R32_UINT);
            FComputeShaderUtils::AddPass(
                GraphBuilder,
                RDG_EVENT_NAME("KasumiSplat.Cull(%u)", PointCount),
                CullShader,
                CullParameters,
                FComputeShaderUtils::GetGroupCount(PointCount, KasumiCullGroupSize));

            if (ResolvedSortMode == EKasumiSplatSortMode::Bucket)
            {
                FKasumiSplatPrefixCS::FParameters* PrefixParameters = GraphBuilder.AllocParameters<FKasumiSplatPrefixCS::FParameters>();
                PrefixParameters->BucketCounts = GraphBuilder.CreateSRV(BucketCounts, PF_R32_UINT);
                PrefixParameters->RWBucketOffsets = GraphBuilder.CreateUAV(BucketOffsets, PF_R32_UINT);
                PrefixParameters->RWBucketWriteOffsets = GraphBuilder.CreateUAV(BucketWriteOffsets, PF_R32_UINT);
                PrefixParameters->RWDrawIndirectArgs = GraphBuilder.CreateUAV(DrawIndirectArgs, PF_R32_UINT);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("KasumiSplat.Prefix"),
                    PrefixShader,
                    PrefixParameters,
                    FIntVector(1, 1, 1));

                FKasumiSplatScatterCS::FParameters* ScatterParameters = GraphBuilder.AllocParameters<FKasumiSplatScatterCS::FParameters>();
                ScatterParameters->VisibilityBuckets = GraphBuilder.CreateSRV(VisibilityBuckets, PF_R32_UINT);
                ScatterParameters->ExactValues = GraphBuilder.CreateSRV(ExactValues0, PF_R32_UINT);
                ScatterParameters->PointCount = PointCount;
                ScatterParameters->RWBucketWriteOffsets = GraphBuilder.CreateUAV(BucketWriteOffsets, PF_R32_UINT);
                ScatterParameters->RWSortedIndices = GraphBuilder.CreateUAV(SortedIndices, PF_R32_UINT);
                FComputeShaderUtils::AddPass(
                    GraphBuilder,
                    RDG_EVENT_NAME("KasumiSplat.LinearBucketScatter"),
                    ScatterShader,
                    ScatterParameters,
                    FComputeShaderUtils::GetGroupCount(PointCount, KasumiCullGroupSize));
            }
            else if (ResolvedSortMode == EKasumiSplatSortMode::GlobalRadix)
            {
                AddKasumiRadixSortPass(
                    GraphBuilder,
                    ExactKeys0,
                    ExactKeys1,
                    ExactValues0,
                    ExactValues1,
                    PointCount,
                    View.GetFeatureLevel(),
                    TEXT("Global"));
            }
            else
            {
                if (bSameFrameTiledFallback)
                {
                    // Keep a global ordering ready for the same frame. FinalizeTiledCS selects it
                    // only when the generated tile-pair list exceeds the configured limits.
                    AddKasumiRadixSortPass(
                        GraphBuilder,
                        ExactKeys0,
                        ExactKeys1,
                        ExactValues0,
                        ExactValues1,
                        PointCount,
                        View.GetFeatureLevel(),
                        TEXT("TiledFallbackGlobal"));
                }
                AddKasumiRadixSortPass(
                    GraphBuilder,
                    TileKeys0,
                    TileKeys1,
                    TileValues0,
                    TileValues1,
                    TileEntryCount,
                    View.GetFeatureLevel(),
                    TEXT("TiledPairs"));

                if (bTiledFallbackPossible)
                {
                    FKasumiSplatFinalizeTiledCS::FParameters* FinalizeParameters =
                        GraphBuilder.AllocParameters<FKasumiSplatFinalizeTiledCS::FParameters>();
                    FinalizeParameters->TilePairCapacity = TileEntryCount;
                    FinalizeParameters->RWDrawIndirectArgs = GraphBuilder.CreateUAV(DrawIndirectArgs, PF_R32_UINT);
                    FinalizeParameters->RWTileDrawIndirectArgs = GraphBuilder.CreateUAV(TileDrawIndirectArgs, PF_R32_UINT);
                    FinalizeParameters->RWTileOverflow = GraphBuilder.CreateUAV(TileOverflow, PF_R32_UINT);
                    FComputeShaderUtils::AddPass(
                        GraphBuilder,
                        RDG_EVENT_NAME("KasumiSplat.FinalizeTiled"),
                        FinalizeTiledShader,
                        FinalizeParameters,
                        FIntVector(1, 1, 1));
                }

                if (bReadBackDelayedTiledOverflow && !Entry->bTiledOverflowReadbackPending)
                {
                    if (!Entry->TiledOverflowReadback.IsValid())
                    {
                        Entry->TiledOverflowReadback = MakeUnique<FRHIGPUBufferReadback>(
                            TEXT("KasumiSplat.TiledOverflow"));
                    }
                    AddEnqueueCopyPass(
                        GraphBuilder,
                        Entry->TiledOverflowReadback.Get(),
                        TileOverflow,
                        sizeof(uint32));
                    Entry->bTiledOverflowReadbackPending = true;
                    Entry->bTiledOverflowReadbackRelevant = true;
                    Entry->bTiledOverflowRecoveryProbePending = bDelayedRecoveryProbe;
                }
            }

            FKasumiSplatDrawParameters* Parameters = GraphBuilder.AllocParameters<FKasumiSplatDrawParameters>();
            Parameters->VS.PointData = GraphBuilder.CreateSRV(PointBuffer);
            Parameters->VS.SHData = GraphBuilder.CreateSRV(SHBuffer);
            Parameters->VS.View = View.ViewUniformBuffer;
            Parameters->VS.VisibleIndices = GraphBuilder.CreateSRV(
                ResolvedSortMode == EKasumiSplatSortMode::GlobalRadix
                    ? ExactValues0
                    : (ResolvedSortMode == EKasumiSplatSortMode::Tiled ? TileValues0 : SortedIndices),
                PF_R32_UINT);
            Parameters->VS.TileKeys = GraphBuilder.CreateSRV(TileKeys0, PF_R32_UINT);
            Parameters->VS.LocalToWorld = LocalToWorld;
            Parameters->VS.WorldToClip = WorldToClip;
            const bool bResetVelocity = View.bCameraCut ||
                Entry->Packet.bResetVelocityHistory ||
                !Entry->bVelocityHistoryValid;
            Parameters->VS.PreviousLocalToWorld = FMatrix44f(
                (bResetVelocity ? Entry->Packet.LocalToWorld : Entry->PreviousLocalToWorld).ToMatrixWithScale());
            Parameters->VS.PreviousWorldToClip = bResetVelocity ? WorldToClip : PreviousWorldToClip;
            Parameters->VS.LocalToSHDirection = Entry->Packet.LocalToSHDirection;
            Parameters->VS.ViewSize = ViewSize;
            Parameters->VS.Tint = FVector4f(
                Entry->Packet.Style.Tint.R,
                Entry->Packet.Style.Tint.G,
                Entry->Packet.Style.Tint.B,
                Entry->Packet.Style.Tint.A);
            Parameters->VS.StyleValues = StyleValues;
            Parameters->VS.AppearanceValues = AppearanceValues;
            Parameters->VS.OpacityDensity = OpacityDensity;
            Parameters->VS.EffectTint = EffectTint;
            Parameters->VS.EffectValues = EffectValues;
            Parameters->VS.MaskCenterRadius = MaskCenterRadius;
            Parameters->VS.MaskValues = MaskValues;
            Parameters->VS.ViewLocalPosition = FVector3f(Entry->Packet.LocalToWorld.InverseTransformPosition(View.ViewMatrices.GetViewOrigin()));
            Parameters->VS.MaxProjectedRadiusPixels = MaxProjectedRadiusPixels;
            Parameters->VS.AntialiasingFilterVariance = Entry->Packet.AntialiasingFilterVariance;
            Parameters->VS.AntialiasingMode = AntialiasingOverride >= 0
                ? uint32(FMath::Clamp(AntialiasingOverride, 0, 2))
                : uint32(Entry->Packet.AntialiasingMode);
            Parameters->VS.TemporalTransitionAlpha = Entry->Packet.TemporalTransitionAlpha;
            Parameters->VS.ShapeLimits = FVector2f(
                Entry->Packet.MaxAnisotropy,
                Entry->Packet.MaxSplatSigmaCentimeters);
            Parameters->VS.TileConfig = TileConfig;
            Parameters->VS.UseTilePairs = ResolvedSortMode == EKasumiSplatSortMode::Tiled ? 1u : 0u;
            Parameters->VS.TileDepthBits = TileDepthBits;
            Parameters->VS.Seed = uint32(Entry->Packet.Style.Seed);
            Parameters->VS.HigherOrderSHCoefficientsPerPoint = Entry->Packet.HigherOrderSHCoefficientsPerPoint;
            Parameters->VS.LayerCount = LayerCount;
            FMemory::Memcpy(Parameters->VS.LayerTypeWeight.GetData(), LayerTypeWeight, sizeof(LayerTypeWeight));
            FMemory::Memcpy(Parameters->VS.LayerMaskCenterFeather.GetData(), LayerMaskCenterFeather, sizeof(LayerMaskCenterFeather));
            FMemory::Memcpy(Parameters->VS.LayerMaskExtentEnabled.GetData(), LayerMaskExtentEnabled, sizeof(LayerMaskExtentEnabled));
            FMemory::Memcpy(Parameters->VS.LayerTint.GetData(), LayerTint, sizeof(LayerTint));
            FMemory::Memcpy(Parameters->VS.LayerVectorSeed.GetData(), LayerVectorSeed, sizeof(LayerVectorSeed));
            Parameters->VS.ExternalMaskTexture2D = MaskTexture2D;
            Parameters->VS.ExternalMaskTexture3D = MaskTexture3D;
            Parameters->VS.ExternalMaskSampler = MaskSampler;
            Parameters->VS.ExternalMaskCenterMode = ExternalMaskCenterMode;
            Parameters->VS.ExternalMaskExtentValues = ExternalMaskExtentValues;
            Parameters->PS.SceneTextures = Inputs.SceneTextures;
            Parameters->PS.SceneDepthBias = Entry->Packet.SceneDepthBias;
            Parameters->PS.UseSceneDepth = Entry->Packet.bUseSceneDepth ? 1u : 0u;
            Parameters->PS.ViewRectMin = FVector2f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y);
            Parameters->PS.TileConfig = TileConfig;
            Parameters->PS.UseTilePairs = ResolvedSortMode == EKasumiSplatSortMode::Tiled ? 1u : 0u;
            Parameters->IndirectArgs = bTiledPath ? TileDrawIndirectArgs : DrawIndirectArgs;
            Parameters->RenderTargets[0] = FRenderTargetBinding(Output.Texture, ERenderTargetLoadAction::ELoad);

            const FIntRect ViewRect = Output.ViewRect;
            AddKasumiDrawPass(
                GraphBuilder,
                Parameters,
                VertexShader,
                PixelShader,
                ViewRect,
                TEXT("DepthSorted"));

            if (bSameFrameTiledFallback)
            {
                FKasumiSplatDrawParameters* FallbackParameters =
                    GraphBuilder.AllocParameters<FKasumiSplatDrawParameters>();
                *FallbackParameters = *Parameters;
                FallbackParameters->VS.VisibleIndices = GraphBuilder.CreateSRV(ExactValues0, PF_R32_UINT);
                FallbackParameters->VS.UseTilePairs = 0u;
                FallbackParameters->PS.UseTilePairs = 0u;
                FallbackParameters->IndirectArgs = DrawIndirectArgs;

                AddKasumiDrawPass(
                    GraphBuilder,
                    FallbackParameters,
                    VertexShader,
                    PixelShader,
                    ViewRect,
                    TEXT("TiledOverflowFallback"));
            }

            if (Entry->Packet.VelocityMode == EKasumiSplatVelocityMode::ActorAndCamera && VelocityTexture)
            {
                FKasumiSplatVelocityDrawParameters* VelocityParameters =
                    GraphBuilder.AllocParameters<FKasumiSplatVelocityDrawParameters>();
                VelocityParameters->VS = Parameters->VS;
                VelocityParameters->PS.View = View.ViewUniformBuffer;
                VelocityParameters->PS.VelocitySceneDepthTexture = SceneDepthTexture;
                VelocityParameters->PS.SceneDepthBias = Entry->Packet.SceneDepthBias;
                VelocityParameters->PS.UseSceneDepth = Entry->Packet.bUseSceneDepth ? 1u : 0u;
                VelocityParameters->PS.ViewRectMin = FVector2f(Output.ViewRect.Min.X, Output.ViewRect.Min.Y);
                VelocityParameters->PS.TileConfig = TileConfig;
                VelocityParameters->PS.UseTilePairs = ResolvedSortMode == EKasumiSplatSortMode::Tiled ? 1u : 0u;
                VelocityParameters->IndirectArgs = bTiledPath ? TileDrawIndirectArgs : DrawIndirectArgs;
                VelocityParameters->RenderTargets[0] = FRenderTargetBinding(
                    VelocityTexture,
                    ERenderTargetLoadAction::ELoad);

                AddKasumiVelocityPass(
                    GraphBuilder,
                    VelocityParameters,
                    VertexShader,
                    VelocityPixelShader,
                    ViewRect,
                    TEXT("Velocity"));

                if (bSameFrameTiledFallback)
                {
                    FKasumiSplatVelocityDrawParameters* VelocityFallbackParameters =
                        GraphBuilder.AllocParameters<FKasumiSplatVelocityDrawParameters>();
                    *VelocityFallbackParameters = *VelocityParameters;
                    VelocityFallbackParameters->VS.VisibleIndices = GraphBuilder.CreateSRV(ExactValues0, PF_R32_UINT);
                    VelocityFallbackParameters->VS.UseTilePairs = 0u;
                    VelocityFallbackParameters->PS.UseTilePairs = 0u;
                    VelocityFallbackParameters->IndirectArgs = DrawIndirectArgs;

                    AddKasumiVelocityPass(
                        GraphBuilder,
                        VelocityFallbackParameters,
                        VertexShader,
                        VelocityPixelShader,
                        ViewRect,
                        TEXT("VelocityTiledOverflowFallback"));
                }
            }
        }

        return Output;
    }
};

static TSharedPtr<FKasumiSplatViewExtension, ESPMode::ThreadSafe> KasumiExtension;

namespace KasumiSplat
{
    FVector EvaluatePosition(const FKasumiSplatPoint& Point, const FKasumiSplatStyle& Style)
    {
        FRandomStream Random(int32(uint32(Point.StableId) * 196613u + uint32(Style.Seed)));
        const FVector Direction = Random.VRand();
        return Point.Position + Direction *
            FMath::Clamp(Style.Displacement, 0.0f, 1000.0f) *
            FMath::Clamp(Style.Progress, 0.0f, 1.0f);
    }

    void StartRenderer()
    {
        if (!IsRunningCommandlet())
        {
            if (GMaxRHIFeatureLevel >= ERHIFeatureLevel::SM6)
            {
                KasumiExtension = FSceneViewExtensions::NewExtension<FKasumiSplatViewExtension>();
            }
            else
            {
                UE_LOG(LogKasumiSplatRenderer, Warning,
                    TEXT("KasumiSplat rendering is disabled because this RHI does not provide Shader Model 6."));
            }
        }
    }

    void StopRenderer()
    {
        KasumiExtension.Reset();
        FlushRenderingCommands();
    }

    void Publish(uint32 Id, FKasumiSplatPacket Packet)
    {
        if (!KasumiExtension.IsValid())
        {
            return;
        }

        ENQUEUE_RENDER_COMMAND(KasumiPublish)(
            [Extension = KasumiExtension, Id, Packet = MoveTemp(Packet)](FRHICommandListImmediate&) mutable
            {
                TSharedPtr<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>& Entry = Extension->Entries.FindOrAdd(Id);
                if (!Entry.IsValid())
                {
                    Entry = MakeShared<FKasumiSplatRenderEntry, ESPMode::ThreadSafe>();
                    Entry->ComponentId = Id;
                }
                if (Entry->Packet.Points.Get() != Packet.Points.Get())
                {
                    Entry->PointBuffer.SafeRelease();
                    Entry->SHBuffer.SafeRelease();
                    Entry->bTiledOverflowReadbackRelevant = false;
                    Entry->bTiledOverflowRecoveryProbePending = false;
                    Entry->bDelayedTiledFallbackActive = false;
                    Entry->DelayedTiledFallbackFramesRemaining = 0u;
                }
                const bool bNewFrame = Entry->LastPublishFrame != Packet.FrameNumber;
                if (!Entry->bVelocityHistoryValid || Packet.bResetVelocityHistory)
                {
                    Entry->PreviousLocalToWorld = Packet.LocalToWorld;
                    Entry->bVelocityHistoryValid = true;
                }
                else if (bNewFrame)
                {
                    Entry->PreviousLocalToWorld = Entry->Packet.LocalToWorld;
                }
                Entry->LastPublishFrame = Packet.FrameNumber;
                Entry->Packet = MoveTemp(Packet);
            });
    }

    void Remove(uint32 Id)
    {
        if (!KasumiExtension.IsValid())
        {
            return;
        }

        ENQUEUE_RENDER_COMMAND(KasumiRemove)(
            [Extension = KasumiExtension, Id](FRHICommandListImmediate&)
            {
                Extension->Entries.Remove(Id);
            });
    }
}
