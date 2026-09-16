#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KasumiSplatActor.generated.h"

class UKasumiSplatComponent;

UCLASS()
class KASUMISPLATRUNTIME_API AKasumiSplatActor : public AActor
{
    GENERATED_BODY()
public:
    AKasumiSplatActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="KasumiSplat")
    TObjectPtr<UKasumiSplatComponent> SplatComponent;
};
