#include "BeeSmartHiveActor.h"

#include "DrawDebugHelpers.h"

ABeeSmartHiveActor::ABeeSmartHiveActor()
{
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

void ABeeSmartHiveActor::EnterSite()
{
    ++BeesOnSite;
}

void ABeeSmartHiveActor::LeaveSite()
{
    BeesOnSite = FMath::Max(0, BeesOnSite - 1);
}

void ABeeSmartHiveActor::CommitBee()
{
    ++CommittedBees;
}

bool ABeeSmartHiveActor::HasReachedQuorum() const
{
    return BeesOnSite + CommittedBees >= Quorum;
}

void ABeeSmartHiveActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FColor Color = HasReachedQuorum() ? FColor::Green : FColor(255, FMath::RoundToInt(80.f + Quality * 175.f), 40);
    const float Lifetime = 0.06f;
    DrawDebugSphere(GetWorld(), GetActorLocation(), 80.f + Quality * 70.f, 16, Color, false, Lifetime, 0, 3.f);
    DrawDebugString(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 110.f),
        FString::Printf(TEXT("Q %.2f  On %d  Committed %d"), Quality, BeesOnSite, CommittedBees),
        nullptr, Color, Lifetime, false);
}
