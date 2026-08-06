#include "BeeSmartBeeActor.h"

#include "BeeDanceComponent.h"
#include "BeeMovementComponent.h"
#include "BeeSmartHiveActor.h"
#include "DrawDebugHelpers.h"

ABeeSmartBeeActor::ABeeSmartBeeActor()
{
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;

    Movement = CreateDefaultSubobject<UBeeMovementComponent>(TEXT("Movement"));
    Dance = CreateDefaultSubobject<UBeeDanceComponent>(TEXT("Dance"));
}

void ABeeSmartBeeActor::BeginPlay()
{
    Super::BeginPlay();
    Movement->OnArrived.AddDynamic(this, &ABeeSmartBeeActor::HandleMovementArrived);
    Dance->OnDanceFinished.AddDynamic(this, &ABeeSmartBeeActor::HandleDanceFinished);
}

void ABeeSmartBeeActor::InitializeBee(const FVector& InHomeLocation, const TArray<ABeeSmartHiveActor*>& InAvailableHives, bool bIsInitialScout)
{
    HomeLocation = InHomeLocation;
    AvailableHives.Reset();
    for (ABeeSmartHiveActor* Hive : InAvailableHives) AvailableHives.Add(Hive);
    bInitialScout = bIsInitialScout;
    SetCurrentHive(nullptr);
    Random.Initialize(GetUniqueID());
    bInitialized = true;
    SetState(bInitialScout ? EBeeSmartOOPState::InitialExplore : EBeeSmartOOPState::Idle);
    if (bInitialScout) Movement->SetFreeVelocity(Random.GetUnitVector() * 180.f);
}

void ABeeSmartBeeActor::StartPiping()
{
    Movement->StopMovement();
    Dance->StopDance();
    SetState(EBeeSmartOOPState::Pipe);
}

void ABeeSmartBeeActor::StartMigrationToHive(ABeeSmartHiveActor* WinningHive)
{
    if (!WinningHive) return;
    SetCurrentHive(WinningHive);
    Dance->StopDance();
    Movement->MoveTo(WinningHive->GetActorLocation());
    SetState(EBeeSmartOOPState::MigratingToHive);
}

bool ABeeSmartBeeActor::IsPiping() const
{
    return State == EBeeSmartOOPState::Pipe;
}

void ABeeSmartBeeActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bInitialized && State != EBeeSmartOOPState::Finished) UpdateState(DeltaSeconds);

    const FColor Color = State == EBeeSmartOOPState::Pipe ? FColor::Red :
        (State == EBeeSmartOOPState::MigratingToHive ? FColor::Yellow :
        (State == EBeeSmartOOPState::Finished ? FColor::Green : FColor::White));
    DrawDebugPoint(GetWorld(), GetActorLocation(), 12.f, Color, false, 0.06f, 0);
}

void ABeeSmartBeeActor::SetState(EBeeSmartOOPState NewState)
{
    State = NewState;
    StateTime = 0.f;
}

void ABeeSmartBeeActor::SetCurrentHive(ABeeSmartHiveActor* Hive)
{
    if (CurrentHive != Hive)
    {
        CurrentHive = Hive;
        bHasInspectedCurrentHive = false;
        bCommittedToCurrentHive = false;
    }
    else if (!Hive)
    {
        bHasInspectedCurrentHive = false;
        bCommittedToCurrentHive = false;
    }
}

void ABeeSmartBeeActor::UpdateState(float DeltaSeconds)
{
    StateTime += DeltaSeconds;

    switch (State)
    {
    case EBeeSmartOOPState::InitialExplore:
        if (ABeeSmartHiveActor* Hive = FindHiveAtLocation(GetActorLocation())) BeginInspect(Hive);
        else if (StateTime >= InitialExploreTime) { Movement->MoveTo(HomeLocation); SetState(EBeeSmartOOPState::GoHome); }
        break;
    case EBeeSmartOOPState::Idle:
        if (StateTime > 0.25f && Random.FRand() < DeltaSeconds * 0.20f)
        {
            SetCurrentHive(ChooseAdvertisedHive());
            if (CurrentHive) SetState(EBeeSmartOOPState::WatchDance);
        }
        break;
    case EBeeSmartOOPState::WatchDance:
        if (StateTime > 0.5f + Random.FRandRange(0.f, 2.f)) SetState(EBeeSmartOOPState::Revisit);
        break;
    case EBeeSmartOOPState::Revisit:
        Interest = FMath::Max(0.f, Interest - DeltaSeconds * 0.08f);
        if (CurrentHive && Random.FRand() < DeltaSeconds * (0.08f + CurrentHive->Quality * 0.18f))
        {
            if (!bCommittedToCurrentHive)
            {
                CurrentHive->CommitBee();
                bCommittedToCurrentHive = true;
            }
            Movement->MoveTo(CurrentHive->GetActorLocation());
            SetState(EBeeSmartOOPState::InspectHive);
        }
        else if (StateTime > 2.f) SetState(EBeeSmartOOPState::Idle);
        break;
    case EBeeSmartOOPState::GoHome:
        if (FVector::DistSquared2D(GetActorLocation(), HomeLocation) < FMath::Square(35.f)) SetState(EBeeSmartOOPState::Dance);
        break;
    case EBeeSmartOOPState::Dance:
        if (!Dance->bDancing) Dance->StartDance(CurrentHive ? CurrentHive->Quality : Interest);
        break;
    case EBeeSmartOOPState::Pipe:
        break;
    case EBeeSmartOOPState::MigratingToHive:
        break;
    default:
        break;
    }
}

ABeeSmartHiveActor* ABeeSmartBeeActor::FindHiveAtLocation(const FVector& Location) const
{
    for (ABeeSmartHiveActor* Hive : AvailableHives)
        if (Hive && FVector::Dist2D(Location, Hive->GetActorLocation()) <= SiteDetectionRadius) return Hive;
    return nullptr;
}

ABeeSmartHiveActor* ABeeSmartBeeActor::ChooseAdvertisedHive() const
{
    ABeeSmartHiveActor* BestHive = nullptr;
    float BestScore = 0.f;
    for (ABeeSmartHiveActor* Hive : AvailableHives)
    {
        if (!Hive) continue;
        const float Score = Hive->Quality * (1.f + Hive->CommittedBees * 0.05f) * FMath::FRand();
        if (Score > BestScore) { BestScore = Score; BestHive = Hive; }
    }
    return BestHive;
}

void ABeeSmartBeeActor::BeginInspect(ABeeSmartHiveActor* Hive)
{
    SetCurrentHive(Hive);
    if (!CurrentHive) return;
    if (!bHasInspectedCurrentHive)
    {
        CurrentHive->EnterSite();
        bHasInspectedCurrentHive = true;
    }
    Interest = CurrentHive->Quality;
    Movement->MoveTo(HomeLocation);
    SetState(EBeeSmartOOPState::GoHome);
}

void ABeeSmartBeeActor::HandleMovementArrived()
{
    if (State == EBeeSmartOOPState::InspectHive)
    {
        BeginInspect(CurrentHive);
    }
    else if (State == EBeeSmartOOPState::MigratingToHive)
    {
        if (CurrentHive && !bCommittedToCurrentHive)
        {
            CurrentHive->CommitBee();
            bCommittedToCurrentHive = true;
        }
        SetState(EBeeSmartOOPState::Finished);
    }
}

void ABeeSmartBeeActor::HandleDanceFinished()
{
    if (State == EBeeSmartOOPState::Dance) SetState(EBeeSmartOOPState::Revisit);
}
