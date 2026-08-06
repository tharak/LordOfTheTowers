#include "BeeSmartOOPSimulationActor.h"

#include "BeeSmartBeeActor.h"
#include "BeeMovementComponent.h"
#include "BeeSmartHiveActor.h"

DEFINE_LOG_CATEGORY_STATIC(LogBeeSmartOOP, Log, All);

ABeeSmartOOPSimulationActor::ABeeSmartOOPSimulationActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

void ABeeSmartOOPSimulationActor::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogBeeSmartOOP, Log, TEXT("OOP simulation BeginPlay on %s"), *GetName());
    SetupOOPSimulation(FMath::Rand());
    StartOOPSimulation();
}

void ABeeSmartOOPSimulationActor::SetupOOPSimulation(int32 Seed)
{
    DestroySpawnedAgents();
    Random.Initialize(Seed == 0 ? 12345 : Seed);
    WinningHiveIndex = INDEX_NONE;
    bFinished = false;
    bRunning = false;
    bMigrationStarted = false;
    HomeLocation = GetActorLocation() + FVector(0.f, 0.f, 180.f);

    const TSubclassOf<ABeeSmartHiveActor> ResolvedHiveClass = HiveClass ? HiveClass.Get() : ABeeSmartHiveActor::StaticClass();
    const TSubclassOf<ABeeSmartBeeActor> ResolvedBeeClass = BeeClass ? BeeClass.Get() : ABeeSmartBeeActor::StaticClass();

    for (int32 Index = 0; Index < HiveCount; ++Index)
    {
        const FVector Direction = Random.GetUnitVector();
        const FVector Location = GetActorLocation() + FVector(Direction.X, Direction.Y, 0.f).GetSafeNormal2D() * Random.FRandRange(WorldRadius * 0.35f, WorldRadius) + FVector(0.f, 0.f, 40.f);
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Owner = this;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABeeSmartHiveActor* Hive = GetWorld()->SpawnActor<ABeeSmartHiveActor>(ResolvedHiveClass, Location, FRotator::ZeroRotator, SpawnParameters);
        if (Hive)
        {
            Hive->Quality = Random.FRandRange(0.25f, 1.f);
            Hive->Quorum = Quorum;
            Hives.Add(Hive);
        }
    }

    TArray<ABeeSmartHiveActor*> HiveRefs;
    for (ABeeSmartHiveActor* Hive : Hives) HiveRefs.Add(Hive);
    const int32 InitialScoutCount = FMath::Clamp(FMath::RoundToInt(BeeCount * InitialScoutPercentage), 1, BeeCount);
    for (int32 Index = 0; Index < BeeCount; ++Index)
    {
        FActorSpawnParameters SpawnParameters;
        SpawnParameters.Owner = this;
        SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        ABeeSmartBeeActor* Bee = GetWorld()->SpawnActor<ABeeSmartBeeActor>(ResolvedBeeClass, HomeLocation, FRotator::ZeroRotator, SpawnParameters);
        if (Bee)
        {
            Bee->InitializeBee(HomeLocation, HiveRefs, Index < InitialScoutCount);
            Bee->Movement->Speed *= FMath::Max(0.01f, SimulationSpeed);
            Bees.Add(Bee);
        }
    }

    UE_LOG(LogBeeSmartOOP, Log, TEXT("OOP simulation setup: %d bees, %d hives, %d initial scouts"), Bees.Num(), Hives.Num(), InitialScoutCount);
}

void ABeeSmartOOPSimulationActor::StartOOPSimulation()
{
    if (!bFinished) bRunning = true;
}

void ABeeSmartOOPSimulationActor::StopOOPSimulation()
{
    bRunning = false;
}

void ABeeSmartOOPSimulationActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!bRunning) return;
    if (!bFinished) CheckForQuorum();
    else PropagatePiping(DeltaSeconds);
}

void ABeeSmartOOPSimulationActor::CheckForQuorum()
{
    if (WinningHiveIndex != INDEX_NONE) return;
    for (int32 Index = 0; Index < Hives.Num(); ++Index)
    {
        if (Hives[Index] && Hives[Index]->HasReachedQuorum())
        {
            WinningHiveIndex = Index;
            UE_LOG(LogBeeSmartOOP, Log, TEXT("Hive %d reached quorum: on-site=%d committed=%d"), Index, Hives[Index]->BeesOnSite, Hives[Index]->CommittedBees);
            for (ABeeSmartBeeActor* Bee : Bees)
            {
                if (Bee && (Bee->CurrentHive == Hives[Index] || Random.FRand() < 0.20f)) Bee->StartPiping();
            }
            bFinished = true;
            return;
        }
    }
}

void ABeeSmartOOPSimulationActor::PropagatePiping(float DeltaSeconds)
{
    if (bMigrationStarted)
    {
        int32 FinishedCount = 0;
        for (ABeeSmartBeeActor* Bee : Bees)
        {
            if (Bee && Bee->GetBeeState() == EBeeSmartOOPState::Finished) ++FinishedCount;
        }

        if (FinishedCount == Bees.Num())
        {
            UE_LOG(LogBeeSmartOOP, Log, TEXT("OOP migration finished: all %d bees reached the winning hive"), FinishedCount);
            bRunning = false;
        }
        return;
    }

    int32 PipingCount = 0;
    for (ABeeSmartBeeActor* Bee : Bees) if (Bee && Bee->IsPiping()) ++PipingCount;

    if (PipingCount == Bees.Num())
    {
        UE_LOG(LogBeeSmartOOP, Log, TEXT("OOP simulation finished: all %d bees are piping"), PipingCount);
        if (Hives.IsValidIndex(WinningHiveIndex))
        {
            for (ABeeSmartBeeActor* Bee : Bees) if (Bee) Bee->StartMigrationToHive(Hives[WinningHiveIndex]);
        }
        bMigrationStarted = true;
        return;
    }

    const float PipingFraction = Bees.Num() > 0 ? static_cast<float>(PipingCount) / Bees.Num() : 0.f;
    const float AdoptionProbability = DeltaSeconds * (0.35f + PipingFraction * 1.5f);
    for (ABeeSmartBeeActor* Bee : Bees)
    {
        if (Bee && Bee->GetBeeState() != EBeeSmartOOPState::MigratingToHive &&
            Bee->GetBeeState() != EBeeSmartOOPState::Finished &&
            !Bee->IsPiping() && Random.FRand() < AdoptionProbability)
        {
            Bee->StartPiping();
        }
    }
}

void ABeeSmartOOPSimulationActor::DestroySpawnedAgents()
{
    for (ABeeSmartBeeActor* Bee : Bees) if (IsValid(Bee)) Bee->Destroy();
    for (ABeeSmartHiveActor* Hive : Hives) if (IsValid(Hive)) Hive->Destroy();
    Bees.Reset();
    Hives.Reset();
}
