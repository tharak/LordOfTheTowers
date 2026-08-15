#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyAIController.generated.h"

class UAISenseConfig_Sight;
class UBehaviorTree;
struct FAIStimulus;

/**
 * AI controller for BP_EnemyAI. Drives a Behavior Tree (patrol/detect/chase/attack)
 * and feeds sight perception results into the Blackboard's TargetActor key.
 */
UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AEnemyAIController : public AAIController
{
    GENERATED_BODY()

public:
    AEnemyAIController();

    UPROPERTY(EditDefaultsOnly, Category="AI")
    TObjectPtr<UBehaviorTree> BehaviorTreeAsset;

protected:
    virtual void OnPossess(APawn* InPawn) override;

    UFUNCTION()
    void OnTargetPerceptionUpdated(AActor* Actor, FAIStimulus Stimulus);

private:
    UPROPERTY()
    TObjectPtr<UAISenseConfig_Sight> SightConfig;
};
