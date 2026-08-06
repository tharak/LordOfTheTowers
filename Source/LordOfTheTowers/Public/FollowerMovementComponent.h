#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FollowerMovementComponent.generated.h"

class AFollowerSimulationActor;

UCLASS(ClassGroup=(Follower), Blueprintable, meta=(BlueprintSpawnableComponent))
class LORDOFTHETOWERS_API UFollowerMovementComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Follower|Movement")
    void MoveAndWrap(AFollowerSimulationActor* Simulation, float DeltaSeconds);
};
