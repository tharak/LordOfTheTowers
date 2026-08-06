#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeeSmartOOPSimulationActor.generated.h"

class ABeeSmartBeeActor;
class ABeeSmartHiveActor;

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API ABeeSmartOOPSimulationActor : public AActor
{
    GENERATED_BODY()

public:
    ABeeSmartOOPSimulationActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1", ClampMax="500"))
    int32 BeeCount = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1", ClampMax="20"))
    int32 HiveCount = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="0.01", ClampMax="1.0"))
    float InitialScoutPercentage = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup")
    float WorldRadius = 2400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup")
    int32 Quorum = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup")
    float SimulationSpeed = 1.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Classes")
    TSubclassOf<ABeeSmartBeeActor> BeeClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Classes")
    TSubclassOf<ABeeSmartHiveActor> HiveClass;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Runtime")
    TArray<TObjectPtr<ABeeSmartBeeActor>> Bees;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Runtime")
    TArray<TObjectPtr<ABeeSmartHiveActor>> Hives;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Runtime")
    int32 WinningHiveIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Runtime")
    bool bFinished = false;

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Simulation")
    void SetupOOPSimulation(int32 Seed = 0);

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Simulation")
    void StartOOPSimulation();

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Simulation")
    void StopOOPSimulation();

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    FRandomStream Random;
    bool bRunning = false;
    bool bMigrationStarted = false;
    FVector HomeLocation = FVector::ZeroVector;

    void CheckForQuorum();
    void PropagatePiping(float DeltaSeconds);
    void DestroySpawnedAgents();
};
