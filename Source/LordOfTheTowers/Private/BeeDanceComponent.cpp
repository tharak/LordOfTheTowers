#include "BeeDanceComponent.h"

UBeeDanceComponent::UBeeDanceComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.033f;
}

void UBeeDanceComponent::StartDance(float Quality)
{
    DanceQuality = FMath::Clamp(Quality, 0.f, 1.f);
    RemainingDanceTime = 1.5f + DanceQuality * 5.f;
    bDancing = true;
}

void UBeeDanceComponent::StopDance()
{
    bDancing = false;
    RemainingDanceTime = 0.f;
}

void UBeeDanceComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    if (!bDancing) return;
    RemainingDanceTime -= DeltaTime;
    if (RemainingDanceTime <= 0.f)
    {
        StopDance();
        OnDanceFinished.Broadcast();
    }
}
