#include "BTTask_FireAtTarget.h"

#include "EnemyAIBlackboardKeys.h"
#include "EnemyPawn.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTTask_FireAtTarget::UBTTask_FireAtTarget()
{
    NodeName = TEXT("Fire At Target");
}

EBTNodeResult::Type UBTTask_FireAtTarget::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    AEnemyPawn* Pawn = OwnerComp.GetAIOwner() ? Cast<AEnemyPawn>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;

    if (!BB || !Pawn || !Cast<AActor>(BB->GetValueAsObject(EnemyAIBlackboardKeys::TargetActor)))
    {
        return EBTNodeResult::Failed;
    }

    Pawn->FireWeapon();
    return EBTNodeResult::Succeeded;
}
