#include "KasumiSplatActor.h"
#include "KasumiSplatComponent.h"

AKasumiSplatActor::AKasumiSplatActor()
{
    SplatComponent = CreateDefaultSubobject<UKasumiSplatComponent>(TEXT("KasumiSplat"));
    SetRootComponent(SplatComponent);
}
