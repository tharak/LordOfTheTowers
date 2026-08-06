#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeeSmartHiveActor.generated.h"

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API ABeeSmartHiveActor : public AActor
{
    GENERATED_BODY()

public:
    ABeeSmartHiveActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BeeSmart|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Hive", meta=(ClampMin="0.0", ClampMax="1.0"))
    float Quality = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Hive", meta=(ClampMin="1"))
    int32 Quorum = 12;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Hive")
    int32 BeesOnSite = 0;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Hive")
    int32 CommittedBees = 0;

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Hive")
    void EnterSite();

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Hive")
    void LeaveSite();

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Hive")
    void CommitBee();

    UFUNCTION(BlueprintPure, Category="BeeSmart|Hive")
    bool HasReachedQuorum() const;

protected:
    virtual void Tick(float DeltaSeconds) override;
};
