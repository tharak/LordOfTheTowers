#include "BTTask_MoveToBBLocation.h"

#include "EnemyPawn.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTTask_MoveToBBLocation::UBTTask_MoveToBBLocation()
{
    NodeName = TEXT("Move To BB Location");
    INIT_TASK_NODE_NOTIFY_FLAGS();
    LocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_MoveToBBLocation, LocationKey));
}

uint16 UBTTask_MoveToBBLocation::GetInstanceMemorySize() const
{
    return sizeof(FMemory);
}

EBTNodeResult::Type UBTTask_MoveToBBLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    FMemory* Memory = reinterpret_cast<FMemory*>(NodeMemory);
    Memory->ElapsedTime = 0.f;
    return EBTNodeResult::InProgress;
}

void UBTTask_MoveToBBLocation::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    FMemory* Memory = reinterpret_cast<FMemory*>(NodeMemory);
    Memory->ElapsedTime += DeltaSeconds;

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    AEnemyPawn* Pawn = OwnerComp.GetAIOwner() ? Cast<AEnemyPawn>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr;

    if (!BB || !Pawn || !BB->IsVectorValueSet(LocationKey.SelectedKeyName))
    {
        FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
        return;
    }

    const FVector TargetLocation = BB->GetValueAsVector(LocationKey.SelectedKeyName);
    const FVector ToTarget = TargetLocation - Pawn->GetActorLocation();
    const float Distance = ToTarget.Size();

    if (Distance <= AcceptanceRadius)
    {
        Pawn->SetMoveDirection(FVector::ZeroVector);
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    if (Memory->ElapsedTime >= MaxDuration)
    {
        Pawn->SetMoveDirection(FVector::ZeroVector);
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    Pawn->SetMoveDirection(ToTarget / Distance);
}

EBTNodeResult::Type UBTTask_MoveToBBLocation::AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    if (AEnemyPawn* Pawn = OwnerComp.GetAIOwner() ? Cast<AEnemyPawn>(OwnerComp.GetAIOwner()->GetPawn()) : nullptr)
    {
        Pawn->SetMoveDirection(FVector::ZeroVector);
    }
    return EBTNodeResult::Aborted;
}
