#include "BeeMovementComponent.h"

#include "GameFramework/Actor.h"

UBeeMovementComponent::UBeeMovementComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickInterval = 0.016f;
}

void UBeeMovementComponent::BeginPlay()
{
    Super::BeginPlay();
    CurrentVelocity = FVector::ZeroVector;
}

void UBeeMovementComponent::MoveTo(const FVector& Target)
{
    TargetLocation = Target;
    bMovingToTarget = true;
    bFreeMoving = false;
    CurrentVelocity = FVector::ZeroVector;
}

void UBeeMovementComponent::SetFreeVelocity(const FVector& Velocity)
{
    CurrentVelocity = Velocity;
    bFreeMoving = true;
    bMovingToTarget = false;
}

void UBeeMovementComponent::StopMovement()
{
    CurrentVelocity = FVector::ZeroVector;
    bFreeMoving = false;
    bMovingToTarget = false;
}

void UBeeMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    AActor* Owner = GetOwner();
    if (!Owner) return;

    if (bMovingToTarget)
    {
        const FVector OldLocation = Owner->GetActorLocation();
        const FVector NewLocation = FMath::VInterpConstantTo(OldLocation, TargetLocation, DeltaTime, Speed);
        Owner->SetActorLocation(NewLocation);
        CurrentVelocity = (NewLocation - OldLocation) / FMath::Max(DeltaTime, KINDA_SMALL_NUMBER);
        if (FVector::DistSquared(NewLocation, TargetLocation) < FMath::Square(25.f))
        {
            Owner->SetActorLocation(TargetLocation);
            StopMovement();
            OnArrived.Broadcast();
        }
    }
    else if (bFreeMoving)
    {
        Owner->SetActorLocation(Owner->GetActorLocation() + CurrentVelocity * DeltaTime);
    }
}
