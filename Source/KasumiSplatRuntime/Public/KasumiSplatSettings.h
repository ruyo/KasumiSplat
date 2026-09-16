#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "KasumiSplatSettings.generated.h"

/** Project-wide defaults and platform safety limits for KasumiSplat. */
UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="KasumiSplat"))
class KASUMISPLATRUNTIME_API UKasumiSplatSettings : public UDeveloperSettings
{
    GENERATED_BODY()
public:
    UPROPERTY(Config, EditAnywhere, Category="Streaming", meta=(ClampMin="16", ClampMax="4096", Units="MB"))
    int32 DefaultStreamingMemoryBudgetMB = 256;

    UPROPERTY(Config, EditAnywhere, Category="Streaming", meta=(ClampMin="1000"))
    int32 DefaultMaxResidentSplats = 1000000;

    UPROPERTY(Config, EditAnywhere, Category="Rendering")
    bool bRequireShaderModel6 = true;

    UPROPERTY(Config, EditAnywhere, Category="Rendering")
    bool bEnableSceneDepthByDefault = true;
};
