#include "BTService_UpdateChaseLocation.h"

#include "EnemyAIBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BehaviorTreeComponent.h"

UBTService_UpdateChaseLocation::UBTService_UpdateChaseLocation()
{
    NodeName = TEXT("Update Chase Location");
    Interval = 0.2f;
    RandomDeviation = 0.02f;
}

void UBTService_UpdateChaseLocation::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

    UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    if (AActor* Target = Cast<AActor>(BB->GetValueAsObject(EnemyAIBlackboardKeys::TargetActor)))
    {
        BB->SetValueAsVector(EnemyAIBlackboardKeys::ChaseLocation, Target->GetActorLocation());
    }
}
