# Physics

How rigid bodies work in Nyx, and why. The code is in `Engine/Source/Runtime/Physics`; the
engine is [Jolt](https://github.com/jrouwe/JoltPhysics) (`Dependencies/JoltPhysics`).

## Why Jolt

- Constraints for what games built on Nyx need: fixed, point, hinge, slider, distance, path; a
  character controller; sensors.
- Fast and multithreaded, with bodies at rest asleep and costing nothing.
- Deterministic on one build: the same bodies, made in the same order and stepped the same way,
  end in exactly the same place. Tests and replays depend on it.
- Modern C++, plain CMake, MIT licence, no SDK to install.

## The shape of it

- **`PhysicsWorld`** owns a Jolt physics system, its job threads and its scratch memory. Everything
  is called from one thread; Jolt's threads run only inside `Step()`.
- **Jolt stays inside the folder.** `PhysicsWorld.h` speaks glm and opaque `BodyHandle`s and
  `ConstraintHandle`s; only `PhysicsWorld.cpp` includes Jolt. The engine links Jolt privately.
- **Bodies** are made from a `BodyDesc`: a shape (box, sphere, capsule, triangle mesh), a pose, a
  motion (static, kinematic, dynamic), mass (or the shape's volume at the density of water),
  friction, restitution, damping, gravity factor, and a number of the owner's (an entity's guid).
- **Triangle meshes** (`EShapeKind::Mesh`, a shared `TriangleMesh`: vertices, three indices a
  triangle, the smooth edges' angle) are Jolt's `MeshShape`: static bodies only, since Jolt gives
  triangles no mass (`CreateBody()` refuses another motion, logged), and never cast or overlapped
  (`ShapeCast()` and `Overlap()` refuse one, logged). Jolt sanitizes them (degenerate and doubled
  triangles go) and works out which edges are smooth, where two triangles meet bending less than the
  angle, so what slides across doesn't catch on them. Making one is what costs, the more the more
  triangles, so it is made once and again only when its triangles change.
- **Rays** treat convex shapes as solid (one that starts inside a body hits it at once) and
  triangles as one-sided: a ray from behind passes through, so every hit's normal points back at
  the caster.
- **Collision layers** follow from the motion: static bodies collide with moving ones, moving ones
  with everything, sensors with moving ones only. One table in `PhysicsWorld.cpp`.
- **Sensors** (`BodyDesc::bSensor`) detect the moving bodies, kinematic ones too, that enter and
  leave them and push nothing: trigger volumes.
- **Forces** (`AddForce`, `AddForceAtPoint`, `AddTorque`) act during the next step and then go.
  A force off the centre of mass turns the body by itself: no extra code for torque.
- **Teleport** puts a body somewhere at once and stops it, e.g. for an editor's edit.
  **MoveKinematic** moves a kinematic body over the next step, pushing what it meets.
- **Queries**: `RayCast` and `ShapeCast` (any body shape, swept along a direction) give the first
  hit: body, point, normal, distance and the body's user data; `Overlap` gives the bodies a shape
  touches, in handle order. A `QueryFilter` picks static and moving bodies, sensors (left out by
  default), and one body to ignore, e.g. the caster. `PickPhysicsBody()` (`Sim`) casts a ray in a
  game world and gives the entity whose body it hit, through `PhysicsBodies::FindEntity()`.
- **Contact events**: two bodies began or ended touching; for "began" also a point, the normal and
  how fast they came together. Sensors give the same events. Jolt calls its contact listener on its
  job threads during `Step()`, many pairs at once, so the listener only appends to a list under a
  mutex. Once Jolt is done, `Step()` sorts the step's events by their bodies, so the same inputs
  give the same list whatever thread found what, and keeps them for `TakeContactEvents()` on the
  caller's thread (at most `MaxContactEvents` waiting).
- **Sleeping contacts**: Jolt ends a contact when its bodies fall asleep, as it stops looking at
  them, and begins it again when they wake. A box resting in a trigger would leave it, so
  `PhysicsWorld` keeps such a contact open until its bodies wake apart, or one is destroyed. A
  woken body is looked at from the next step on, so that "ended" can come a step late.
- **Constraints** (`CreateConstraint`, `DestroyConstraint`): fixed, point, hinge (an axis, optional
  angle limits), slider (an axis, optional limits), distance (least and greatest). Between two
  bodies, or a body and the world; points and axes in world space, where the bodies are now being
  the rest. A body's constraints go when it does. Handles are opaque, like body handles.
- **Characters** (`CreateCharacter`, `MoveCharacter`, `TeleportCharacter`, `DestroyCharacter`) on
  Jolt's `CharacterVirtual`: an upright capsule standing on its position (its feet), which moves by
  collision checks when asked instead of in the step. `MoveCharacter(move, seconds)` turns its
  velocity towards the walk at the ground or air acceleration, adds gravity (not while it stands on
  walkable ground, which would slide it down slopes it can stand on) and the jump, then runs Jolt's
  `ExtendedUpdate()`: it slides along what it meets, climbs steps up to the step height, slides down
  ground steeper than its max slope, follows the ground down by the snap distance and pushes dynamic
  bodies with at most its push force. Only the capsule's lower round end stands on things
  (`mSupportingVolume`); sensors are walked through. Called before `Step()`, as Jolt advises, so the
  bodies meet it where it is now. A kinematic body 0.9 of its size inside it
  (`GetCharacterBody()`) is what rays, shape casts and dynamic bodies meet; it carries the
  character's user data, and `QueryFilter::Ignore` leaves it out. Two characters meet each other's
  bodies, not each other's capsules.
- **Jolt's own reports** go to Nyx's log; a failed Jolt assert (Debug builds) is logged, and breaks
  under a debugger.
- **The debug view** (`DrawDebug(DebugDraw&, PhysicsDebugOptions)`) draws the world as lines, where
  the last step left it; the options switch each part and set its colours:
  - every body's shape in its axes: a box's 12 edges, a sphere's 3 circles, a capsule's 2 rings, 4
    sides and 2 round ends, a mesh's triangles' 3 edges each (shared edges twice), or for a mesh of
    more triangles than `MaxMeshTriangles` (8192: 24576 lines of `DebugDraw`'s 32768), the 12 edges
    of its bounds; coloured by motion: static, kinematic, dynamic awake, dynamic asleep,
    sensor; every character's capsule, standing on its feet, in its own colour (its body inside
    isn't drawn);
  - the last step's contact points, a cross and an arrow along the normal each. Jolt reports them
    on its threads like the contact events, so they are kept only while asked for
    (`SetKeepContactPoints()`), and sorted after the step. Sleeping bodies report none.
  - constraints: a line between the anchor points on their two bodies, with a cross at each;
    a point or hinge joint that holds shows the two crosses on top of each other.

  Nothing is drawn in Release builds, which compile `DebugDraw` out.

## Build

- Jolt is a static library, built with Nyx's runtime library and flags, without link-time
  optimization, its own profiler, GPU compute, object streams or install target (top
  `CMakeLists.txt`). Its asserts are on in Debug.
- When Jolt is the top project it adds samples and tests; as Nyx's dependency it adds only the
  library.

## Numbers

- Resting bodies sink up to Jolt's penetration slop, 2 cm, before they are pushed out.
- `NyxPhysicsBenchmark` (not run by `ctest`): 1000 boxes dropped onto a floor, at 60 Hz, take about
  0.2 ms a step on average and 0.6 ms at most in Release on the development machine, against the
  simulation step's budget of 4 ms.

## Tests

`NyxPhysicsTests`: a box falls and comes to rest asleep on a floor; default and given mass; a
force at the centre moves without turning, one off it turns; impulses; teleporting stops a body;
a kinematic move reaches its target in one step; twenty boxes piled up twice end exactly alike;
spheres, capsules, user data, destroyed bodies. A ray hits the nearest box at its face, misses when
aimed away and passes the ignored body; a shape cast stops at a wall; an overlap finds the bodies
inside; a box falls through a sensor that tells of it entering and leaving; boxes landing begin one
contact each, sorted, the same twice, and none ends as they sleep; a sleeping contact ends when the
box is thrown up, not when it slides; a distance constraint keeps its range, a hinge its axis and
limits, a slider its body on its axis, a fixed constraint two bodies together. The debug view: a
box in 12 lines, a sphere in 72 and a capsule in 100, each its size; a colour for each motion;
contacts after a box lands, on the floor along its normal, and none unless kept; constraints
between their anchors; nothing with every part off.

## In the game world

- **Components** (`PhysicsComponents.h`, in Scene so they are reflected, saved and edited):
  `BoxColliderComponent`, `SphereColliderComponent`, `CapsuleColliderComponent`,
  `MeshColliderComponent` (the triangles of the entity's `RuntimeMeshComponent`, always static), a
  `RigidBodyComponent` that makes the body move (dynamic, or kinematic), and an optional
  `PhysicsMaterialComponent`. A collider alone is a static body. Small parts that combine, not one
  component with a shape switch; one collider an entity (box over sphere over capsule over mesh).
- **The entity's transform** places the body, and its scale scales the shape: a box by each axis,
  a sphere by the largest, a capsule's radius by X and Z and its length by Y, a mesh's vertices by
  each axis, its triangles turned round when the scale mirrors them.
- **`PhysicsBodies`** (Sim) keeps the bodies in step with the components, in `GameWorld::Step()`:
  - before the physics step: bodies made for new colliders, made again (at the transform) when a
    component's values or the scale changed, destroyed with their collider or entity;
  - a mesh collider's body is made again when its mesh's data (the `MeshData` it points at) or
    revision changes, or the scale or its smooth edge angle; a hash of those is compared each step,
    which costs nothing like making it. Its triangles are made from the `MeshData` only then. A
    mesh that isn't there yet (its generator's system runs after the bodies are synced, so the
    first step has none), is empty or isn't valid (logged) leaves the collider without a body until
    the mesh changes. A rigid body beside it is warned of once and doesn't move it;
  - a transform set since the last step, by a system or an editor's live edit, teleports its body
    and stops it. Compared exactly with what the bodies wrote back, so only real changes count;
  - kinematic bodies are moved to their transforms over the step;
  - after the step, dynamic bodies write their poses into their transforms, and those that moved
    where they were before into a `PreviousTransformComponent`, so the game draws them between
    their last two steps ([Simulation loop](simulation-loop.md#drawing-between-steps)). A body
    teleported, made static or gone has none.
  - characters (`CharacterComponent`, Scene) are made with the component and a transform, made
    again (where they stand, from rest) when their radius, height or max slope change, destroyed
    with the component or entity. Their movement values count from the next step. A position or
    rotation set since the last step teleports the character and stops it. Colliders on a
    character's entity make no body: the capsule is its shape. `FindBody()` gives a character's
    body, `FindEntity()` its entity, `FindCharacter()` the character;
  - a player walks a character with a `MoveCharacterCommand` (Scene) `{ Character guid, Move,
    bJump }`. The step applies the commands before its systems (`ApplyMoveCharacterCommands()`,
    Sim): the last move for a character becomes its `DesiredMove`, kept until the next, so a frame
    with two steps walks in both; any jump sets `bJumpRequested`. This is the only way in from
    outside a step: `DesiredMove` and `bJumpRequested` are simulation input;
  - after the systems of `ESimStage::BeforePhysics`, which may still change `DesiredMove` and
    `bJumpRequested`, and before the physics step,
    `MoveCharacters()` walks each character by its component (walk speed, acceleration, air control
    as a part of it, jump speed, gravity factor, step height, ground snap), clears the jump, writes
    the feet's position into the transform and what it stands on into the component (`bOnGround`,
    `GroundNormal`, `Velocity`). The rotation is the game's, e.g. to face where it walks; the
    capsule stays upright. A position a system set teleports it first. It keeps where it was before
    into its `PreviousTransformComponent`, drawn turning too when a system turned it;
  - a kinematic body that a system moved during the step (`ESimStage::BeforePhysics`) gets one too:
    from where its transform was as the step began to where the system put it. Its body follows a
    step behind, so the debug view's shape trails the mesh. One moved between steps, by a live
    edit, is drawn where it was put, and the next step starts from there.
  - Bodies are kept by entity in a fixed order, so they are made and destroyed the same way each
    run. A body's user data is its entity, for `FindEntity()`; never saved.
- **Systems** get the physics world and the bodies in `SimContext`, and run before the physics step
  (`ESimStage::BeforePhysics`, e.g. to push) or after it (`AfterPhysics`, e.g. to react).
- **The debug view**: `GameWorld::SetPhysicsDebugView(options)` draws the physics world into the
  world's debug drawing after each physics step, and keeps contact points while it draws them;
  `std::nullopt`, the default, switches it off. Both take effect from the next step, so while paused
  the view changes with Step. The shapes are where the step left the bodies, up to a step ahead of
  the meshes drawn between steps. In the game, F3 switches it in Debug builds.
- `NyxPhysicsBodiesTests`: a crate falls onto a floor; a set transform teleports; changed colliders,
  scale and rigid bodies remake the body, and removed colliders or entities destroy it; kinematic
  bodies follow; a force before the step holds a crate up; physics components in scene files.
  The debug view is off by default, draws each step's bodies and contacts while on, and nothing
  from the step after it is switched off.
  Drawing between steps: positions mixed and rotations slerped the short way, a transform set
  since the step drawn where it is; a falling crate keeps its pose from before the step, static,
  still kinematic, sleeping, teleported, made static and removed bodies none; a kinematic body a
  system moves keeps where its transform was as the step began, and after a live edit where it was
  put.
  `NyxSceneGpuTests` draws a cube halfway at half a step, where it is while paused or set from
  outside, and picks it where it is.
- `NyxCharacterTests`: a character falls and lands on a floor; walks 6 m in 2 s at 3 m/s, no faster
  for a longer desired move, and slows at its acceleration; climbs a 0.25 m step and is stopped by a
  0.45 m one (step height 0.3 m); stays put on a 30 degree ramp and slides down a 60 degree one (max
  45); jumps v * v / 2g high and drops a jump asked for in the air; pushes a 5 kg crate along; a ray
  down hits its body and finds its entity, and passes through to the floor when it leaves it out; a
  live edit puts it there, a changed height makes it again, removing the component or entity
  destroys it; walking it keeps its pose from before the step; the debug view draws its 1.8 m
  capsule in 100 lines of its colour; the same inputs among crates give exactly the same path twice;
  the component is saved without its runtime values; the sample scene loads and its character
  stands.
- `NyxMeshColliderTests`: a box dropped on a 35 degree mesh slope lands on it, slides down it and
  comes to rest on the mesh's floor below; a ray down onto a mesh roof, stretched twice along X,
  hits the face beneath it where it is, with that face's normal, misses past the eaves and passes
  through from below; a character walks up a 25 degree mesh ramp and not up a 50 degree one, climbs
  a 0.25 m mesh step and is stopped by a 0.45 m one, as on boxes; the body is made once, kept while
  nothing changes, also when the data changes without a new revision, and made again once for a new
  revision, new data and a new scale; a mesh that isn't valid makes none; without its mesh or its
  collider, the body goes; a rigid body leaves it still, and `PhysicsWorld` refuses a dynamic mesh;
  the debug view draws two triangles in 6 lines where the body is, and a mesh of more triangles
  than asked for as its bounds' 12; the sample ground is valid, as big and bumpy as asked, made once
  and again when its values change; the sample scene loads, and its character and crates stand on
  the ground.
- **The sample scene** `Assets/Scenes/CharacterTest.nyxscene` (written by
  `NyxCharacterTests --write-scene`, with its materials in `Assets/Materials/CharacterTest`): a
  floor, a 25 degree ramp and a 50 degree one, steps of 0.2 m and 0.5 m, four 10 kg crates, a
  character with the built-in capsule mesh (`Meshes/Capsule.nyxmesh`, the default character's size
  standing on its origin) and an orbit camera following it.
- **The mesh sample scene** `Assets/Scenes/MeshTest.nyxscene` (written by
  `NyxMeshColliderTests --write-scene`, with the character test scene's materials): a *Sample
  Ground* (`SampleGroundComponent`, 40 m square, bumps of 0.6 m every 8 m, in 48 by 48 cells) with
  a Mesh Renderer and a Mesh Collider, five crates on it, the character and an orbit camera.
  `SampleGroundSystem` (Sim) makes its mesh in the game, which adds the system to every world; the
  editor shows the scene without it.

## Not built yet

- Mesh colliders from a loaded mesh: the built-in meshes are on the GPU only, and `Sim` doesn't see
  the renderer; once meshes are assets with their data on the CPU, a Mesh Collider can take one.
- Moving mesh bodies: Jolt can move a mesh kinematically; a dynamic one needs a convex hull or
  convex parts.
- Heightfields (Jolt's `HeightFieldShape`), cheaper than a mesh for a grid ground, if the ground
  stays one.

- Constraints as components.
- Path constraints, motors, springs on limits.
- Characters meeting each other's capsules (Jolt's `CharacterVsCharacterCollision`), standing on
  moving characters, crouching (a shape change that checks for room), the push force and mass as
  component values.
