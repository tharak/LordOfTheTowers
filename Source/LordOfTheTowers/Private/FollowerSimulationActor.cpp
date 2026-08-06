#include "FollowerSimulationActor.h"
#include "FollowerTurtleActor.h"
#include "DrawDebugHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogFollower, Log, All);

AFollowerSimulationActor::AFollowerSimulationActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.033f;
}

void AFollowerSimulationActor::BeginPlay()
{
    Super::BeginPlay();
    SetupFollower(FMath::Rand());
    StartFollower();
}

void AFollowerSimulationActor::SetupFollower(int32 Seed)
{
    for (AFollowerTurtleActor* Turtle : Turtles) if (IsValid(Turtle)) Turtle->Destroy();
    Turtles.Reset();
    Random.Initialize(Seed == 0 ? 1998 : Seed);
    const TSubclassOf<AFollowerTurtleActor> ResolvedClass = TurtleClass ? TurtleClass.Get() : AFollowerTurtleActor::StaticClass();
    for (int32 Index = 0; Index < Population; ++Index)
    {
        const FVector Location = GetActorLocation() + FVector(Random.FRandRange(-WorldRadius, WorldRadius), Random.FRandRange(-WorldRadius, WorldRadius), 0.f);
        AFollowerTurtleActor* Turtle = GetWorld()->SpawnActor<AFollowerTurtleActor>(ResolvedClass, Location, FRotator(0.f, Random.FRandRange(0.f, 360.f), 0.f));
        if (Turtle) { Turtle->Initialize(); Turtles.Add(Turtle); }
    }
    UpdateCounts();
    UE_LOG(LogFollower, Log, TEXT("Follower setup: %d turtles, near %.1f, far %.1f, waver %.1f"), Turtles.Num(), NearRadius, FarRadius, Waver);
}

void AFollowerSimulationActor::StartFollower() { bRunning = true; }
void AFollowerSimulationActor::StopFollower() { bRunning = false; }

void AFollowerSimulationActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    DrawDebugBox(GetWorld(), GetActorLocation(), FVector(WorldRadius, WorldRadius, 20.f), FColor(80, 80, 80), false, 0.08f, 0, 2.f);
    if (!bRunning) return;
    AttachTurtles();
    MoveTurtles(DeltaSeconds);
    UpdateCounts();
}

void AFollowerSimulationActor::AttachTurtles()
{
    for (AFollowerTurtleActor* Turtle : Turtles)
    {
        if (!Turtle || Turtle->Leader) continue;
        const float Span = FMath::Max(NearRadius, FarRadius);
        const float Inner = FMath::Min(NearRadius, Span);
        const float Outer = FMath::Max(Inner + 0.1f, Span);
        const float XDistance = Random.FRandRange(Inner, Outer) * 80.f;
        const float YDistance = Random.FRandRange(Inner, Outer) * 80.f;
        FVector CandidateLocation = Turtle->GetActorLocation() + FVector(Random.FRand() < 0.5f ? -XDistance : XDistance, Random.FRand() < 0.5f ? -YDistance : YDistance, 0.f);
        AFollowerTurtleActor* Candidate = FindAttachCandidate(Turtle, CandidateLocation);
        if (Candidate)
        {
            Candidate->Follower = Turtle;
            Turtle->Leader = Candidate;
        }
    }
}

AFollowerTurtleActor* AFollowerSimulationActor::FindAttachCandidate(const AFollowerTurtleActor* Turtle, const FVector& CandidateLocation) const
{
    AFollowerTurtleActor* Best = nullptr;
    float BestDistance = FMath::Square(55.f);
    for (AFollowerTurtleActor* Other : Turtles)
    {
        if (!Other || Other == Turtle || Other->Follower) continue;
        const float Distance = FVector::DistSquared2D(Other->GetActorLocation(), CandidateLocation);
        if (Distance < BestDistance) { BestDistance = Distance; Best = Other; }
    }
    return Best;
}

void AFollowerSimulationActor::MoveTurtles(float DeltaSeconds)
{
    TArray<TObjectPtr<AFollowerTurtleActor>> Snapshot = Turtles;
    for (AFollowerTurtleActor* Turtle : Snapshot) if (IsValid(Turtle)) Turtle->TurnAndMove(this, DeltaSeconds);
}

void AFollowerSimulationActor::UpdateCounts()
{
    UnattachedCount = HeadCount = BodyCount = TailCount = 0;
    for (AFollowerTurtleActor* Turtle : Turtles)
    {
        if (!Turtle) continue;
        if (!Turtle->Leader && !Turtle->Follower) ++UnattachedCount;
        else if (!Turtle->Leader && Turtle->Follower) ++HeadCount;
        else if (Turtle->Leader && Turtle->Follower) ++BodyCount;
        else ++TailCount;
    }
}

void AFollowerSimulationActor::RemoveTurtle(AFollowerTurtleActor* Turtle)
{
    if (!Turtle) return;
    Turtles.Remove(Turtle);
    Turtle->Destroy();
}
