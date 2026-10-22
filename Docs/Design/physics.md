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
- **Jolt stays inside the folder.** `PhysicsWorld.h` speaks glm and an opaque `BodyHandle`; only
  `PhysicsWorld.cpp` includes Jolt. The engine links Jolt privately.
- **Bodies** are made from a `BodyDesc`: a shape (box, sphere, capsule), a pose, a motion (static,
  kinematic, dynamic), mass (or the shape's volume at the density of water), friction,
  restitution, damping, gravity factor, and a number of the owner's (an entity's guid).
- **Collision layers** follow from the motion: static bodies collide with moving ones, moving ones
  with everything. One table in `PhysicsWorld.cpp`; triggers and query-only layers come with the
  queries.
- **Forces** (`AddForce`, `AddForceAtPoint`, `AddTorque`) act during the next step and then go.
  A force off the centre of mass turns the body by itself: no extra code for torque.
- **Teleport** puts a body somewhere at once and stops it, e.g. for an editor's edit.
  **MoveKinematic** moves a kinematic body over the next step, pushing what it meets.
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
spheres, capsules, user data, destroyed bodies.

## Not built yet

- Components (`RigidBodyComponent`, `ColliderComponent`), bodies made and removed with them, and
  the physics step in the `GameWorld`, writing back the transforms.
- Queries (raycasts, shape casts, overlaps) and contact events handed to the main thread.
- Constraints as components; the debug view of shapes and contacts through `DebugDraw`.
- Drawing moving bodies between their last two steps.
