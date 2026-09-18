#include "KasumiSplatActorFactory.h"

#include "AssetRegistry/AssetData.h"
#include "KasumiSplatActor.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatComponent.h"
#include "KasumiSplatNaming.h"

#define LOCTEXT_NAMESPACE "KasumiSplatActorFactory"

UKasumiSplatActorFactory::UKasumiSplatActorFactory()
{
    DisplayName = LOCTEXT("DisplayName", "Kasumi Splat");
    NewActorClass = AKasumiSplatActor::StaticClass();
    bShowInEditorQuickMenu = false;
    bUseSurfaceOrientation = false;
}

bool UKasumiSplatActorFactory::CanCreateActorFrom(const FAssetData& AssetData, FText& OutErrorMsg)
{
    if (AssetData.IsValid() && AssetData.IsInstanceOf(UKasumiSplatAsset::StaticClass()))
    {
        return true;
    }

    OutErrorMsg = LOCTEXT("InvalidAsset", "A valid Kasumi Splat asset must be specified.");
    return false;
}

void UKasumiSplatActorFactory::PostSpawnActor(UObject* Asset, AActor* NewActor)
{
    Super::PostSpawnActor(Asset, NewActor);

    UKasumiSplatAsset* SplatAsset = CastChecked<UKasumiSplatAsset>(Asset);
    AKasumiSplatActor* SplatActor = CastChecked<AKasumiSplatActor>(NewActor);
    check(SplatActor->SplatComponent);

    SplatActor->SplatComponent->Modify();
    SplatActor->SplatComponent->Asset = SplatAsset;
    SplatActor->SplatComponent->bUseSyntheticFallback = false;
    SplatActor->SplatComponent->bFullQualityReference = SplatAsset->bDefaultFullQualityReference;
    SplatActor->SplatComponent->ReloadPoints();
}

UObject* UKasumiSplatActorFactory::GetAssetFromActorInstance(AActor* ActorInstance)
{
    const AKasumiSplatActor* SplatActor = Cast<AKasumiSplatActor>(ActorInstance);
    return SplatActor && SplatActor->SplatComponent
        ? SplatActor->SplatComponent->Asset.Get()
        : nullptr;
}

FString UKasumiSplatActorFactory::GetDefaultActorLabel(UObject* Asset) const
{
    return KasumiSplatNaming::MakeActorLabel(Asset);
}

#undef LOCTEXT_NAMESPACE
