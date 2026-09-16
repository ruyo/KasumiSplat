#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "KasumiSplatNiagaraDriverComponent.generated.h"

class UKasumiSplatComponent;
class UNiagaraComponent;

/**
 * Small integration seam between a KasumiSplat component and Niagara.
 *
 * The prototype deliberately shares only stable, low-frequency style controls.
 * Point buffers stay owned by KasumiSplat; a future data interface can expose
 * them to Niagara without coupling the core renderer to Niagara.
 */
UCLASS(ClassGroup=(Rendering, Niagara), meta=(BlueprintSpawnableComponent))
class KASUMISPLATNIAGARA_API UKasumiSplatNiagaraDriverComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UKasumiSplatNiagaraDriverComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara")
    TObjectPtr<UKasumiSplatComponent> SplatComponent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara")
    TObjectPtr<UNiagaraComponent> NiagaraComponent;

    /** Finds sibling components when explicit references are not assigned. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara")
    bool bAutoFindComponents = true;

    /** Animates the same normalized progress value used by both systems. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Prototype")
    bool bAnimateProgress = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Prototype", meta=(ClampMin="0.1", Units="s"))
    float CycleDuration = 4.0f;

    // These are raw C++ Niagara variable names, so the User namespace is explicit.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Parameters")
    FName ProgressParameter = TEXT("User.KasumiProgress");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Parameters")
    FName DisplacementParameter = TEXT("User.KasumiDisplacement");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Parameters")
    FName ScaleParameter = TEXT("User.KasumiScale");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="KasumiSplat|Niagara|Parameters")
    FName TintParameter = TEXT("User.KasumiTint");

    UFUNCTION(BlueprintCallable, Category="KasumiSplat|Niagara")
    void ResolveComponents();

    UFUNCTION(BlueprintCallable, CallInEditor, Category="KasumiSplat|Niagara")
    void SynchronizeParameters();

protected:
    virtual void OnRegister() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    float ElapsedTime = 0.0f;
};
