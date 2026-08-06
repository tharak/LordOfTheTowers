#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AntAdaptationMovementComponent.generated.h"

class AAntAdaptationSimulationActor;

UCLASS(ClassGroup=(AntAdaptation), Blueprintable, meta=(BlueprintSpawnableComponent))
class LORDOFTHETOWERS_API UAntAdaptationMovementComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation|Movement")
    void MoveAndWrap(AAntAdaptationSimulationActor* Simulation, const FVector& Direction, float Speed, float DeltaSeconds);
};
