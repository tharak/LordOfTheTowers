#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_MoveToBBLocation.generated.h"

/**
 * Generic direct-steering mover, used by both the Patrol and Chase branches.
 * Re-reads LocationKey every tick and calls AEnemyPawn::SetMoveDirection with
 * the normalized direction toward it, instead of AAIController::MoveTo
 * (there is no NavMesh in this bounded-arena SHMUP).
 */
UCLASS()
class LORDOFTHETOWERS_API UBTTask_MoveToBBLocation : public UBTTaskNode
{
    GENERATED_BODY()

public:
    UBTTask_MoveToBBLocation();

    UPROPERTY(EditAnywhere, Category="Blackboard")
    FBlackboardKeySelector LocationKey;

    UPROPERTY(EditAnywhere, Category="Movement")
    float AcceptanceRadius = 75.f;

    UPROPERTY(EditAnywhere, Category="Movement")
    float MaxDuration = 8.f;

protected:
    virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
    virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
    virtual uint16 GetInstanceMemorySize() const override;

private:
    struct FMemory
    {
        float ElapsedTime = 0.f;
    };
};
