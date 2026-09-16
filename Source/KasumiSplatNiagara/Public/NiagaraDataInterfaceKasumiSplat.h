#pragma once

#include "CoreMinimal.h"
#include "NiagaraDataInterface.h"
#include "NiagaraDataInterfaceKasumiSplat.generated.h"

class AActor;
struct FVMExternalFunctionBindingInfo;

/** CPU and GPU Niagara access to the immutable resident KasumiSplat snapshot. */
UCLASS(EditInlineNew, Category="KasumiSplat", meta=(DisplayName="Kasumi Splat"))
class KASUMISPLATNIAGARA_API UNiagaraDataInterfaceKasumiSplat : public UNiagaraDataInterface
{
    GENERATED_UCLASS_BODY()
public:
    /** Actor containing the source UKasumiSplatComponent. Defaults to the Niagara system's attach root. */
    UPROPERTY(EditAnywhere, Category="Source")
    TObjectPtr<AActor> SourceActor;

    virtual void PostInitProperties() override;
    virtual bool CanExecuteOnTarget(ENiagaraSimTarget Target) const override;
    virtual bool InitPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
    virtual void DestroyPerInstanceData(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance) override;
    virtual int32 PerInstanceDataSize() const override;
    virtual bool HasPreSimulateTick() const override { return true; }
    virtual bool PerInstanceTick(void* PerInstanceData, FNiagaraSystemInstance* SystemInstance, float DeltaSeconds) override;
    virtual void ProvidePerInstanceDataForRenderThread(void* DataForRenderThread, void* PerInstanceData, const FNiagaraSystemInstanceID& SystemInstance) override;
    virtual void GetVMExternalFunction(const FVMExternalFunctionBindingInfo& BindingInfo, void* InstanceData, FVMExternalFunction& OutFunc) override;
#if WITH_EDITORONLY_DATA
    virtual bool AppendCompileHash(FNiagaraCompileHashVisitor* InVisitor) const override;
    virtual bool GetFunctionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, const FNiagaraDataInterfaceGeneratedFunction& FunctionInfo, int FunctionInstanceIndex, FString& OutHLSL) override;
    virtual void GetParameterDefinitionHLSL(const FNiagaraDataInterfaceGPUParamInfo& ParamInfo, FString& OutHLSL) override;
#endif
    virtual void BuildShaderParameters(FNiagaraShaderParametersBuilder& ShaderParametersBuilder) const override;
    virtual void SetShaderParameters(const FNiagaraDataInterfaceSetShaderParametersContext& Context) const override;
    virtual bool Equals(const UNiagaraDataInterface* Other) const override;

protected:
#if WITH_EDITORONLY_DATA
    virtual void GetFunctionsInternal(TArray<FNiagaraFunctionSignature>& OutFunctions) const override;
#endif

private:
    void GetPointCountVM(FVectorVMExternalFunctionContext& Context);
    void GetPointVM(FVectorVMExternalFunctionContext& Context);
    static const FName GetPointCountName;
    static const FName GetPointName;
};
