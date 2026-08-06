#include "AntAdaptationSimulationActor.h"
#include "AntAdaptationAntActor.h"
#include "AntAdaptationFlowerActor.h"
#include "AntAdaptationNestActor.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogAntAdaptation, Log, All);

AAntAdaptationSimulationActor::AAntAdaptationSimulationActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

void AAntAdaptationSimulationActor::BeginPlay()
{
    Super::BeginPlay();
    SetupAntAdaptation(FMath::Rand());
    StartAntAdaptation();
}

void AAntAdaptationSimulationActor::SetupAntAdaptation(int32 Seed)
{
    for (AAntAdaptationAntActor* Ant : Ants) if (IsValid(Ant)) Ant->Destroy();
    for (AAntAdaptationFlowerActor* Flower : Flowers) if (IsValid(Flower)) Flower->Destroy();
    for (AAntAdaptationNestActor* Nest : Nests) if (IsValid(Nest)) Nest->Destroy();
    Ants.Reset(); Flowers.Reset(); Nests.Reset();
    Random.Initialize(Seed == 0 ? 1741 : Seed);
    Chemical.Init(0.f, FMath::Max(1, GridWidth * GridHeight));
    SimulationTime = 0.f;

    const TSubclassOf<AAntAdaptationNestActor> ResolvedNestClass = NestClass ? NestClass.Get() : AAntAdaptationNestActor::StaticClass();
    const TSubclassOf<AAntAdaptationFlowerActor> ResolvedFlowerClass = FlowerClass ? FlowerClass.Get() : AAntAdaptationFlowerActor::StaticClass();
    for (int32 Index = 0; Index < 2; ++Index)
    {
        const EAntAdaptationTeam Team = Index == 0 ? EAntAdaptationTeam::Blue : EAntAdaptationTeam::Red;
        const FVector Location = GetActorLocation() + (Team == EAntAdaptationTeam::Blue ? FVector(-1200.f, 0.f, 0.f) : FVector(1200.f, 0.f, 0.f));
        AAntAdaptationNestActor* Nest = GetWorld()->SpawnActor<AAntAdaptationNestActor>(ResolvedNestClass, Location, FRotator::ZeroRotator);
        if (Nest)
        {
            Nest->Team = Team;
            Nest->AntSize = Team == EAntAdaptationTeam::Blue ? BlueSize : RedSize;
            Nest->Aggression = Team == EAntAdaptationTeam::Blue ? BlueAggression : RedAggression;
            Nest->StartEnergy = Team == EAntAdaptationTeam::Blue ? BlueStartEnergy : RedStartEnergy;
            Nests.Add(Nest);
            for (int32 AntIndex = 0; AntIndex < InitialAntsPerNest; ++AntIndex) SpawnAnt(Nest);
        }
    }
    for (int32 Index = 0; Index < InitialFlowers; ++Index) AddFlowerAt(GetRandomPoint());
    UE_LOG(LogAntAdaptation, Log, TEXT("Ant Adaptation setup: %d ants, %d flowers, %d nests"), Ants.Num(), Flowers.Num(), Nests.Num());
}

void AAntAdaptationSimulationActor::StartAntAdaptation() { bRunning = true; }
void AAntAdaptationSimulationActor::StopAntAdaptation() { bRunning = false; }

void AAntAdaptationSimulationActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    const FColor Blue = FColor::Cyan;
    DrawDebugBox(GetWorld(), GetActorLocation(), FVector(WorldRadius, WorldRadius, 20.f), FColor(40, 70, 40), false, 0.06f, 0, 2.f);
    if (!bRunning) return;
    const float Step = DeltaSeconds * FMath::Max(0.01f, SimulationSpeed);
    SimulationTime += Step;
    TArray<TObjectPtr<AAntAdaptationAntActor>> Snapshot = Ants;
    for (AAntAdaptationAntActor* Ant : Snapshot) if (IsValid(Ant)) StepAnt(Ant, Step);
    Reproduce();
    GrowFlowers();
    DiffuseChemical(Step);
    BlueAntCount = 0; RedAntCount = 0;
    for (AAntAdaptationAntActor* Ant : Ants) if (Ant && Ant->Team == EAntAdaptationTeam::Blue) ++BlueAntCount; else if (Ant) ++RedAntCount;
}

void AAntAdaptationSimulationActor::StepAnt(AAntAdaptationAntActor* Ant, float DeltaSeconds) { Ant->SimulateStep(this, DeltaSeconds); }

void AAntAdaptationSimulationActor::SpawnAnt(AAntAdaptationNestActor* Nest)
{
    if (!Nest) return;
    const TSubclassOf<AAntAdaptationAntActor> ResolvedAntClass = AntClass ? AntClass.Get() : AAntAdaptationAntActor::StaticClass();
    AAntAdaptationAntActor* Ant = GetWorld()->SpawnActor<AAntAdaptationAntActor>(ResolvedAntClass, Nest->GetActorLocation() + GetRandomPoint() * 0.01f, FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f));
    if (Ant)
    {
        Ant->Initialize(Nest, Nest->StartEnergy, Nest->AntSize);
        Ants.Add(Ant);
    }
}

void AAntAdaptationSimulationActor::Reproduce()
{
    for (AAntAdaptationNestActor* Nest : Nests)
    {
        if (!Nest || Nest->FoodStore <= Nest->GetCreateCost()) continue;
        Nest->FoodStore -= Nest->GetCreateCost();
        SpawnAnt(Nest);
        if (FMath::FloorToInt(SimulationTime) % 100 == 1 && Nest->FoodStore > 50.f) Nest->FoodStore -= 50.f;
    }
}

void AAntAdaptationSimulationActor::GrowFlowers()
{
    if (Random.RandRange(0, 499) == 0) AddFlowerAt(GetRandomPoint());
}

void AAntAdaptationSimulationActor::DiffuseChemical(float DeltaSeconds)
{
    if (Chemical.Num() == 0) return;
    TArray<float> Next = Chemical;
    for (int32 Y = 1; Y < GridHeight - 1; ++Y) for (int32 X = 1; X < GridWidth - 1; ++X)
    {
        const int32 I = X + Y * GridWidth;
        Next[I] = (Chemical[I] * 0.2f + Chemical[I - 1] * 0.2f + Chemical[I + 1] * 0.2f + Chemical[I - GridWidth] * 0.2f + Chemical[I + GridWidth] * 0.2f) * FMath::Max(0.f, 1.f - EvaporationRate * DeltaSeconds * 0.01f);
    }
    Chemical = MoveTemp(Next);
}

void AAntAdaptationSimulationActor::AddFlowerAt(const FVector& Location)
{
    for (AAntAdaptationFlowerActor* Existing : Flowers) if (Existing && FVector::DistSquared2D(Existing->GetActorLocation(), Location) < FMath::Square(120.f)) return;
    const TSubclassOf<AAntAdaptationFlowerActor> ResolvedFlowerClass = FlowerClass ? FlowerClass.Get() : AAntAdaptationFlowerActor::StaticClass();
    AAntAdaptationFlowerActor* Flower = GetWorld()->SpawnActor<AAntAdaptationFlowerActor>(ResolvedFlowerClass, Location, FRotator::ZeroRotator);
    if (Flower) Flowers.Add(Flower);
}

void AAntAdaptationSimulationActor::AddPheromoneAt(const FVector& Location, float Amount) { AddChemicalAt(Location, Amount); }
void AAntAdaptationSimulationActor::ErasePheromoneAt(const FVector& Location)
{
    const int32 Center = CellIndex(Location);
    if (!Chemical.IsValidIndex(Center)) return;
    Chemical[Center] = 0.f;
}

float AAntAdaptationSimulationActor::GetChemicalAt(const FVector& Location) const
{
    const int32 I = CellIndex(Location);
    return Chemical.IsValidIndex(I) ? Chemical[I] : 0.f;
}

void AAntAdaptationSimulationActor::AddChemicalAt(const FVector& Location, float Amount)
{
    const int32 I = CellIndex(Location);
    if (Chemical.IsValidIndex(I)) Chemical[I] = FMath::Min(1000.f, Chemical[I] + Amount);
}

int32 AAntAdaptationSimulationActor::CellIndex(const FVector& Location) const
{
    const FVector Local = Location - GetActorLocation();
    const int32 X = FMath::Clamp(FMath::FloorToInt((Local.X + WorldRadius) / (2.f * WorldRadius) * GridWidth), 0, GridWidth - 1);
    const int32 Y = FMath::Clamp(FMath::FloorToInt((Local.Y + WorldRadius) / (2.f * WorldRadius) * GridHeight), 0, GridHeight - 1);
    return X + Y * GridWidth;
}

FVector AAntAdaptationSimulationActor::CellLocation(int32 X, int32 Y) const
{
    return GetActorLocation() + FVector((X + 0.5f) / GridWidth * 2.f * WorldRadius - WorldRadius, (Y + 0.5f) / GridHeight * 2.f * WorldRadius - WorldRadius, 0.f);
}

FVector AAntAdaptationSimulationActor::GetChemicalDirection(const FVector& Location, const FVector& Forward) const
{
    const FVector Right = FRotator(0.f, 45.f, 0.f).RotateVector(Forward).GetSafeNormal2D();
    const FVector Left = FRotator(0.f, -45.f, 0.f).RotateVector(Forward).GetSafeNormal2D();
    const float Ahead = GetChemicalAt(Location + Forward * 90.f);
    const float RightValue = GetChemicalAt(Location + Right * 90.f);
    const float LeftValue = GetChemicalAt(Location + Left * 90.f);
    if (Ahead < 0.05f && RightValue < 0.05f && LeftValue < 0.05f) return FVector::ZeroVector;
    return RightValue > LeftValue && RightValue > Ahead ? Right : (LeftValue > Ahead ? Left : Forward);
}

FVector AAntAdaptationSimulationActor::GetRandomPoint()
{
    return GetActorLocation() + FVector(Random.FRandRange(-WorldRadius, WorldRadius), Random.FRandRange(-WorldRadius, WorldRadius), 0.f);
}

AAntAdaptationNestActor* AAntAdaptationSimulationActor::GetNest(EAntAdaptationTeam Team) const
{
    for (AAntAdaptationNestActor* Nest : Nests) if (Nest && Nest->Team == Team) return Nest;
    return nullptr;
}

float AAntAdaptationSimulationActor::GetStartEnergy(EAntAdaptationTeam Team) const { return Team == EAntAdaptationTeam::Blue ? BlueStartEnergy : RedStartEnergy; }
float AAntAdaptationSimulationActor::GetAggression(EAntAdaptationTeam Team) const { return Team == EAntAdaptationTeam::Blue ? BlueAggression : RedAggression; }
float AAntAdaptationSimulationActor::GetAntSize(EAntAdaptationTeam Team) const { return Team == EAntAdaptationTeam::Blue ? BlueSize : RedSize; }

void AAntAdaptationSimulationActor::RemoveAnt(AAntAdaptationAntActor* Ant)
{
    if (!Ant) return;
    Ants.Remove(Ant);
    Ant->Destroy();
}
