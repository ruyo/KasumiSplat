#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KasumiSplatNiagaraProbeActor.generated.h"

class UKasumiSplatComponent;
class UKasumiSplatNiagaraDriverComponent;
class UNiagaraComponent;

/** Ready-to-place actor for validating the optional Niagara bridge module. */
UCLASS()
class KASUMISPLATNIAGARA_API AKasumiSplatNiagaraProbeActor : public AActor
{
    GENERATED_BODY()

public:
    AKasumiSplatNiagaraProbeActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    TObjectPtr<UKasumiSplatComponent> SplatComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Niagara")
    TObjectPtr<UNiagaraComponent> NiagaraComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat|Niagara")
    TObjectPtr<UKasumiSplatNiagaraDriverComponent> NiagaraDriver;
};
