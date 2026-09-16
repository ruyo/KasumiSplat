#include "KasumiSplatNiagaraProbeActor.h"

#include "KasumiSplatComponent.h"
#include "KasumiSplatNiagaraDriverComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "UObject/ConstructorHelpers.h"

AKasumiSplatNiagaraProbeActor::AKasumiSplatNiagaraProbeActor()
{
    SplatComponent = CreateDefaultSubobject<UKasumiSplatComponent>(TEXT("KasumiSplat"));
    SetRootComponent(SplatComponent);

    NiagaraComponent = CreateDefaultSubobject<UNiagaraComponent>(TEXT("Niagara"));
    NiagaraComponent->SetupAttachment(SplatComponent);
    static ConstructorHelpers::FObjectFinder<UNiagaraSystem> ProbeSystem(
        TEXT("/KasumiSplat/Niagara/NS_KasumiSplatBridgeProbe.NS_KasumiSplatBridgeProbe"));
    if (ProbeSystem.Succeeded())
    {
        NiagaraComponent->SetAsset(ProbeSystem.Object);
    }

    NiagaraDriver = CreateDefaultSubobject<UKasumiSplatNiagaraDriverComponent>(TEXT("NiagaraDriver"));
    NiagaraDriver->SplatComponent = SplatComponent;
    NiagaraDriver->NiagaraComponent = NiagaraComponent;
}
