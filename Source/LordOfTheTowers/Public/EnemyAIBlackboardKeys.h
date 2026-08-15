#pragma once

#include "CoreMinimal.h"

// Shared Blackboard key names for the AI enemy's Behavior Tree (BB_Enemy).
// Centralized here so the controller, service, tasks, and decorator all
// agree on the same key names without repeating string literals.
namespace EnemyAIBlackboardKeys
{
    const FName TargetActor(TEXT("TargetActor"));
    const FName ChaseLocation(TEXT("ChaseLocation"));
    const FName PatrolLocation(TEXT("PatrolLocation"));
    const FName HomeLocation(TEXT("HomeLocation"));
}
