#include "AntAdaptationAntActor.h"
#include "AntAdaptationFlowerActor.h"
#include "AntAdaptationNestActor.h"
#include "DrawDebugHelpers.h"
#include "AntAdaptationMovementComponent.h"

AAntAdaptationAntActor::AAntAdaptationAntActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    Movement = CreateDefaultSubobject<UAntAdaptationMovementComponent>(TEXT("Movement"));
}

void AAntAdaptationAntActor::Initialize(AAntAdaptationNestActor* Nest, float InEnergy, float InSize)
{
    HomeNest = Nest;
    Team = Nest ? Nest->Team : EAntAdaptationTeam::Blue;
    Energy = InEnergy;
    AntSize = InSize;
    Age = 0;
    bHasFood = false;
    bFighting = false;
    bWinged = false;
}

void AAntAdaptationAntActor::SimulateStep(AAntAdaptationSimulationActor* Simulation, float DeltaSeconds)
{
    if (!Simulation || bWinged) return;
    const float Speed = (70.f + AntSize * 9.f) * DeltaSeconds;
    const FVector Home = HomeNest ? HomeNest->GetActorLocation() : FVector::ZeroVector;
    const float HomeDistance = FVector::Dist2D(GetActorLocation(), Home);

    if (bHasFood)
    {
        if (HomeDistance < 140.f)
        {
            bHasFood = false;
            if (HomeNest) HomeNest->FoodStore += 1.f;
            SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + 180.f, 0.f));
        }
        else
        {
            Simulation->AddChemicalAt(GetActorLocation(), 60.f * DeltaSeconds * 30.f);
            SetActorRotation((Home - GetActorLocation()).Rotation());
        }
    }
    else
    {
        bool bFoundFood = false;
        for (AAntAdaptationFlowerActor* Flower : Simulation->Flowers)
        {
            if (Flower && FVector::DistSquared2D(GetActorLocation(), Flower->GetActorLocation()) < FMath::Square(80.f))
            {
                bFoundFood = Flower->Harvest();
                if (bFoundFood)
                {
                    Energy += 1000.f;
                    bHasFood = true;
                    SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + 180.f, 0.f));
                }
                break;
            }
        }
        if (!bFoundFood)
        {
            const FVector ChemicalDirection = Simulation->GetChemicalDirection(GetActorLocation(), GetActorForwardVector());
            if (!ChemicalDirection.IsNearlyZero()) SetActorRotation(ChemicalDirection.Rotation());
            else SetActorRotation(FRotator(0.f, GetActorRotation().Yaw + FMath::FRandRange(-35.f, 35.f), 0.f));
        }
    }

    Movement->MoveAndWrap(Simulation, GetActorForwardVector(), Speed, DeltaSeconds);

    for (AAntAdaptationAntActor* Other : Simulation->Ants)
    {
        if (!Other || Other == this || Other->Team == Team || Other->bWinged) continue;
        if (FVector::DistSquared2D(GetActorLocation(), Other->GetActorLocation()) < FMath::Square(55.f))
        {
            bFighting = true;
            Other->bFighting = true;
            const float Aggression = Simulation->GetAggression(Team);
            if (FMath::FRand() < DeltaSeconds * Aggression * 0.01f)
            {
                const bool bWin = FMath::FRand() < FMath::Clamp(AntSize / FMath::Max(1.f, Other->AntSize), 0.1f, 0.9f);
                if (bWin) Other->Energy -= 250.f;
                else Energy -= 250.f;
                Simulation->AddChemicalAt(GetActorLocation(), 15.f);
            }
        }
    }

    Energy -= (5.f - (1.f - Simulation->GetAggression(Team) / 100.f)) * DeltaSeconds * 30.f;
    if (Energy <= 0.f) Simulation->RemoveAnt(this);

    DrawDebugPoint(GetWorld(), GetActorLocation() + FVector(0.f, 0.f, 35.f), 12.f,
        Team == EAntAdaptationTeam::Blue ? FColor::Cyan : FColor::Red, false, 0.08f, 0);
}
