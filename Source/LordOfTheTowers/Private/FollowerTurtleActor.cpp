#include "FollowerTurtleActor.h"
#include "FollowerSimulationActor.h"
#include "DrawDebugHelpers.h"
#include "FollowerMovementComponent.h"

AFollowerTurtleActor::AFollowerTurtleActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    Movement = CreateDefaultSubobject<UFollowerMovementComponent>(TEXT("Movement"));
}

void AFollowerTurtleActor::Initialize()
{
    Leader = nullptr;
    Follower = nullptr;
}

void AFollowerTurtleActor::TurnAndMove(AFollowerSimulationActor* Simulation, float DeltaSeconds)
{
    if (!Simulation) return;
    if (Leader)
    {
        const FVector ToLeader = Leader->GetActorLocation() - GetActorLocation();
        if (!ToLeader.IsNearlyZero()) SetActorRotation(ToLeader.Rotation());
    }
    else
    {
        SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + FMath::FRandRange(-Simulation->Waver, Simulation->Waver), 0.f));
    }

    Movement->MoveAndWrap(Simulation, DeltaSeconds);
    const FVector Location = GetActorLocation();

    FColor Color = FColor::Magenta;
    if (Leader && Follower) Color = FColor(135, 206, 235);
    else if (Follower) Color = FColor::Yellow;
    else if (Leader) Color = FColor(50, 255, 50);
    DrawDebugPoint(GetWorld(), Location + FVector(0.f, 0.f, 30.f), 10.f, Color, false, 0.08f, 0);
    if (Leader) DrawDebugLine(GetWorld(), Location + FVector(0.f, 0.f, 30.f), Leader->GetActorLocation() + FVector(0.f, 0.f, 30.f), Color, false, 0.08f, 0, 1.f);
}
