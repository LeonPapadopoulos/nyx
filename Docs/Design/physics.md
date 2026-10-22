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
- **Bodies** are made from a `BodyDesc`: a shape (box, sphere, capsule), a pose, a motion (static,
  kinematic, dynamic), mass (or the shape's volume at the density of water), friction,
  restitution, damping, gravity factor, and a number of the owner's (an entity's guid).
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
  default), and one body to ignore, e.g. the caster.
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
- **Jolt's own reports** go to Nyx's log; a failed Jolt assert (Debug builds) is logged, and breaks
  under a debugger.

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
limits, a slider its body on its axis, a fixed constraint two bodies together.

## In the game world

- **Components** (`PhysicsComponents.h`, in Scene so they are reflected, saved and edited):
  `BoxColliderComponent`, `SphereColliderComponent`, `CapsuleColliderComponent`, a
  `RigidBodyComponent` that makes the body move (dynamic, or kinematic), and an optional
  `PhysicsMaterialComponent`. A collider alone is a static body. Small parts that combine, not one
  component with a shape switch; one collider an entity (box over sphere over capsule).
- **The entity's transform** places the body, and its scale scales the shape: a box by each axis,
  a sphere by the largest, a capsule's radius by X and Z and its length by Y.
- **`PhysicsBodies`** (Sim) keeps the bodies in step with the components, in `GameWorld::Step()`:
  - before the physics step: bodies made for new colliders, made again (at the transform) when a
    component's values or the scale changed, destroyed with their collider or entity;
  - a transform set since the last step, by a system or an editor's live edit, teleports its body
    and stops it. Compared exactly with what the bodies wrote back, so only real changes count;
  - kinematic bodies are moved to their transforms over the step;
  - after the step, dynamic bodies write their poses into their transforms.
  - Bodies are kept by entity in a fixed order, so they are made and destroyed the same way each
    run. A body's user data is its entity, for `FindEntity()`; never saved.
- **Systems** get the physics world and the bodies in `SimContext`, and run before the physics step
  (`ESimStage::BeforePhysics`, e.g. to push) or after it (`AfterPhysics`, e.g. to react).
- `NyxPhysicsBodiesTests`: a crate falls onto a floor; a set transform teleports; changed colliders,
  scale and rigid bodies remake the body, and removed colliders or entities destroy it; kinematic
  bodies follow; a force before the step holds a crate up; physics components in scene files.

## Not built yet

- Constraints as components; the debug view of shapes and contacts through `DebugDraw`.
- Path constraints, motors, springs on limits; a character controller.
- Drawing moving bodies between their last two steps.
