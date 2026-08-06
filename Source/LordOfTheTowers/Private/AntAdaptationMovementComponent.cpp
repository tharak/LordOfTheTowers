#include "AntAdaptationMovementComponent.h"
#include "AntAdaptationSimulationActor.h"
#include "GameFramework/Actor.h"

void UAntAdaptationMovementComponent::MoveAndWrap(AAntAdaptationSimulationActor* Simulation, const FVector& Direction, float Speed, float DeltaSeconds)
{
    AActor* Owner = GetOwner();
    if (!Owner || !Simulation) return;
    Owner->AddActorWorldOffset(Direction.GetSafeNormal() * Speed * DeltaSeconds, true);
    FVector Location = Owner->GetActorLocation();
    const FVector Center = Simulation->GetActorLocation();
    if (Location.X > Center.X + Simulation->WorldRadius) Location.X = Center.X - Simulation->WorldRadius;
    if (Location.X < Center.X - Simulation->WorldRadius) Location.X = Center.X + Simulation->WorldRadius;
    if (Location.Y > Center.Y + Simulation->WorldRadius) Location.Y = Center.Y - Simulation->WorldRadius;
    if (Location.Y < Center.Y - Simulation->WorldRadius) Location.Y = Center.Y + Simulation->WorldRadius;
    Owner->SetActorLocation(Location);
}
