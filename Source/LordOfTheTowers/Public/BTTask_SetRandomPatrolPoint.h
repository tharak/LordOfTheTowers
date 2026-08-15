#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_SetRandomPatrolPoint.generated.h"

/**
 * Picks a random point within PatrolRadius of HomeLocation, clamped to the
 * arena bounds (the union of all placed ABlockingVolume actors, matching the
 * ScreenBounds* volumes in Main.umap), and writes it to PatrolLocation.
 */
UCLASS()
class LORDOFTHETOWERS_API UBTTask_SetRandomPatrolPoint : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_SetRandomPatrolPoint();

    UPROPERTY(EditAnywhere, Category="Patrol")
    float PatrolRadius = 800.f;

protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
