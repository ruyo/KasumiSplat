#include "Modules/ModuleManager.h"

#include "Brushes/SlateImageBrush.h"
#include "Interfaces/IPluginManager.h"
#include "KasumiSplatAsset.h"
#include "KasumiSplatThumbnailRenderer.h"
#include "Styling/SlateStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "ThumbnailRendering/ThumbnailManager.h"

namespace
{
    TSharedPtr<FSlateStyleSet> KasumiSplatStyle;
}

class FKasumiSplatEditorModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("KasumiSplat"));
        if (Plugin.IsValid())
        {
            KasumiSplatStyle = MakeShared<FSlateStyleSet>(TEXT("KasumiSplatStyle"));
            KasumiSplatStyle->SetContentRoot(Plugin->GetBaseDir() / TEXT("Resources"));
            const FString IconPath = KasumiSplatStyle->RootToContentDir(TEXT("Icon128"), TEXT(".png"));
            KasumiSplatStyle->Set(
                TEXT("ClassIcon.KasumiSplatAsset"),
                new FSlateImageBrush(IconPath, FVector2D(16.0, 16.0)));
            KasumiSplatStyle->Set(
                TEXT("ClassThumbnail.KasumiSplatAsset"),
                new FSlateImageBrush(IconPath, FVector2D(64.0, 64.0)));
            FSlateStyleRegistry::RegisterSlateStyle(*KasumiSplatStyle);
        }

        UThumbnailManager::Get().RegisterCustomRenderer(
            UKasumiSplatAsset::StaticClass(),
            UKasumiSplatThumbnailRenderer::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UObjectInitialized())
        {
            UThumbnailManager::Get().UnregisterCustomRenderer(UKasumiSplatAsset::StaticClass());
        }
        if (KasumiSplatStyle.IsValid())
        {
            FSlateStyleRegistry::UnRegisterSlateStyle(*KasumiSplatStyle);
            KasumiSplatStyle.Reset();
        }
    }
};

IMPLEMENT_MODULE(FKasumiSplatEditorModule, KasumiSplatEditor)
