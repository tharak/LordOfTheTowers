# BeeSmart Hive Finding in Unreal

Place an `ABeeSmartSimulationActor` in a level. Call `SetupSimulation` and `StartSimulation` from Blueprint, or set the actor's editable parameters and let `BeginPlay` create the initial state.

The actor translates the NetLogo model's scout-bee state machine into UE: initial exploration, hive inspection, returning home, quality-weighted waggle-dance time, declining revisits, quorum detection, and piping. It uses debug geometry so no mesh assets are required for the first playable prototype.

The original model is by Yu Guo and Uri Wilensky. This implementation is an independent Unreal translation of the model behavior; it is not a line-for-line conversion of NetLogo code.

## Object-oriented comparison version

The parallel OOP implementation leaves `ABeeSmartSimulationActor` unchanged. Create a Blueprint derived from `ABeeSmartOOPSimulationActor` for the new version. It spawns independent `ABeeSmartHiveActor` and `ABeeSmartBeeActor` instances.

Each bee owns:

- `UBeeMovementComponent` for free-flight and target movement
- `UBeeDanceComponent` for quality-dependent dance timing

The coordinator only creates agents and detects quorum. Bee state transitions, movement, inspection, dancing, revisits, and piping live on the bee actor and its components.
