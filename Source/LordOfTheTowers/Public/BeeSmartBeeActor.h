#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BeeSmartBeeActor.generated.h"

class ABeeSmartHiveActor;
class UBeeDanceComponent;
class UBeeMovementComponent;

UENUM(BlueprintType)
enum class EBeeSmartOOPState : uint8
{
    Idle,
    InitialExplore,
    WatchDance,
    InspectHive,
    GoHome,
    Dance,
    Revisit,
    Pipe,
    MigratingToHive,
    Finished
};

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API ABeeSmartBeeActor : public AActor
{
    GENERATED_BODY()

public:
    ABeeSmartBeeActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BeeSmart|Components")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BeeSmart|Components")
    TObjectPtr<UBeeMovementComponent> Movement;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="BeeSmart|Components")
    TObjectPtr<UBeeDanceComponent> Dance;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    EBeeSmartOOPState State = EBeeSmartOOPState::Idle;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    TObjectPtr<ABeeSmartHiveActor> CurrentHive;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    bool bInitialScout = false;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|State")
    float Interest = 0.f;

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Bee")
    void InitializeBee(const FVector& HomeLocation, const TArray<ABeeSmartHiveActor*>& AvailableHives, bool bIsInitialScout);

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Bee")
    void StartPiping();

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Bee")
    void StartMigrationToHive(ABeeSmartHiveActor* WinningHive);

    UFUNCTION(BlueprintPure, Category="BeeSmart|Bee")
    bool IsPiping() const;

    UFUNCTION(BlueprintPure, Category="BeeSmart|Bee")
    EBeeSmartOOPState GetBeeState() const { return State; }

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Tuning")
    float InitialExploreTime = 40.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BeeSmart|Tuning")
    float SiteDetectionRadius = 110.f;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    UPROPERTY()
    TArray<TObjectPtr<ABeeSmartHiveActor>> AvailableHives;

    FVector HomeLocation = FVector::ZeroVector;
    float StateTime = 0.f;
    FRandomStream Random;
    bool bInitialized = false;
    bool bHasInspectedCurrentHive = false;
    bool bCommittedToCurrentHive = false;

    void SetState(EBeeSmartOOPState NewState);
    void SetCurrentHive(ABeeSmartHiveActor* Hive);
    void UpdateState(float DeltaSeconds);
    ABeeSmartHiveActor* FindHiveAtLocation(const FVector& Location) const;
    ABeeSmartHiveActor* ChooseAdvertisedHive() const;
    void BeginInspect(ABeeSmartHiveActor* Hive);

    UFUNCTION()
    void HandleMovementArrived();

    UFUNCTION()
    void HandleDanceFinished();
};
