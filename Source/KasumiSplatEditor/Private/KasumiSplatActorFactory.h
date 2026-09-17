#pragma once

#include "ActorFactories/ActorFactory.h"
#include "KasumiSplatActorFactory.generated.h"

struct FAssetData;

/** Creates a configured KasumiSplatActor when a KasumiSplat asset is dropped into a level. */
UCLASS()
class UKasumiSplatActorFactory : public UActorFactory
{
    GENERATED_BODY()

public:
    UKasumiSplatActorFactory();

    virtual bool CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg) override;
    virtual void PostSpawnActor(UObject* Asset, AActor* NewActor) override;
    virtual UObject* GetAssetFromActorInstance(AActor* ActorInstance) override;
    virtual FString GetDefaultActorLabel(UObject* Asset) const override;
};
