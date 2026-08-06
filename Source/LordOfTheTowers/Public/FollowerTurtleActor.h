#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FollowerTurtleActor.generated.h"

class AFollowerSimulationActor;
class UFollowerMovementComponent;

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AFollowerTurtleActor : public AActor
{
    GENERATED_BODY()

public:
    AFollowerTurtleActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Follower|Components") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Follower|Components") TObjectPtr<UFollowerMovementComponent> Movement;

    UPROPERTY(BlueprintReadOnly, Category="Follower") TObjectPtr<AFollowerTurtleActor> Leader;
    UPROPERTY(BlueprintReadOnly, Category="Follower") TObjectPtr<AFollowerTurtleActor> Follower;

    void Initialize();
    void TurnAndMove(AFollowerSimulationActor* Simulation, float DeltaSeconds);
};
