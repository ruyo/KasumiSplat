#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "KasumiSplatActor.generated.h"

class UKasumiSplatComponent;

UCLASS(PrioritizeCategories=("Asset", "Appearance", "Effects", "Quality", "Rendering", "Streaming", "Performance", "Debug"))
class KASUMISPLATRUNTIME_API AKasumiSplatActor : public AActor
{
    GENERATED_BODY()
public:
    AKasumiSplatActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Asset")
    TObjectPtr<UKasumiSplatComponent> SplatComponent;
};
