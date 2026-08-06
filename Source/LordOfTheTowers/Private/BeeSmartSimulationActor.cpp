#include "BeeSmartSimulationActor.h"

#include "DrawDebugHelpers.h"

ABeeSmartSimulationActor::ABeeSmartSimulationActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

void ABeeSmartSimulationActor::BeginPlay()
{
    Super::BeginPlay();
    SetupSimulation(FMath::Rand());
    StartSimulation();
}

void ABeeSmartSimulationActor::SetupSimulation(int32 Seed)
{
    Random.Initialize(Seed == 0 ? 12345 : Seed);
    SimulationTime = 0.f;
    WinningSiteIndex = INDEX_NONE;
    bFinished = false;
    bRunning = false;

    Sites.Reset();
    Scouts.Reset();
    Sites.Reserve(HiveCount);
    Scouts.Reserve(ScoutCount);

    for (int32 Index = 0; Index < HiveCount; ++Index)
    {
        const FVector SiteDirection = Random.GetUnitVector();
        const FVector2D Direction = FVector2D(SiteDirection.X, SiteDirection.Y).GetSafeNormal();
        const FVector2D Offset = Direction * Random.FRandRange(WorldRadius * 0.35f, WorldRadius);
        FBeeSmartSite& Site = Sites.AddDefaulted_GetRef();
        Site.Location = GetActorLocation() + FVector(Offset.X, Offset.Y, 40.f);
        Site.Quality = Random.FRandRange(0.25f, 1.f);
    }

    const int32 InitialScouts = FMath::Clamp(FMath::RoundToInt(ScoutCount * InitialPercentage), 1, ScoutCount);
    for (int32 Index = 0; Index < ScoutCount; ++Index)
    {
        FBeeSmartScout& Scout = Scouts.AddDefaulted_GetRef();
        Scout.Location = GetActorLocation() + FVector(0.f, 0.f, 180.f) + Random.GetUnitVector() * Random.FRandRange(0.f, 100.f);
        Scout.Velocity = Random.GetUnitVector() * 180.f;
        Scout.bInitialScout = Index < InitialScouts;
        Scout.State = Scout.bInitialScout ? EBeeSmartState::InitialExplore : EBeeSmartState::Idle;
        Scout.StateTime = 0.f;
    }
}

void ABeeSmartSimulationActor::StartSimulation()
{
    if (!bFinished) bRunning = true;
}

void ABeeSmartSimulationActor::StopSimulation()
{
    bRunning = false;
}

int32 ABeeSmartSimulationActor::GetPipingCount() const
{
    return Scouts.FilterByPredicate([](const FBeeSmartScout& Scout)
    {
        return Scout.State == EBeeSmartState::Pipe || Scout.State == EBeeSmartState::Finished;
    }).Num();
}

void ABeeSmartSimulationActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bRunning && !bFinished)
    {
        StepSimulation(DeltaSeconds * FMath::Max(0.f, SimulationSpeed));
    }
    DrawSimulation();
}

void ABeeSmartSimulationActor::StepSimulation(float DeltaSeconds)
{
    SimulationTime += DeltaSeconds;
    for (FBeeSmartScout& Scout : Scouts) UpdateScout(Scout, DeltaSeconds);
    TryReachQuorum();

    if (WinningSiteIndex != INDEX_NONE && GetPipingCount() == Scouts.Num())
    {
        bFinished = true;
        bRunning = false;
        for (FBeeSmartScout& Scout : Scouts) EnterState(Scout, EBeeSmartState::Finished);
    }
}

void ABeeSmartSimulationActor::UpdateScout(FBeeSmartScout& Scout, float DeltaSeconds)
{
    Scout.StateTime += DeltaSeconds;
    const FVector Home = GetActorLocation() + FVector(0.f, 0.f, 180.f);

    switch (Scout.State)
    {
    case EBeeSmartState::Idle:
        if (Scout.StateTime > 0.25f && Random.FRand() < DeltaSeconds * 0.20f)
        {
            const int32 AdvertisedSite = BestAdvertisedSite();
            if (AdvertisedSite != INDEX_NONE)
            {
                Scout.SiteIndex = AdvertisedSite;
                EnterState(Scout, EBeeSmartState::WatchDance);
            }
        }
        break;
    case EBeeSmartState::InitialExplore:
        Scout.Location += Scout.Velocity * DeltaSeconds;
        if (FVector::Dist2D(Scout.Location, Home) > WorldRadius) Scout.Velocity = (Home - Scout.Location).GetSafeNormal2D() * 180.f;
        if (FindSiteAt(Scout.Location) != INDEX_NONE) { Scout.SiteIndex = FindSiteAt(Scout.Location); EnterState(Scout, EBeeSmartState::InspectHive); }
        else if (Scout.StateTime >= InitialExploreTime) EnterState(Scout, EBeeSmartState::GoHome);
        break;
    case EBeeSmartState::WatchDance:
        if (Scout.StateTime > 0.5f + Random.FRandRange(0.f, 2.f)) EnterState(Scout, EBeeSmartState::Revisit);
        break;
    case EBeeSmartState::InspectHive:
        if (Scout.SiteIndex >= 0 && Scout.SiteIndex < Sites.Num()) Sites[Scout.SiteIndex].OnSite++;
        EnterState(Scout, EBeeSmartState::GoHome);
        break;
    case EBeeSmartState::GoHome:
        Scout.Location = FMath::VInterpConstantTo(Scout.Location, Home, DeltaSeconds, 260.f);
        if (FVector::Dist2D(Scout.Location, Home) < 35.f) EnterState(Scout, EBeeSmartState::Dance);
        break;
    case EBeeSmartState::Dance:
        if (Scout.SiteIndex >= 0 && Scout.SiteIndex < Sites.Num())
        {
            const float DanceDuration = 1.5f + Sites[Scout.SiteIndex].Quality * 5.f;
            if (Scout.StateTime >= DanceDuration) EnterState(Scout, EBeeSmartState::Revisit);
        }
        break;
    case EBeeSmartState::Revisit:
        Scout.Interest = FMath::Max(0.f, Scout.Interest - DeltaSeconds * 0.08f);
        if (Scout.SiteIndex >= 0 && Scout.SiteIndex < Sites.Num() && Random.FRand() < DeltaSeconds * (0.08f + Sites[Scout.SiteIndex].Quality * 0.18f))
        {
            Sites[Scout.SiteIndex].Committed++;
            EnterState(Scout, EBeeSmartState::InspectHive);
        }
        else if (Scout.StateTime > 2.f) EnterState(Scout, EBeeSmartState::Idle);
        break;
    case EBeeSmartState::Pipe:
        break;
    default:
        break;
    }
}

void ABeeSmartSimulationActor::EnterState(FBeeSmartScout& Scout, EBeeSmartState NewState)
{
    Scout.State = NewState;
    Scout.StateTime = 0.f;
    if (NewState == EBeeSmartState::Dance) Scout.Interest = Sites.IsValidIndex(Scout.SiteIndex) ? Sites[Scout.SiteIndex].Quality : 0.f;
}

int32 ABeeSmartSimulationActor::FindSiteAt(const FVector& Location) const
{
    for (int32 Index = 0; Index < Sites.Num(); ++Index)
        if (FVector::Dist2D(Location, Sites[Index].Location) < 110.f) return Index;
    return INDEX_NONE;
}

int32 ABeeSmartSimulationActor::BestAdvertisedSite()
{
    int32 Best = INDEX_NONE;
    float Score = 0.f;
    for (int32 Index = 0; Index < Sites.Num(); ++Index)
    {
        const float Candidate = Sites[Index].Quality * (1.f + Sites[Index].Committed * 0.05f) * Random.FRand();
        if (Candidate > Score) { Score = Candidate; Best = Index; }
    }
    return Best;
}

void ABeeSmartSimulationActor::TryReachQuorum()
{
    if (WinningSiteIndex != INDEX_NONE) return;
    for (int32 Index = 0; Index < Sites.Num(); ++Index)
    {
        if (Sites[Index].OnSite + Sites[Index].Committed >= Quorum)
        {
            WinningSiteIndex = Index;
            for (FBeeSmartScout& Scout : Scouts)
                if (Random.FRand() < 0.35f || Scout.SiteIndex == WinningSiteIndex) EnterState(Scout, EBeeSmartState::Pipe);
            return;
        }
    }
}

void ABeeSmartSimulationActor::DrawSimulation() const
{
    UWorld* World = GetWorld();
    if (!World) return;
    constexpr float DebugLifetime = 0.06f;
    DrawDebugSphere(World, GetActorLocation() + FVector(0.f, 0.f, 180.f), 150.f, 16, FColor::Yellow, false, DebugLifetime, 0, 2.f);
    for (int32 Index = 0; Index < Sites.Num(); ++Index)
    {
        const FColor Color = Index == WinningSiteIndex ? FColor::Green : FColor(255, FMath::RoundToInt(80.f + Sites[Index].Quality * 175.f), 40);
        DrawDebugSphere(World, Sites[Index].Location, 80.f + Sites[Index].Quality * 70.f, 16, Color, false, DebugLifetime, 0, 3.f);
        DrawDebugString(World, Sites[Index].Location + FVector(0, 0, 100), FString::Printf(TEXT("Q %.2f  C %d  On %d"), Sites[Index].Quality, Sites[Index].Committed, Sites[Index].OnSite), nullptr, Color, DebugLifetime, false);
    }
    for (const FBeeSmartScout& Scout : Scouts)
    {
        const FColor Color = Scout.State == EBeeSmartState::Pipe ? FColor::Red : FColor::White;
        DrawDebugPoint(World, Scout.Location, 10.f, Color, false, DebugLifetime, 0);
    }
}
