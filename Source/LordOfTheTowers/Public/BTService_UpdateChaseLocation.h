#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_UpdateChaseLocation.generated.h"

/**
 * Writes the Blackboard TargetActor's current location into ChaseLocation
 * every tick (Interval), so UBTTask_MoveToBBLocation can keep homing on a
 * moving target during the Chase branch.
 */
UCLASS()
class LORDOFTHETOWERS_API UBTService_UpdateChaseLocation : public UBTService
{
    GENERATED_BODY()

public:
    UBTService_UpdateChaseLocation();

protected:
    virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
