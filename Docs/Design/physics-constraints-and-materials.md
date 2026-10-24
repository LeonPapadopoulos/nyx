# Physics constraints

How constraints join bodies in Nyx, what they report back, and why. The physics side is in
`Engine/Source/Runtime/Physics` (`PhysicsWorld`), the components in `Scene`
(`ConstraintComponent.h`), and what keeps them in step in `Sim` (`PhysicsConstraints`). The rest of
the physics is in [Physics](physics.md).

## A rest in each body's own space

A constraint holds two bodies, or a body and the world, as they are at its *rest*. Phase 2 gave the
rest in world space, as the bodies stood when it was made: fine for a joint made where things
already are, wrong for one a game makes on things that are elsewhere, or one saved in a scene and
made again after the bodies moved.

So a `ConstraintDesc` can now give each end in its own body's space
(`EConstraintSpace::Local`): a `ConstraintFrame` on Body and one on Other, each a point and a
rotation about the body's position (the world's own for the world). The rest is where the two frames
meet, wherever the bodies are when it is made; bodies away from it are pulled there.

- The frame's X axis is what a hinge turns about and a slider moves along; its Y is where a hinge's
  angle is 0. One rotation says both, and two frames turned differently make a rest in which the
  bodies are turned to each other.
- Jolt takes local frames about a body's centre of mass (`LocalToBodyCOM`); Nyx's are about the
  body's position, so `PhysicsWorld` moves the point by the shape's centre of mass. For Nyx's
  shapes that is no move at all, but a body made with an offset shape later stays right.
- The world-space form stays (`EConstraintSpace::World`, the default), for joints made where
  things stand, and Phase 2's callers.

## Kinds, limits, motors and springs

Fixed, point, hinge, slider and distance, as before, each in either space.

- **Limits**: a hinge's in radians from its rest (Min in [-pi, 0], Max in [0, pi]), a slider's in
  metres (Min <= 0 <= Max); a distance keeps its points between a least and a greatest distance.
- **Springs**: `SpringFrequency` above 0 makes a distance's range, or a hinge's or slider's limits,
  a spring of that frequency and damping instead of a hard stop: Jolt's limit spring, no cost.
- **Motors**: a hinge or a slider can be driven, by velocity (radians or metres a second) or to a
  position (radians or metres from the rest, with a frequency and damping, like a spring), with at
  most `MaxForce` (N m or N; 0, no limit). `SetConstraintMotor()` changes it at once without
  making the constraint again, so a system can drive one every step. It wakes the bodies.

Jolt's other constraints (cone, swing-twist, six degrees of freedom, path, gears, pulleys) wait
until a game needs them; each would be one more kind and a case in `CreateConstraint()`.

## The load

After each step a constraint's *load* is what it did to its Body: the force (N) it held it with at
its point, and the torque (N m) it kept it from turning with, in world space; Other got the
opposite. `GetConstraintLoad()` reads it from Jolt's total lambdas, the impulses each part of the
constraint applied over the step, divided by the step's time.

- Most parts keep a vector in world space (a point's, a fixed joint's position and rotation, a
  slider's rotation). The rest keep numbers along axes Jolt works out from the bodies at the start
  of each step: a hinge's rotation part pushes along two perpendiculars of body 2's hinge axis, a
  slider's position part along two perpendiculars of its axis, a distance along the line between
  its ends. Those axes are worked out again from where the step left the bodies, which is as near
  as anything outside Jolt's solver can get.
- The torque is what the constraint's rotation parts did, not the turn its force gives about the
  body's centre: a box welded a metre out from a point holds its weight as force and the weight
  times the metre as torque.
- Jolt keeps the lambdas of a constraint that is off, or whose bodies sleep, as they were the last
  step it held, so a resting body keeps reading its load, and a broken constraint reads the load
  that broke it.

## Off and on, and breaking

- **Off and on** (`SetConstraintEnabled()`): off, Jolt skips the constraint, which keeps its rest
  and costs almost nothing; on again, it pulls its bodies back. Either way both bodies wake, so
  nothing hangs asleep in the air. Switching on resets Jolt's warm start: what it held last time is
  no guess for what it holds now.
- **Breaking**: `BreakForce` and `BreakTorque` above 0 are the most it takes, `BreakSeconds` for how
  long (0: one step). After each step `PhysicsWorld` reads the load of every breakable constraint
  that is on; one past a threshold counts the time up, one under it starts again. At the time
  (half a step's slack, so rounding doesn't cost a step) it switches itself off, wakes its bodies
  and is listed with the load that broke it (`TakeConstraintBreaks()`, in handle order, at most
  `MaxConstraintBreaks` waiting). Switching it on mends it.
- Breaking off rather than destroying keeps the handle, so a game can mend what broke.

## In the game world

- **`ConstraintComponent`** (Scene, reflected, saved and edited): the kind; the entity whose body
  it holds (`Body`, none: its own) and the other (`Other`, none: the world), by guid; whether it is
  on; each end's point and rotation in its entity's space, points scaled with the entity as its
  shape is; limits (a hinge's in degrees), distances, the spring, the motor, and breaking. It can
  sit on either entity or a third, so one body can hold as many constraints as it has holders, and
  a game can make and drop holders as it joins and lets go of things.
- **`ConstraintStateComponent`** (Scene, runtime only, never saved or sent): whether it holds now,
  whether it broke, and the force and torque of the last step.
- **`PhysicsConstraints`** (Sim), in `GameWorld::Step()`:
  - before the step, after the bodies and the guid lookup: a constraint is made once both bodies are
    there, by `PhysicsBodies::FindBody()` (a character's body too); made again when its kind,
    frames, limits, spring or breaking change, when its entities change, or when a body is made
    again (which takes its constraints with it in Jolt); destroyed with its component or entity.
    An `Other` named but missing, or without a body, isn't the world: the constraint waits.
  - switching on and off and the motor reach the constraint without making it again, compared with
    what it was last told, so a system can drive a motor each step at no cost;
  - after the step, each state takes its load; a constraint that was on before the step and is off
    after it broke: its component is switched off, its state says so, and it is listed in
    `GetBrokenConstraints()` (with the load) until the next step, for the systems of
    `ESimStage::AfterPhysics` (`SimContext::Constraints`).
  - In the order of their entities, so the same scene gives the same constraints, in the same order
    in Jolt's solver, each run.
- Making one again (a changed value, a live edit) starts it cold: Jolt's warm start goes with the
  old one, so a long chain may sag for a step. Cheap otherwise: a constraint is no body.
- The editor shows the component like any other; entity fields pick by name, drop from the outliner
  or the eyedropper ([Entity references](../Features.md#entity-references)). The editor doesn't
  simulate; the game started with Play gets every edit, and makes the constraint again.

## Tests

- `NyxPhysicsTests`: a box hung by its top from a point of the world, given in each one's own
  space, is pulled up to its rest and its load is its weight (2%), no torque; a box welded a metre
  out holds its weight and 9.81 N m; a slider along Z turned in its frames stays on its axis
  within its limits, a position motor pulls it to 1 m and a velocity motor to its upper limit;
  switched off a hung box falls and keeps its last load, switched on it is pulled back and held; one
  made off holds nothing; a hook of 30 N for a quarter of a second, pulled with 39.6 N, breaks in
  exactly the fifteenth step, tells the load once and stays, off; a weld past its torque breaks at
  once; a chain of two boxes hangs at its rests, the link holding the lower box's weight and the top
  both; a body's constraints go with it; a chain, a driven door and a sprung slider end bit for bit
  alike after 600 steps; a constraint switched off isn't drawn.
- `NyxConstraintTests`: a crate hung by its component rests where its frames meet, its state
  reading its weight; switched off it falls, switched on it is held again by the same constraint;
  past its break force it breaks in the first step, says so to an after-physics system once,
  switches its component off and mends when switched on; a third entity links two crates by guid,
  the link and the top holding their weights; a changed frame and a body made again make it again;
  without its other body it holds nothing, without its component its state goes; a hinge's
  position motor opened a degree each step turns a door a quarter round without making it again;
  scene files keep every value and never the state; a live edit from another world moves the rest,
  an entity sent again replaces the constraint; three linked crates, a driven door and a shoved
  drawer end bit for bit alike after 600 steps.
