#include "AntAdaptationFlowerActor.h"
#include "DrawDebugHelpers.h"

AAntAdaptationFlowerActor::AAntAdaptationFlowerActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.1f;
}

bool AAntAdaptationFlowerActor::Harvest()
{
    if (Petals <= 0) return false;
    --Petals;
    if (Petals <= 0) Destroy();
    return true;
}

void AAntAdaptationFlowerActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    DrawDebugSphere(GetWorld(), GetActorLocation(), 30.f + Petals * 3.f, 8, FColor::Yellow, false, 0.12f, 0, 2.f);
}
