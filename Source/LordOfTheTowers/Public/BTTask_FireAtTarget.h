#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FireAtTarget.generated.h"

/**
 * Fires the possessed AEnemyPawn's weapon at the current TargetActor. Fire
 * rate is gated by a BTDecorator_Cooldown wrapping this node in the tree,
 * not inside this task (BP_ShooterComponent's own Cooldown variable isn't
 * enforced in its Shoot graph).
 */
UCLASS()
class LORDOFTHETOWERS_API UBTTask_FireAtTarget : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_FireAtTarget();

protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
