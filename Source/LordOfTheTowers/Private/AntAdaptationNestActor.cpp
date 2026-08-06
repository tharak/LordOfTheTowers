#include "AntAdaptationNestActor.h"
#include "DrawDebugHelpers.h"

AAntAdaptationNestActor::AAntAdaptationNestActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

float AAntAdaptationNestActor::GetCreateCost() const
{
    return AntSize / 2.f + Aggression / 15.f + StartEnergy / 1000.f;
}

void AAntAdaptationNestActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FColor Color = Team == EAntAdaptationTeam::Blue ? FColor::Cyan : FColor::Red;
    DrawDebugSphere(GetWorld(), GetActorLocation(), 110.f, 16, Color, false, 0.06f, 0, 3.f);
    DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 140),
        FString::Printf(TEXT("%s food %.1f cost %.1f"), Team == EAntAdaptationTeam::Blue ? TEXT("BLUE") : TEXT("RED"), FoodStore, GetCreateCost()),
        nullptr, Color, 0.06f, false);
}
