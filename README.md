# LordOfTheTowers

Unreal Engine SHMUP project (plane-vs-plane shooter over water/islands, top-down/2.5D, movement bounded to an on-screen 3D arena).

This file exists so a fresh session (human or AI) with no prior conversation history can quickly reconstruct project conventions and the state of in-flight feature work. Update it as issues close.

## Project shape

- Single Runtime module: `Source/LordOfTheTowers/`.
- Gameplay is currently **100% Blueprint**, living under `Content/Blueprints/`. The C++ classes already in `Source/LordOfTheTowers/{Public,Private}` (`AntAdaptation*`, `BeeSmart*`, `Follower*`, etc.) are leftovers from earlier, unrelated tutorial exercises — not part of the SHMUP.
- Core composition idiom (used by player, enemy, and projectile alike):
  - `BP_MovableComponent` — smoothed forward movement driven by a raw `MoveVector` variable (`VInterpTo` toward `MoveVector * MovementSpeed` each tick).
  - `BP_RotationComponent` — banking-tilt visual, reads its own `MoveVector` and interpolates the mesh's *relative* rotation. Never touches actor rotation directly.
  - `BP_ShooterComponent` — fires `BP_Projectile` via a `Shoot(FTransform SpawnTransform)` custom event (`SpawnActorFromClass`).
- Player: `BP_PawnPlayer` (parent `Pawn`) + `BP_PlayerControllerBase` (Enhanced Input: `IA_MoveX/Y/Z`, `IA_Shoot`, `IA_Restart` via `IMC_Main`).
- Enemy (dumb target, pre-AI): `BP_PawnEnemy` (parent `Pawn`) — flies a hardcoded direction, self-destructs on hit. No shooting, no controller logic.
- Arena: `Content/Maps/Main.umap` has 7 `BlockingVolume` actors named `ScreenBounds`/`ScreenBounds2..7` constraining pawn movement to a bounded 3D box. **No NavMesh** — this is not walkable terrain, so AI movement uses direct steering (`MoveVector`), not `AAIController::MoveToLocation`/pathfinding.
- GameMode: `BP_GameModeBase` (`DefaultPawnClass=BP_PawnPlayer`, `PlayerControllerClass=BP_PlayerControllerBase`).

## Working conventions (from explicit user direction — do not violate)

1. **Additive only.** New features get new Blueprint/C++ assets. Never edit, reparent, or delete an existing Blueprint asset (even unused/orphaned stubs) to implement a new feature — create a sibling asset instead. Reusing an existing *component class* by adding a fresh instance of it to a new actor is fine; opening and editing the component's own asset definition is not.
2. **GitHub process before code.** For non-trivial features: create/use the repo's GitHub Project board, file one issue per implementation unit (not one giant issue) and add each to the board, and keep this README current — all *before* writing implementation code.
3. Prefer C++/UObject patterns for new systems (this repo's gameplay logic is Blueprint-heavy today mostly because it predates this convention, not because Blueprint is preferred going forward).

## GitHub

- Project board: [LordOfTheTowers](https://github.com/users/tharak/projects/7) — general-purpose repo backlog, not feature-scoped.
- Issues: filed per implementation unit, one per board card.

## In-flight feature: AI Enemy Controller (ARC-Raiders-style patrol/detect/chase/attack)

Adding a proper AI-controlled enemy: a perception-driven state loop (patrol → detect → chase → attack), scoped to this project's bounded-arena SHMUP (no investigate/search, no flee, no squad alerting, no multiple enemy archetypes in v1).

**Architecture decisions:**
- Movement: direct-steering Behavior Tree tasks that set `MoveVector` on `BP_MovableComponent`/`BP_RotationComponent`, mirroring how the player already moves — not `MoveToLocation`/NavMesh (arena has no NavMesh and isn't walkable terrain).
- New native classes: `AEnemyPawn` (thin `APawn` interface boundary — `SetMoveDirection`, `FireWeapon`, `AttackRange`), `AEnemyAIController` (`AAIController` + `UAIPerceptionComponent`/sight sense + Blackboard wiring), plus BT support nodes (`UBTService_UpdateChaseLocation`, `UBTTask_MoveToBBLocation`, `UBTTask_SetRandomPatrolPoint`, `UBTTask_FireAtTarget`, `UBTDecorator_InAttackRange`).
- New Blueprints (siblings, not edits): `BP_EnemyAI` (parent `AEnemyPawn`, own component instances), `AIC_Enemy` (parent `AEnemyAIController`), all under `Content/Blueprints/AI/`.
- Blackboard (`BB_Enemy`): `TargetActor` (Object), `ChaseLocation` (Vector), `PatrolLocation` (Vector), `HomeLocation` (Vector).
- Perception: player has no `IGenericTeamAgentInterface`, so it reads as Neutral — sight sense configured to detect Neutrals (documented simplification; revisit if a real team system is added later).
- Behavior Tree (`BT_EnemyPatrolChaseAttack`): root Selector — Combat branch (gated by `TargetActor` IsSet, observer-abort) containing an Attack/Chase sub-selector, falling back to a Patrol branch (random point near spawn → move → wait).

**Status checklist** (mirrors the [Project board](https://github.com/users/tharak/projects/7)):
- [x] Add `AIModule`/`NavigationSystem`/`GameplayTasks` to Build.cs
- [x] `AEnemyPawn` base class
- [x] `AEnemyAIController` + perception setup
- [x] BT support classes (service/tasks/decorator)
- [x] `BP_EnemyAI` Blueprint
- [x] `AIC_Enemy` Blueprint
- [ ] `BB_Enemy` + `BT_EnemyPatrolChaseAttack` assets (manual, in-editor — **next step, needs a human in the Unreal Editor**: create both assets under `Content/Blueprints/AI/`, build the tree per the "Behavior Tree" layout below, then set `AIC_Enemy`'s `BehaviorTreeAsset` to it)
- [ ] Place test instances, verify patrol/chase/attack loop

**Explicitly out of scope for this pass:** investigate/search-on-lost-target, flee/retreat at low health, call-for-backup/squad alerting, multiple enemy archetypes, multiplayer/replication, real team-based friend/foe on the player, homing/aimed projectiles, wave-based spawner.
