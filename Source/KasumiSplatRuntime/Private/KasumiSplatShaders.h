#pragma once

#include "KasumiSplatConfiguration.h"
#include "GlobalShader.h"
#include "PostProcess/PostProcessMaterialInputs.h"
#include "SceneView.h"

inline constexpr uint32 KasumiMaxEffectLayers = KasumiSplatConfig::MaxEffectLayers;

class FKasumiSplatCullCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatCullCS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatCullCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PointData)
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToClip)
        SHADER_PARAMETER(FVector2f, ViewSize)
        SHADER_PARAMETER(FVector4f, Tint)
        SHADER_PARAMETER(FVector4f, StyleValues)
        SHADER_PARAMETER(FVector4f, AppearanceValues)
        SHADER_PARAMETER(float, OpacityDensity)
        SHADER_PARAMETER(FVector4f, EffectTint)
        SHADER_PARAMETER(FVector4f, EffectValues)
        SHADER_PARAMETER(FVector4f, MaskCenterRadius)
        SHADER_PARAMETER(FVector2f, MaskValues)
        SHADER_PARAMETER(FVector2f, ProjectedRadiusRange)
        SHADER_PARAMETER(float, AntialiasingFilterVariance)
        SHADER_PARAMETER(uint32, AntialiasingMode)
        SHADER_PARAMETER(float, TemporalTransitionAlpha)
        SHADER_PARAMETER(FVector2f, ShapeLimits)
        SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
        SHADER_PARAMETER(FVector2f, ViewRectMin)
        SHADER_PARAMETER(float, SceneDepthBias)
        SHADER_PARAMETER(float, DepthPreCullMaxRadiusPixels)
        SHADER_PARAMETER(uint32, UseSceneDepth)
        SHADER_PARAMETER_ARRAY(FVector4f, LayerTypeWeight, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerMaskCenterFeather, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerMaskExtentEnabled, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerTint, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerVectorSeed, [KasumiMaxEffectLayers])
        SHADER_PARAMETER(uint32, LayerCount)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ExternalMaskTexture2D)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ExternalMaskTexture3D)
        SHADER_PARAMETER_SAMPLER(SamplerState, ExternalMaskSampler)
        SHADER_PARAMETER(FVector4f, ExternalMaskCenterMode)
        SHADER_PARAMETER(FVector4f, ExternalMaskExtentValues)
        SHADER_PARAMETER(uint32, Seed)
        SHADER_PARAMETER(uint32, PointCount)
        SHADER_PARAMETER(uint32, SourcePointCount)
        SHADER_PARAMETER(uint32, UseLogDepthSort)
        SHADER_PARAMETER(uint32, SortPath)
        SHADER_PARAMETER(uint32, EnableTiledFallback)
        SHADER_PARAMETER(uint32, MaxTilesPerSplat)
        SHADER_PARAMETER(uint32, TilePairCapacity)
        SHADER_PARAMETER(uint32, TileDepthBits)
        SHADER_PARAMETER(FUintVector4, TileConfig)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWVisibilityBuckets)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketCounts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWExactKeys)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWExactValues)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDrawIndirectArgs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileDrawIndirectArgs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileOverflow)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileKeys)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileValues)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatFinalizeTiledCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatFinalizeTiledCS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatFinalizeTiledCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER(uint32, TilePairCapacity)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDrawIndirectArgs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileDrawIndirectArgs)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWTileOverflow)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatPrefixCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatPrefixCS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatPrefixCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, BucketCounts)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketOffsets)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketWriteOffsets)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWDrawIndirectArgs)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatScatterCS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatScatterCS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatScatterCS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VisibilityBuckets)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, ExactValues)
        SHADER_PARAMETER(uint32, PointCount)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWBucketWriteOffsets)
        SHADER_PARAMETER_RDG_BUFFER_UAV(RWBuffer<uint>, RWSortedIndices)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatInstanceVS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatInstanceVS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatInstanceVS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, PointData)
        SHADER_PARAMETER_RDG_BUFFER_SRV(StructuredBuffer<float4>, SHData)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, VisibleIndices)
        SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<uint>, TileKeys)
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER(FMatrix44f, LocalToWorld)
        SHADER_PARAMETER(FMatrix44f, WorldToClip)
        SHADER_PARAMETER(FMatrix44f, PreviousLocalToWorld)
        SHADER_PARAMETER(FMatrix44f, LocalToSHDirection)
        SHADER_PARAMETER(FVector2f, ViewSize)
        SHADER_PARAMETER(FVector4f, Tint)
        SHADER_PARAMETER(FVector4f, StyleValues)
        SHADER_PARAMETER(FVector4f, AppearanceValues)
        SHADER_PARAMETER(float, OpacityDensity)
        SHADER_PARAMETER(FVector4f, EffectTint)
        SHADER_PARAMETER(FVector4f, EffectValues)
        SHADER_PARAMETER(FVector4f, MaskCenterRadius)
        SHADER_PARAMETER(FVector2f, MaskValues)
        SHADER_PARAMETER(FVector3f, ViewLocalPosition)
        SHADER_PARAMETER(float, MaxProjectedRadiusPixels)
        SHADER_PARAMETER(float, AntialiasingFilterVariance)
        SHADER_PARAMETER(uint32, AntialiasingMode)
        SHADER_PARAMETER(float, TemporalTransitionAlpha)
        SHADER_PARAMETER(FVector2f, ShapeLimits)
        SHADER_PARAMETER(FUintVector4, TileConfig)
        SHADER_PARAMETER(uint32, UseTilePairs)
        SHADER_PARAMETER(uint32, TileDepthBits)
        SHADER_PARAMETER(uint32, Seed)
        SHADER_PARAMETER(uint32, HigherOrderSHCoefficientsPerPoint)
        SHADER_PARAMETER(uint32, ResetVelocityHistory)
        SHADER_PARAMETER_ARRAY(FVector4f, LayerTypeWeight, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerMaskCenterFeather, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerMaskExtentEnabled, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerTint, [KasumiMaxEffectLayers])
        SHADER_PARAMETER_ARRAY(FVector4f, LayerVectorSeed, [KasumiMaxEffectLayers])
        SHADER_PARAMETER(uint32, LayerCount)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, ExternalMaskTexture2D)
        SHADER_PARAMETER_RDG_TEXTURE(Texture3D, ExternalMaskTexture3D)
        SHADER_PARAMETER_SAMPLER(SamplerState, ExternalMaskSampler)
        SHADER_PARAMETER(FVector4f, ExternalMaskCenterMode)
        SHADER_PARAMETER(FVector4f, ExternalMaskExtentValues)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatInstancePS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatInstancePS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatInstancePS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_INCLUDE(FSceneTextureShaderParameters, SceneTextures)
        SHADER_PARAMETER(float, SceneDepthBias)
        SHADER_PARAMETER(uint32, UseSceneDepth)
        SHADER_PARAMETER(FVector2f, ViewRectMin)
        SHADER_PARAMETER(FUintVector4, TileConfig)
        SHADER_PARAMETER(uint32, UseTilePairs)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};

class FKasumiSplatVelocityPS : public FGlobalShader
{
public:
    DECLARE_GLOBAL_SHADER(FKasumiSplatVelocityPS);
    SHADER_USE_PARAMETER_STRUCT(FKasumiSplatVelocityPS, FGlobalShader);

    BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
        SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)
        SHADER_PARAMETER_RDG_TEXTURE(Texture2D, VelocitySceneDepthTexture)
        SHADER_PARAMETER(float, SceneDepthBias)
        SHADER_PARAMETER(uint32, UseSceneDepth)
        SHADER_PARAMETER(FVector2f, ViewRectMin)
        SHADER_PARAMETER(FUintVector4, TileConfig)
        SHADER_PARAMETER(uint32, UseTilePairs)
    END_SHADER_PARAMETER_STRUCT()

    static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
    {
        return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM6);
    }
};
