#include "KasumiSplatActorFactory.h"

#include "AssetRegistry/AssetData.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/World.h"
#include "KasumiSplatActor.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatComponent.h"
#include "KasumiSplatNaming.h"
#include "KasumiSplatThumbnailRenderer.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"
#include "ThumbnailRendering/ThumbnailManager.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
    FKasumiSplatActorFactoryTest,
    "KasumiSplat.Editor.AssetDragDropPlacement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FKasumiSplatActorFactoryTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Imported asset prefix is added"), KasumiSplatNaming::MakeAssetName(TEXT("Garden")), FName(TEXT("KSA_Garden")));
    TestEqual(TEXT("Imported asset prefix is not duplicated"), KasumiSplatNaming::MakeAssetName(TEXT("KSA_Garden")), FName(TEXT("KSA_Garden")));
    TestEqual(
        TEXT("Imported package name matches the prefixed asset name"),
        KasumiSplatNaming::MakeAssetPackageName(TEXT("/Game/Scans/Garden"), TEXT("KSA_Garden")),
        FString(TEXT("/Game/Scans/KSA_Garden")));

    UKasumiSplatActorFactory* Factory = nullptr;
    if (GEditor)
    {
        for (UActorFactory* Candidate : GEditor->ActorFactories)
        {
            if (UKasumiSplatActorFactory* KasumiFactory = Cast<UKasumiSplatActorFactory>(Candidate))
            {
                Factory = KasumiFactory;
                break;
            }
        }
    }
    TestNotNull(TEXT("KasumiSplat actor factory is registered with the editor"), Factory);
    if (!Factory)
    {
        return false;
    }

    UKasumiSplatAsset* Asset = NewObject<UKasumiSplatAsset>(GetTransientPackage(), TEXT("KSA_Garden"));
    TestTrue(TEXT("Imported assets default placed Actors to Full Quality"), Asset->bDefaultFullQualityReference);
    Asset->bDefaultFullQualityReference = false;
    UKasumiSplatAsset* PreviewAsset = NewObject<UKasumiSplatAsset>();
    TArray<FKasumiSplatPoint> PreviewSource;
    PreviewSource.AddDefaulted_GetRef().Color = FLinearColor::White;
    PreviewAsset->SetImportedPoints(MoveTemp(PreviewSource), TEXT("ThumbnailTest"), 1.0);
    UKasumiSplatThumbnailRenderer* ThumbnailRenderer = NewObject<UKasumiSplatThumbnailRenderer>();
    TestTrue(TEXT("Generated preview can be visualized without point BulkData"),
        ThumbnailRenderer->CanVisualizeAsset(PreviewAsset));
    if (FApp::CanEverRender())
    {
        const FThumbnailRenderingInfo* ThumbnailInfo = UThumbnailManager::Get().GetRenderingInfo(PreviewAsset);
        TestTrue(TEXT("KasumiSplat thumbnail renderer is registered"),
            ThumbnailInfo && ThumbnailInfo->Renderer && ThumbnailInfo->Renderer->IsA<UKasumiSplatThumbnailRenderer>());
    }
    FText ErrorMessage;
    TestTrue(TEXT("KasumiSplat assets are accepted for placement"), Factory->CanCreateActorFrom(FAssetData(Asset), ErrorMessage));

    UWorld* World = GEditor->GetEditorWorldContext().World();
    TestNotNull(TEXT("Editor world is available"), World);
    if (!World)
    {
        return false;
    }

    FActorSpawnParameters SpawnParameters;
    SpawnParameters.ObjectFlags |= RF_Transient;
    SpawnParameters.bTemporaryEditorActor = true;
    AKasumiSplatActor* Actor = World->SpawnActor<AKasumiSplatActor>(
        FVector::ZeroVector,
        FRotator::ZeroRotator,
        SpawnParameters);
    TestNotNull(TEXT("KasumiSplat actor can be spawned"), Actor);
    if (!Actor)
    {
        return false;
    }

    Factory->PostSpawnActor(Asset, Actor);
    TestTrue(TEXT("Dropped asset is assigned to the component"), Actor->SplatComponent->Asset == Asset);
    TestFalse(TEXT("Imported data disables the synthetic fallback"), Actor->SplatComponent->bUseSyntheticFallback);
    TestFalse(TEXT("Placed Actor inherits the asset Full Quality preference"), Actor->SplatComponent->bFullQualityReference);
    TestTrue(TEXT("Actor factory exposes the assigned asset"), Factory->GetAssetFromActorInstance(Actor) == Asset);
    TestEqual(TEXT("Placed actor label uses KS without retaining KSA"), Factory->GetDefaultActorLabel(Asset), FString(TEXT("KS_Garden")));
    TestEqual(
        TEXT("Placed actor label does not duplicate an existing KS prefix"),
        KasumiSplatNaming::MakeActorLabel(NewObject<UKasumiSplatAsset>(GetTransientPackage(), TEXT("KS_Garden"))),
        FString(TEXT("KS_Garden")));

    World->DestroyActor(Actor, false, false);
    return true;
}

#endif
