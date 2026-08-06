#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AntAdaptationSimulationActor.generated.h"

class AAntAdaptationAntActor;
class AAntAdaptationFlowerActor;
class AAntAdaptationNestActor;

UENUM(BlueprintType)
enum class EAntAdaptationTeam : uint8
{
    Blue,
    Red
};

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AAntAdaptationSimulationActor : public AActor
{
    GENERATED_BODY()

public:
    AAntAdaptationSimulationActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") int32 InitialAntsPerNest = 10;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") int32 InitialFlowers = 45;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") float WorldRadius = 3800.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") float SimulationSpeed = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Classes") TSubclassOf<AAntAdaptationAntActor> AntClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Classes") TSubclassOf<AAntAdaptationFlowerActor> FlowerClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Classes") TSubclassOf<AAntAdaptationNestActor> NestClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") float EvaporationRate = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") int32 GridWidth = 77;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Setup") int32 GridHeight = 61;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Blue") float BlueSize = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Blue") float BlueAggression = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Blue") float BlueStartEnergy = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Red") float RedSize = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Red") float RedAggression = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Red") float RedStartEnergy = 2000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Runtime") TArray<TObjectPtr<AAntAdaptationAntActor>> Ants;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Runtime") TArray<TObjectPtr<AAntAdaptationFlowerActor>> Flowers;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Ant Adaptation|Runtime") TArray<TObjectPtr<AAntAdaptationNestActor>> Nests;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation|Runtime") int32 BlueAntCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation|Runtime") int32 RedAntCount = 0;

    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void SetupAntAdaptation(int32 Seed = 0);
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void StartAntAdaptation();
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void StopAntAdaptation();
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void AddFlowerAt(const FVector& Location);
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void AddPheromoneAt(const FVector& Location, float Amount = 60.f);
    UFUNCTION(BlueprintCallable, Category="Ant Adaptation") void ErasePheromoneAt(const FVector& Location);
    UFUNCTION(BlueprintPure, Category="Ant Adaptation") float GetChemicalAt(const FVector& Location) const;

    void StepAnt(AAntAdaptationAntActor* Ant, float DeltaSeconds);
    void RemoveAnt(AAntAdaptationAntActor* Ant);
    AAntAdaptationNestActor* GetNest(EAntAdaptationTeam Team) const;
    float GetStartEnergy(EAntAdaptationTeam Team) const;
    float GetAggression(EAntAdaptationTeam Team) const;
    float GetAntSize(EAntAdaptationTeam Team) const;
    FVector GetRandomPoint();
    FVector GetChemicalDirection(const FVector& Location, const FVector& Forward) const;
    void AddChemicalAt(const FVector& Location, float Amount);

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    FRandomStream Random;
    TArray<float> Chemical;
    bool bRunning = false;
    float SimulationTime = 0.f;
    int32 CellIndex(const FVector& Location) const;
    FVector CellLocation(int32 X, int32 Y) const;
    void SpawnAnt(AAntAdaptationNestActor* Nest);
    void Reproduce();
    void GrowFlowers();
    void DiffuseChemical(float DeltaSeconds);
};
