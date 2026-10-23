# Simulation loop

**Partly a proposal.** How a game's world moves forward in time, and why. Built so far: the fixed
steps (`FixedStepClock`, `ILayer::OnFixedUpdate()`, in `Application::Run()`), the world with its
ordered systems (`GameWorld`, `ISimSystem`, in `Engine/Source/Runtime/Sim`), Pause and Step, and
drawing moving bodies between steps. Commands, events and time scale are still the plan.

## The shape of it

- **The world moves in fixed steps.** 60 steps a second by default, whatever the frame rate.
  `Application::Run()` adds each frame's real time to an accumulator and runs a step for every
  1/60 s in it. A frame runs at most 4 steps; time beyond that is dropped and logged once, so a slow
  frame makes the game slow down instead of falling further and further behind.
- **One frame**, with the steps added to what [Architecture.md](../Architecture.md) describes:
  1. wait for the frame, poll events, `InputSystem::BeginFrame()`, as today;
  2. every layer's `OnUpdate()`: reads input and turns it into commands, moves the camera;
  3. zero or more steps: every layer's `OnFixedUpdate()`;
  4. `DrawFrame()`, which draws moving things between their last two steps;
  5. `InputSystem::EndFrame()`.
- **Two kinds of time.** `FrameTime` is what `OnUpdate()` gets: seconds since the last frame and
  the frame's number. `StepTime` is what `OnFixedUpdate()` gets: the step length, the step's
  number (64-bit, never wraps) and the world's time in seconds as a `double`. `OnUpdate(float)`
  stays as it is until the layers move to `FrameTime`.

## Only the simulation changes the world

- **Commands.** What a player does becomes a command: a small struct such as
  `PickUp { item guid }` or `Interact { target guid, point }`. A game declares its own.
  `OnUpdate()` queues it, and the next step applies it before its systems run. Nothing else writes
  simulated components: not `OnUpdate()`, not `OnUI()`, not the renderer.
- **Why.** Co-op later sends these same commands over the network to the one machine that runs
  the simulation, and sends back what changed. If only commands change the world, that is an
  addition. If any code can write the world from anywhere, it is a rewrite.
- **Commands are data.** They read and write with `BinaryArchive` and name entities by guid,
  never by `Entity` handle. That also gives replays: record a session's commands, play them
  again, and the world ends up the same (see Determinism). A bug report can carry its replay.
- **The editor still edits directly.** Live edits from the editor are not player actions: they
  set state, as today, and the game applies them between steps.

## Systems, in one order

- **Built:** a `GameWorld` owns the `Registry` and the systems, in one list in the order they were
  added (`AddSystem()`). `GameLayer` owns it, has the game module add its systems
  (`GameModule::CreateSystems`) and steps it in `OnFixedUpdate()`.
- **Built:** a system implements `ISimSystem::Step(const SimContext&)`; `SimContext` carries the
  registry, the `StepTime` and the world's debug drawing (`DebugDraw`, cleared at the start of
  each step, timed shapes aged by the step's length). Systems talk through components, never by
  calling each other.
- **Built:** a system can run every nth step (`AddSystem(system, n)`), counted from step 0, so
  systems with the same interval run in the same steps: heat or plant growth don't need 60 updates
  a second.
- **Built:** each step of the world is a profiler zone, with one zone per system named by its
  `GetName()`, so a system over the step's budget shows by name.
- **Built:** `SimContext::Guids` finds the world's entities by guid (`FindEntityByGuid(World,
  Guids, guid)`), e.g. for surface anchors. `GameWorld` makes it once a step, before the systems,
  in the memory it had, so it allocates nothing once the world stops growing. An entity made or
  given a guid during a step is in it from the next step on; one destroyed is gone at once. A
  system that needs the newer ones searches the world or makes a lookup of its own.
- **Not yet:** the command and event queues and the engine services (the physics world, later the
  spatial index) in `SimContext`.
- The engine's part of the order is fixed; a game's systems go into the slots around it:
  1. apply commands (not built yet);
  2. bodies follow the physics components and the transforms set since the last step (built:
     `PhysicsBodies`), and the guid lookup is made again (built: `SimContext::Guids`);
  3. the game's systems that decide forces and constraints (built: `ESimStage::BeforePhysics`);
  4. physics step (built);
  5. bodies write their transforms (built);
  6. the game's systems that react to where things are, each at its own rate (built:
     `ESimStage::AfterPhysics`);
  7. hand out events (contacts, regions entered and left; not built yet).

## Drawing between steps

- **Built.** At 60 steps a second on a 144 Hz monitor, a moving crate would jitter if drawn where
  its last step left it. Moving entities keep their previous transform too, and the renderer draws
  them between the two by how far the frame is into the next step. That shows the world up to one
  step late, which is the price of smooth motion.
- **The data:** `PreviousTransformComponent` (Scene): the pose before the last step, and the pose
  that step left. Not reflected, so never saved, edited or sent. `PhysicsBodies` writes it for
  dynamic bodies, before writing their new poses; only entities that moved in the last step have
  one, so still scenes cost nothing extra. Kinematic bodies, and anything else moved only in steps,
  can opt in the same way (`SetPreviousTransform()`) later.
- **Teleports aren't smeared.** A transform set from outside since the step, by a system or a live
  edit, no longer matches the pose the step left, and is drawn where it is
  (`InterpolateTransform()`). The next step teleports the body and starts from there.
- **The renderer:** `IRenderer::SetWorldInterpolation(world, clock)` draws the world's entities
  that have the component at `mix(previous, current, factor)`, rotations slerped the short way,
  with the clock's `GetInterpolationFactor()`, read as the frame is drawn. The game sets its
  world's once, with `Application::GetSimulationClock()`: a factor handed over by a layer would be
  a frame off, as `OnUpdate()` runs before the frame's steps and `OnUI()` after the scene is
  drawn.
  - Paused, the factor is 1: the picture is still, and Step shows where each step leaves things.
  - Picking and the editor's overlays use the transforms as they are.
  - Surface decals use the same factor, worked out once per world and frame: each is resolved on
    its host as the host is drawn (`GetDrawnTransform()`), so a decal on a tumbling crate stays on
    the crate's mesh instead of running a step ahead of it.
  - The editor's own world is never set, so its views draw transforms as they are.

## Threads

- The main thread owns the world. The first threads in Nyx are Jolt's, and they only run inside
  the physics step. What they report (contacts) goes into a queue that the main thread empties
  after the step, so game code never runs on another thread.
- A job system of Nyx's own comes when the profiler shows a need, not before.

## Pause, Step and time scale

- **Built:** **Pause** stops steps; frames go on, so the camera still moves and debug drawing
  still shows. **Step** runs exactly one step while paused. Both come from the toolbar's Pause and
  Step (F6 and F10), over the editor link messages `SetSimulationPaused` and `StepSimulation`,
  for every running game; a game started while paused starts paused. New message types need no
  new protocol version: a side that doesn't know one skips it.
- **Time scale** (0.1 to 4) multiplies the time added to the accumulator. Debug only.

## Determinism

- Goal: the same build, scene and commands give the same world, step for step. That makes tests
  and replays exact, and simulation bugs reproducible.
- What it takes: fixed steps, systems in a fixed order, no iteration over unordered containers in
  the simulation, no wall-clock time in it, one random generator per world with a saved seed. Jolt
  is deterministic on the same build when bodies are added in the same order.
- Not a goal: the same result on different machines or builds. Co-op sends state, not just
  commands, so it doesn't need that.
- `NyxSimulationTests` runs a scene for 600 steps twice and compares a hash of every transform.

## Budget

- A step takes at most 4 ms on the reference PC. Every system has its own profiler zone, so the
  one over budget is plain to see.

## Not decided yet

- Whether slow systems run at a fixed fraction of the step rate or on a world clock with a
  scheduler, once there is one.
- Simulating in the editor's own viewport, without Play. Play covers it for now.
