#include "BTDecorator_InAttackRange.h"

#include "EnemyAIBlackboardKeys.h"
#include "EnemyPawn.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTDecorator_InAttackRange::UBTDecorator_InAttackRange()
{
    NodeName = TEXT("In Attack Range");
}

bool UBTDecorator_InAttackRange::CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
    const UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    const AEnemyPawn* Pawn = OwnerComp.GetAIOwner() ? Cast<AEnemyPawn>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;
    const AActor* Target = BB ? Cast<AActor>(BB->GetValueAsObject(EnemyAIBlackboardKeys::TargetActor)) : nullptr;

    if (!Pawn || !Target)
    {
        return false;
    }

    return FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation()) <= Pawn->AttackRange;
}
