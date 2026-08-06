#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "FollowerSimulationActor.generated.h"

class AFollowerTurtleActor;

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AFollowerSimulationActor : public AActor
{
    GENERATED_BODY()

public:
    AFollowerSimulationActor();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup", meta=(ClampMin="0", ClampMax="10000")) int32 Population = 1500;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup", meta=(ClampMin="0")) float NearRadius = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup", meta=(ClampMin="0.1")) float FarRadius = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup", meta=(ClampMin="0")) float Waver = 70.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup") float WorldRadius = 2400.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Setup") float MovementSpeed = 80.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Follower|Classes") TSubclassOf<AFollowerTurtleActor> TurtleClass;
    UPROPERTY(BlueprintReadOnly, Category="Follower|Runtime") TArray<TObjectPtr<AFollowerTurtleActor>> Turtles;
    UPROPERTY(BlueprintReadOnly, Category="Follower|Runtime") int32 UnattachedCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Follower|Runtime") int32 HeadCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Follower|Runtime") int32 BodyCount = 0;
    UPROPERTY(BlueprintReadOnly, Category="Follower|Runtime") int32 TailCount = 0;

    UFUNCTION(BlueprintCallable, Category="Follower") void SetupFollower(int32 Seed = 0);
    UFUNCTION(BlueprintCallable, Category="Follower") void StartFollower();
    UFUNCTION(BlueprintCallable, Category="Follower") void StopFollower();

    void RemoveTurtle(AFollowerTurtleActor* Turtle);
    AFollowerTurtleActor* FindAttachCandidate(const AFollowerTurtleActor* Turtle, const FVector& CandidateLocation) const;

protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

private:
    FRandomStream Random;
    bool bRunning = false;
    void AttachTurtles();
    void MoveTurtles(float DeltaSeconds);
    void UpdateCounts();
};
