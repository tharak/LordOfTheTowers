#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "EnemyPawn.generated.h"

/**
 * Thin native interface boundary for the AI enemy pawn. Movement and combat
 * logic live in the Blueprint (BP_EnemyAI) via BlueprintImplementableEvent,
 * so native Behavior Tree nodes can call typed functions instead of doing
 * reflection-based Blueprint property access.
 */
UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AEnemyPawn : public APawn
{
    GENERATED_BODY()

public:
    AEnemyPawn();

    UFUNCTION(BlueprintImplementableEvent, Category="AI|Movement")
    void SetMoveDirection(const FVector& Direction);

    UFUNCTION(BlueprintImplementableEvent, Category="AI|Combat")
    void FireWeapon();

    // Called when the AI acquires or loses its target, so the pawn can show a
    // visual "alert" indicator (e.g. an emissive glow) while it has one.
    UFUNCTION(BlueprintImplementableEvent, Category="AI|Perception")
    void SetAlertVisual(bool bAlert);

    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="AI")
    float AttackRange = 600.f;
};
