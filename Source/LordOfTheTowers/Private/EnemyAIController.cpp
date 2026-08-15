#include "EnemyAIController.h"

#include "EnemyAIBlackboardKeys.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionSystem.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISense_Sight.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/GameplayStatics.h"

AEnemyAIController::AEnemyAIController()
{
    bStartAILogicOnPossess = true;

    UAIPerceptionComponent* Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
    SetPerceptionComponent(*Perception);

    SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    SightConfig->SightRadius = 1500.f;
    SightConfig->LoseSightRadius = 1800.f;
    SightConfig->PeripheralVisionAngleDegrees = 90.f;
    SightConfig->DetectionByAffiliation.bDetectEnemies = true;
    SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
    SightConfig->DetectionByAffiliation.bDetectFriendlies = false;

    Perception->ConfigureSense(*SightConfig);
    Perception->SetDominantSense(SightConfig->GetSenseImplementation());

    SetGenericTeamId(FGenericTeamId(1));
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (BehaviorTreeAsset)
    {
        RunBehaviorTree(BehaviorTreeAsset);
    }

    if (UAIPerceptionComponent* Perception = GetAIPerceptionComponent())
    {
        Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnTargetPerceptionUpdated);
    }

    if (UBlackboardComponent* BB = GetBlackboardComponent())
    {
        if (InPawn)
        {
            BB->SetValueAsVector(EnemyAIBlackboardKeys::HomeLocation, InPawn->GetActorLocation());
        }
    }

    // The player pawn has no UAIPerceptionStimuliSourceComponent (and we don't
    // modify BP_PawnPlayer to add one), so sight perception has nothing to
    // detect unless we register it as a source explicitly.
    if (UAIPerceptionSystem* PerceptionSystem = UAIPerceptionSystem::GetCurrent(GetWorld()))
    {
        if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0))
        {
            PerceptionSystem->RegisterSource(*PlayerPawn);
        }
    }
}

void AEnemyAIController::OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus)
{
    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    if (Stimulus.WasSuccessfullySensed())
    {
        BB->SetValueAsObject(EnemyAIBlackboardKeys::TargetActor, Actor);
    }
    else
    {
        // No investigate/search phase in v1 — drop the target immediately and
        // let the Behavior Tree fall back to Patrol.
        BB->ClearValue(EnemyAIBlackboardKeys::TargetActor);
    }
}
