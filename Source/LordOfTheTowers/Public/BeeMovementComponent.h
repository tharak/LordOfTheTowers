#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BeeMovementComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBeeMovementArrived);

UCLASS(ClassGroup=(BeeSmart), Blueprintable, meta=(BlueprintSpawnableComponent))
class LORDOFTHETOWERS_API UBeeMovementComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBeeMovementComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Movement")
    float Speed = 260.f;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Movement")
    bool bMovingToTarget = false;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Movement")
    FVector CurrentVelocity = FVector::ZeroVector;

    UPROPERTY(BlueprintAssignable, Category="BeeSmart|Movement")
    FBeeMovementArrived OnArrived;

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Movement")
    void MoveTo(const FVector& Target);

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Movement")
    void SetFreeVelocity(const FVector& Velocity);

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Movement")
    void StopMovement();

protected:
    virtual void BeginPlay() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
    FVector TargetLocation = FVector::ZeroVector;
    bool bFreeMoving = false;
};
