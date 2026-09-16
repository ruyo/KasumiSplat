#include "NiagaraDataInterfaceKasumiSplat.h"

#include "GameFramework/Actor.h"
#include "KasumiSplatComponent.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraTypes.h"
#include "NiagaraCompileHashVisitor.h"
#include "NiagaraShaderParametersBuilder.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "SystemTextures.h"
#include "UnifiedBuffer.h"

#define LOCTEXT_NAMESPACE "NiagaraDataInterfaceKasumiSplat"

const FName UNiagaraDataInterfaceKasumiSplat::GetPointCountName(TEXT("GetPointCount"));
const FName UNiagaraDataInterfaceKasumiSplat::GetPointName(TEXT("GetPoint"));
static const TCHAR* KasumiDITemplateShaderFile = TEXT("/Plugin/KasumiSplat/Private/NiagaraDataInterfaceKasumiSplatTemplate.ush");

BEGIN_SHADER_PARAMETER_STRUCT(FKasumiSplatDIShaderParameters, )
    SHADER_PARAMETER_RDG_BUFFER_SRV(Buffer<float4>, PointData)
    SHADER_PARAMETER(FMatrix44f, LocalToWorld)
    SHADER_PARAMETER(int32, PointCount)
END_SHADER_PARAMETER_STRUCT()

struct FKasumiSplatDIInstanceData
{
    TWeakObjectPtr<UKasumiSplatComponent> Component;
    TSharedPtr<const TArray<FKasumiSplatPoint>, ESPMode::ThreadSafe> Points;
    int32 Revision = INDEX_NONE;
    int32 LastSentRevision = INDEX_NONE;
    FTransform LocalToWorld = FTransform::Identity;
};

struct FKasumiSplatDIGameToRenderData
{
    TArray<FVector4f> DataToUpload;
    FMatrix44f LocalToWorld = FMatrix44f::Identity;
    int32 PointCount = 0;
};

struct FKasumiSplatDIRenderData
{
    TArray<FVector4f> DataToUpload;
    TRefCountPtr<FRDGPooledBuffer> PointBuffer;
    FMatrix44f LocalToWorld = FMatrix44f::Identity;
    int32 PointCount = 0;
};

struct FKasumiSplatDIProxy final : public FNiagaraDataInterfaceProxy
{
    virtual int32 PerInstanceDataPassedToRenderThreadSize() const override { return sizeof(FKasumiSplatDIGameToRenderData); }

    virtual void ConsumePerInstanceDataFromGameThread(void* PerInstanceData, const FNiagaraSystemInstanceID& InstanceID) override
    {
        FKasumiSplatDIGameToRenderData* Source = static_cast<FKasumiSplatDIGameToRenderData*>(PerInstanceData);
        FKasumiSplatDIRenderData& Target = Instances.FindOrAdd(InstanceID);
        Target.DataToUpload = MoveTemp(Source->DataToUpload);
        Target.LocalToWorld = Source->LocalToWorld;
        Target.PointCount = Source->PointCount;
        Source->~FKasumiSplatDIGameToRenderData();
    }

    virtual void PreStage(const FNDIGpuComputePreStageContext& Context) override
    {
        FKasumiSplatDIRenderData* Data = Instances.Find(Context.GetSystemInstanceID());
        if (!Data || Data->DataToUpload.IsEmpty()) return;
        FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
        const uint64 UploadBytes = uint64(Data->DataToUpload.Num()) * sizeof(FVector4f);
        FRDGBufferDesc Desc = FRDGBufferDesc::CreateBufferDesc(sizeof(FVector4f), FMath::Max(1, Data->DataToUpload.Num()));
        ResizeBufferIfNeeded(GraphBuilder, Data->PointBuffer, Desc, TEXT("KasumiSplat.NiagaraPointBuffer"));
        GraphBuilder.QueueBufferUpload(
            GraphBuilder.RegisterExternalBuffer(Data->PointBuffer),
            Data->DataToUpload.GetData(),
            UploadBytes,
            [KeepAlive = MoveTemp(Data->DataToUpload)](const void*) {});
    }

    TMap<FNiagaraSystemInstanceID, FKasumiSplatDIRenderData> Instances;
};

UNiagaraDataInterfaceKasumiSplat::UNiagaraDataInterfaceKasumiSplat(FObjectInitializer const& ObjectInitializer)
    : Super(ObjectInitializer)
{
    Proxy.Reset(new FKasumiSplatDIProxy());
}

void UNiagaraDataInterfaceKasumiSplat::PostInitProperties()
{
    Super::PostInitProperties();
    if (HasAnyFlags(RF_ClassDefaultObject))
    {
        FNiagaraTypeRegistry::Register(
            FNiagaraTypeDefinition(GetClass()),
            ENiagaraTypeRegistryFlags::AllowAnyVariable | ENiagaraTypeRegistryFlags::AllowParameter);
    }
}

bool UNiagaraDataInterfaceKasumiSplat::CanExecuteOnTarget(ENiagaraSimTarget Target) const
{
    return true;
}

bool UNiagaraDataInterfaceKasumiSplat::InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
    new (PerInstanceData) FKasumiSplatDIInstanceData();
    return true;
}

void UNiagaraDataInterfaceKasumiSplat::DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance)
{
    static_cast<FKasumiSplatDIInstanceData*>(PerInstanceData)->~FKasumiSplatDIInstanceData();
    ENQUEUE_RENDER_COMMAND(RemoveKasumiSplatDIInstance)(
        [Proxy = GetProxyAs<FKasumiSplatDIProxy>(), InstanceID = SystemInstance->GetId()](FRHICommandListImmediate&)
        {
            Proxy->Instances.Remove(InstanceID);
        });
}

int32 UNiagaraDataInterfaceKasumiSplat::PerInstanceDataSize() const
{
    return sizeof(FKasumiSplatDIInstanceData);
}

bool UNiagaraDataInterfaceKasumiSplat::PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds)
{
    FKasumiSplatDIInstanceData* Data = static_cast<FKasumiSplatDIInstanceData*>(PerInstanceData);
    UKasumiSplatComponent* Component = Data->Component.Get();
    AActor* Actor = SourceActor.Get();
    if (!Actor && SystemInstance && SystemInstance->GetAttachComponent())
    {
        Actor = SystemInstance->GetAttachComponent()->GetOwner();
    }
    if (!Component || Component->GetOwner() != Actor)
    {
        Component = Actor ? Actor->FindComponentByClass<UKasumiSplatComponent>() : nullptr;
        Data->Component = Component;
        Data->Revision = INDEX_NONE;
    }
    if (Component && Data->Revision != Component->GetResidentRevision())
    {
        Data->Points = Component->GetResidentPointSnapshot();
        Data->Revision = Component->GetResidentRevision();
    }
    else if (!Component)
    {
        Data->Points.Reset();
    }
    Data->LocalToWorld = Component ? Component->GetComponentTransform() : FTransform::Identity;
    return false;
}

void UNiagaraDataInterfaceKasumiSplat::ProvidePerInstanceDataForRenderThread(
    void* DataForRenderThread,
    void* PerInstanceData,
    const FNiagaraSystemInstanceID& SystemInstance)
{
    FKasumiSplatDIInstanceData* Source = static_cast<FKasumiSplatDIInstanceData*>(PerInstanceData);
    FKasumiSplatDIGameToRenderData* Target = new (DataForRenderThread) FKasumiSplatDIGameToRenderData();
    Target->LocalToWorld = FMatrix44f(Source->LocalToWorld.ToMatrixWithScale());
    Target->PointCount = Source->Points ? Source->Points->Num() : 0;
    if (Source->Revision != Source->LastSentRevision && Source->Points)
    {
        Target->DataToUpload.SetNumUninitialized(Source->Points->Num() * 3);
        for (int32 Index = 0; Index < Source->Points->Num(); ++Index)
        {
            const FKasumiSplatPoint& Point = (*Source->Points)[Index];
            float StableIdBits;
            FMemory::Memcpy(&StableIdBits, &Point.StableId, sizeof(int32));
            Target->DataToUpload[Index * 3 + 0] = FVector4f(FVector3f(Point.Position), StableIdBits);
            Target->DataToUpload[Index * 3 + 1] = FVector4f(FVector3f(Point.Sigma), Point.Color.A);
            Target->DataToUpload[Index * 3 + 2] = FVector4f(Point.Color.R, Point.Color.G, Point.Color.B, 0.0f);
        }
        Source->LastSentRevision = Source->Revision;
    }
}

#if WITH_EDITORONLY_DATA
void UNiagaraDataInterfaceKasumiSplat::GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const
{
    FNiagaraFunctionSignature Count;
    Count.Name = GetPointCountName;
    Count.Description = LOCTEXT("GetPointCountDescription", "Returns the current resident splat count.");
    Count.bMemberFunction = true;
    Count.bSupportsCPU = true;
    Count.bSupportsGPU = true;
    Count.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Kasumi Splat"));
    Count.Outputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("Count"));
    OutFunctions.Add(Count);

    FNiagaraFunctionSignature Point;
    Point.Name = GetPointName;
    Point.Description = LOCTEXT("GetPointDescription", "Reads one resident splat in world space.");
    Point.bMemberFunction = true;
    Point.bSupportsCPU = true;
    Point.bSupportsGPU = true;
    Point.Inputs.Emplace(FNiagaraTypeDefinition(GetClass()), TEXT("Kasumi Splat"));
    Point.Inputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("Index"));
    Point.Outputs.Emplace(FNiagaraTypeDefinition::GetPositionDef(), TEXT("Position"));
    Point.Outputs.Emplace(FNiagaraTypeDefinition::GetColorDef(), TEXT("Color"));
    Point.Outputs.Emplace(FNiagaraTypeDefinition::GetVec3Def(), TEXT("Sigma"));
    Point.Outputs.Emplace(FNiagaraTypeDefinition::GetIntDef(), TEXT("Stable ID"));
    Point.Outputs.Emplace(FNiagaraTypeDefinition::GetBoolDef(), TEXT("Valid"));
    OutFunctions.Add(Point);
}
#endif

#if WITH_EDITORONLY_DATA
bool UNiagaraDataInterfaceKasumiSplat::AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const
{
    if (!Super::AppendCompileHash(InVisitor)) return false;
    InVisitor->UpdateShaderFile(KasumiDITemplateShaderFile);
    InVisitor->UpdateShaderParameters<FKasumiSplatDIShaderParameters>();
    return true;
}

bool UNiagaraDataInterfaceKasumiSplat::GetFunctionHLSL(
    const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
    const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo,
    int FunctionInstanceIndex,
    FString& OutHLSL)
{
    return FunctionInfo.DefinitionName == GetPointCountName || FunctionInfo.DefinitionName == GetPointName;
}

void UNiagaraDataInterfaceKasumiSplat::GetParameterDefinitionHLSL(
    const FNiagaraDataInterfaceGPUParamInfo& ParamInfo,
    FString& OutHLSL)
{
    AppendTemplateHLSL(OutHLSL, KasumiDITemplateShaderFile,
        {{TEXT("ParameterName"), ParamInfo.DataInterfaceHLSLSymbol}});
}
#endif

void UNiagaraDataInterfaceKasumiSplat::BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const
{
    ShaderParametersBuilder.AddNestedStruct<FKasumiSplatDIShaderParameters>();
}

void UNiagaraDataInterfaceKasumiSplat::SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const
{
    FRDGBuilder& GraphBuilder = Context.GetGraphBuilder();
    FKasumiSplatDIProxy& DataProxy = Context.GetProxy<FKasumiSplatDIProxy>();
    FKasumiSplatDIRenderData* Data = DataProxy.Instances.Find(Context.GetSystemInstanceID());
    FKasumiSplatDIShaderParameters* Parameters = Context.GetParameterNestedStruct<FKasumiSplatDIShaderParameters>();
    Parameters->PointCount = Data ? Data->PointCount : 0;
    Parameters->LocalToWorld = Data ? Data->LocalToWorld : FMatrix44f::Identity;
    if (Data && Data->PointBuffer)
    {
        Parameters->PointData = GraphBuilder.CreateSRV(GraphBuilder.RegisterExternalBuffer(Data->PointBuffer), PF_A32B32G32R32F);
    }
    else
    {
        Parameters->PointData = GraphBuilder.CreateSRV(GSystemTextures.GetDefaultBuffer(GraphBuilder, 16u, 1u), PF_A32B32G32R32F);
    }
}

bool UNiagaraDataInterfaceKasumiSplat::Equals(const UNiagaraDataInterface* Other) const
{
    const UNiagaraDataInterfaceKasumiSplat* Typed = Cast<const UNiagaraDataInterfaceKasumiSplat>(Other);
    return Super::Equals(Other) && Typed && Typed->SourceActor == SourceActor;
}

void UNiagaraDataInterfaceKasumiSplat::GetVMExternalFunction(
    const FVMExternalFunctionBindingInfo& BindingInfo,
    void* InstanceData,
    FVMExternalFunction& OutFunc)
{
    if (BindingInfo.Name == GetPointCountName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceKasumiSplat::GetPointCountVM);
    }
    else if (BindingInfo.Name == GetPointName)
    {
        OutFunc = FVMExternalFunction::CreateUObject(this, &UNiagaraDataInterfaceKasumiSplat::GetPointVM);
    }
}

void UNiagaraDataInterfaceKasumiSplat::GetPointCountVM(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FUserPtrHandler<FKasumiSplatDIInstanceData> Data(Context);
    FNDIOutputParam<int32> OutCount(Context);
    const int32 Count = Data.Get() && Data.Get()->Points ? Data.Get()->Points->Num() : 0;
    for (int32 Instance = 0; Instance < Context.GetNumInstances(); ++Instance) OutCount.SetAndAdvance(Count);
}

void UNiagaraDataInterfaceKasumiSplat::GetPointVM(FVectorVMExternalFunctionContext& Context)
{
    VectorVM::FUserPtrHandler<FKasumiSplatDIInstanceData> Data(Context);
    FNDIInputParam<int32> InIndex(Context);
    FNDIOutputParam<FVector3f> OutPosition(Context);
    FNDIOutputParam<FLinearColor> OutColor(Context);
    FNDIOutputParam<FVector3f> OutSigma(Context);
    FNDIOutputParam<int32> OutStableId(Context);
    FNDIOutputParam<FNiagaraBool> OutValid(Context);
    for (int32 Instance = 0; Instance < Context.GetNumInstances(); ++Instance)
    {
        const int32 Index = InIndex.GetAndAdvance();
        const bool bValid = Data.Get() && Data.Get()->Points && Data.Get()->Points->IsValidIndex(Index);
        if (bValid)
        {
            const FKasumiSplatPoint& Point = (*Data.Get()->Points)[Index];
            OutPosition.SetAndAdvance(FVector3f(Data.Get()->LocalToWorld.TransformPosition(Point.Position)));
            OutColor.SetAndAdvance(Point.Color);
            OutSigma.SetAndAdvance(FVector3f(Point.Sigma));
            OutStableId.SetAndAdvance(Point.StableId);
        }
        else
        {
            OutPosition.SetAndAdvance(FVector3f::ZeroVector);
            OutColor.SetAndAdvance(FLinearColor::Transparent);
            OutSigma.SetAndAdvance(FVector3f::ZeroVector);
            OutStableId.SetAndAdvance(INDEX_NONE);
        }
        OutValid.SetAndAdvance(FNiagaraBool(bValid));
    }
}

#undef LOCTEXT_NAMESPACE
