#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AntAdaptationSimulationActor.h"
#include "AntAdaptationAntActor.generated.h"

class AAntAdaptationNestActor;
class AAntAdaptationFlowerActor;
class UAntAdaptationMovementComponent;

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AAntAdaptationAntActor : public AActor
{
    GENERATED_BODY()

public:
    AAntAdaptationAntActor();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ant Adaptation|Components") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Ant Adaptation|Components") TObjectPtr<UAntAdaptationMovementComponent> Movement;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") EAntAdaptationTeam Team = EAntAdaptationTeam::Blue;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") TObjectPtr<AAntAdaptationNestActor> HomeNest;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") float Energy = 2000.f;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") int32 Age = 0;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") bool bHasFood = false;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") bool bFighting = false;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") bool bWinged = false;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") float AntSize = 6.f;
    void Initialize(AAntAdaptationNestActor* Nest, float InEnergy, float InSize);
    void SimulateStep(AAntAdaptationSimulationActor* Simulation, float DeltaSeconds);
};
