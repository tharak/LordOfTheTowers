#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AntAdaptationSimulationActor.h"
#include "AntAdaptationNestActor.generated.h"

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AAntAdaptationNestActor : public AActor
{
    GENERATED_BODY()

public:
    AAntAdaptationNestActor();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation") EAntAdaptationTeam Team = EAntAdaptationTeam::Blue;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") float FoodStore = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation") float AntSize = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation") float Aggression = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation") float StartEnergy = 2000.f;
    UFUNCTION(BlueprintPure, Category="Ant Adaptation") float GetCreateCost() const;

protected:
    virtual void Tick(float DeltaSeconds) override;
};
