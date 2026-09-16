#include "KasumiSplatNiagaraDriverComponent.h"

#include "KasumiSplatComponent.h"
#include "NiagaraComponent.h"

UKasumiSplatNiagaraDriverComponent::UKasumiSplatNiagaraDriverComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    bTickInEditor = true;
}

void UKasumiSplatNiagaraDriverComponent::ResolveComponents()
{
    if (!bAutoFindComponents || !GetOwner())
    {
        return;
    }

    if (!SplatComponent)
    {
        SplatComponent = GetOwner()->FindComponentByClass<UKasumiSplatComponent>();
    }
    if (!NiagaraComponent)
    {
        NiagaraComponent = GetOwner()->FindComponentByClass<UNiagaraComponent>();
    }
}

void UKasumiSplatNiagaraDriverComponent::SynchronizeParameters()
{
    ResolveComponents();
    if (!SplatComponent || !NiagaraComponent)
    {
        return;
    }

    const FKasumiSplatStyle& Style = SplatComponent->Style;
    NiagaraComponent->SetVariableFloat(ProgressParameter, Style.Progress);
    NiagaraComponent->SetVariableFloat(DisplacementParameter, Style.Displacement);
    NiagaraComponent->SetVariableFloat(ScaleParameter, Style.Scale);
    NiagaraComponent->SetVariableLinearColor(TintParameter, Style.Tint);
}

void UKasumiSplatNiagaraDriverComponent::OnRegister()
{
    Super::OnRegister();
    ResolveComponents();
    SynchronizeParameters();
}

void UKasumiSplatNiagaraDriverComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    ResolveComponents();

    if (bAnimateProgress && SplatComponent)
    {
        ElapsedTime += DeltaTime;
        const float Duration = FMath::Max(CycleDuration, 0.1f);
        const float Phase = FMath::Fmod(ElapsedTime, Duration) / Duration;
        SplatComponent->SetProgress(0.5f - 0.5f * FMath::Cos(Phase * UE_TWO_PI));
    }

    SynchronizeParameters();
}
