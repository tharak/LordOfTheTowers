#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BeeDanceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBeeDanceFinished);

UCLASS(ClassGroup=(BeeSmart), Blueprintable, meta=(BlueprintSpawnableComponent))
class LORDOFTHETOWERS_API UBeeDanceComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBeeDanceComponent();

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Dance")
    bool bDancing = false;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Dance")
    float DanceQuality = 0.f;

    UPROPERTY(BlueprintReadOnly, Category="BeeSmart|Dance")
    float RemainingDanceTime = 0.f;

    UPROPERTY(BlueprintAssignable, Category="BeeSmart|Dance")
    FBeeDanceFinished OnDanceFinished;

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Dance")
    void StartDance(float Quality);

    UFUNCTION(BlueprintCallable, Category="BeeSmart|Dance")
    void StopDance();

protected:
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
};
