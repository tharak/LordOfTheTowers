#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AntAdaptationFlowerActor.generated.h"

UCLASS(Blueprintable)
class LORDOFTHETOWERS_API AAntAdaptationFlowerActor : public AActor
{
    GENERATED_BODY()

public:
    AAntAdaptationFlowerActor();
    UPROPERTY(BlueprintReadOnly, Category="Ant Adaptation") int32 Petals = 8;
    bool Harvest();

protected:
    virtual void Tick(float DeltaSeconds) override;
};
