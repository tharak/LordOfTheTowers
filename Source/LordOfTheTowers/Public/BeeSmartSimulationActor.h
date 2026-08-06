#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeeSmartSimulationActor.generated.h"

UENUM(BlueprintType)
enum class EBeeSmartState : uint8
{
    Idle,
    InitialExplore,
    WatchDance,
    InspectHive,
    GoHome,
    Dance,
    Revisit,
    Pipe,
    Finished
};

USTRUCT(BlueprintType)
struct FBeeSmartSite
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) float Quality = 0.f;
    UPROPERTY(BlueprintReadOnly) int32 OnSite = 0;
    UPROPERTY(BlueprintReadOnly) int32 Committed = 0;
};

USTRUCT(BlueprintType)
struct FBeeSmartScout
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly) FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) FVector Velocity = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly) EBeeSmartState State = EBeeSmartState::Idle;
    UPROPERTY(BlueprintReadOnly) int32 SiteIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly) float Interest = 0.f;
    UPROPERTY(BlueprintReadOnly) float StateTime = 0.f;
    UPROPERTY(BlueprintReadOnly) bool bInitialScout = false;
};

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API ABeeSmartSimulationActor : public AActor
{
    GENERATED_BODY()

public:
    ABeeSmartSimulationActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1", ClampMax="500"))
    int32 ScoutCount = 100;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1", ClampMax="20"))
    int32 HiveCount = 5;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="0.01", ClampMax="1.0"))
    float InitialPercentage = 0.10f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1.0"))
    float InitialExploreTime = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup", meta=(ClampMin="1"))
    int32 Quorum = 12;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup")
    float WorldRadius = 2400.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Setup")
    float SimulationSpeed = 1.f;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    TArray<FBeeSmartSite> Sites;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    TArray<FBeeSmartScout> Scouts;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    bool bRunning = false;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    bool bFinished = false;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    int32 WinningSiteIndex = INDEX_NONE;

    UFUNCTION(BlueprintCallable, Category="BeeSmart")
    void SetupSimulation(int32 Seed = 0);

    UFUNCTION(BlueprintCallable, Category="BeeSmart")
    void StartSimulation();

    UFUNCTION(BlueprintCallable, Category="BeeSmart")
    void StopSimulation();

    UFUNCTION(BlueprintPure, Category="BeeSmart")
    int32 GetPipingCount() const;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    FRandomStream Random;
    float SimulationTime = 0.f;

    void StepSimulation(float DeltaSeconds);
    void UpdateScout(FBeeSmartScout& Scout, float DeltaSeconds);
    void EnterState(FBeeSmartScout& Scout, EBeeSmartState NewState);
    int32 FindSiteAt(const FVector& Location) const;
    int32 BestAdvertisedSite();
    void TryReachQuorum();
    void DrawSimulation() const;
};
