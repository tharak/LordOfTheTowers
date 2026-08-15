#include "BTTask_SetRandomPatrolPoint.h"

#include "EnemyAIBlackboardKeys.h"
#include "AIController.h"
#include "Engine/BlockingVolume.h"
#include "EngineUtils.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTTask_SetRandomPatrolPoint::UBTTask_SetRandomPatrolPoint()
{
    NodeName = TEXT("Set Random Patrol Point");
}

EBTNodeResult::Type UBTTask_SetRandomPatrolPoint::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    UWorld* World = OwnerComp.GetWorld();
    if (!BB || !World)
    {
        return EBTNodeResult::Failed;
    }

    const FVector Home = BB->GetValueAsVector(EnemyAIBlackboardKeys::HomeLocation);

    // Union of all placed blocking volumes (the ScreenBounds* arena walls) —
    // no hardcoded arena constants to go stale if the level changes.
    FBox ArenaBounds(EForceInit::ForceInit);
    for (TActorIterator<ABlockingVolume> It(World); It; ++It)
    {
        ArenaBounds += It->GetComponentsBoundingBox();
    }

    const FVector2D RandomOffset = FMath::RandPointInCircle(PatrolRadius);
    FVector Candidate = Home + FVector(RandomOffset.X, RandomOffset.Y, 0.f);

    if (ArenaBounds.IsValid)
    {
        Candidate = FVector(
            FMath::Clamp(Candidate.X, ArenaBounds.Min.X, ArenaBounds.Max.X),
            FMath::Clamp(Candidate.Y, ArenaBounds.Min.Y, ArenaBounds.Max.Y),
            Home.Z);
    }

    BB->SetValueAsVector(EnemyAIBlackboardKeys::PatrolLocation, Candidate);
    return EBTNodeResult::Succeeded;
}
