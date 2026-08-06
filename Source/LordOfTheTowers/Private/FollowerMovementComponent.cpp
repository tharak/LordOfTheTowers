#include "FollowerMovementComponent.h"
#include "FollowerSimulationActor.h"
#include "GameFramework/Actor.h"

void UFollowerMovementComponent::MoveAndWrap(AFollowerSimulationActor* Simulation, float DeltaSeconds)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Simulation) return;
    Owner->AddActorWorldOffset(Owner->GetActorForwardVector() * Simulation->MovementSpeed * DeltaSeconds);
    FVector Location = Owner->GetActorLocation();
    const FVector Center = Simulation->GetActorLocation();
    if (Location.X > Center.X + Simulation->WorldRadius) Location.X = Center.X - Simulation->WorldRadius;
    if (Location.X < Center.X - Simulation->WorldRadius) Location.X = Center.X + Simulation->WorldRadius;
    if (Location.Y > Center.Y + Simulation->WorldRadius) Location.Y = Center.Y - Simulation->WorldRadius;
    if (Location.Y < Center.Y - Simulation->WorldRadius) Location.Y = Center.Y + Simulation->WorldRadius;
    Owner->SetActorLocation(Location);
}
