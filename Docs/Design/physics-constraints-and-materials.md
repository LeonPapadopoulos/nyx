# Physics constraints and materials

How constraints join bodies in Nyx, what they report back, and why; and the physics materials that
give bodies their surface and weight, with numbers of a game's own. The physics side is in
`Engine/Source/Runtime/Physics` (`PhysicsWorld`), the components in `Scene`
(`ConstraintComponent.h`, `PhysicsComponents.h`), the material files in `Assets`
(`PhysicsMaterialAsset.h`, a [reflected asset](reflected-assets.md)), and what keeps them in step in `Sim` (`PhysicsConstraints`,
`PhysicsMaterials`). The rest of the physics is in [Physics](physics.md).

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

## Physics materials

A physics material is an asset, `.nyxphysmat`, that colliders name: friction, restitution, density,
and numbers of a game's own. A crate and a cart of the same wood share it; tuning the wood tunes
both, and a game asks what a body is made of, e.g. how readily it burns, without Nyx knowing what
burning is.

```
# Assets/PhysicsMaterials/Wood.nyxphysmat
Friction = 0.6
Restitution = 0.1
Density = 600
Extra.Flammability = 0.8
```

- **The file** is text, as a material file is, so it diffs, merges and is written by hand: a
  [reflected asset](reflected-assets.md) (`ReflectedAssetText`), whose lines are kept as they are:
  comments, blank lines, and lines Nyx doesn't know, for a later Nyx or a game's own tools. Writing
  values changes their own lines alone, adds new ones (a new extra after the other extras) and drops
  an extra that is gone; a value that reads the same keeps its line as written. A value that isn't
  a number is listed and left at the engine's.
- **The values** are `PhysicsMaterialData`, reflected, so the editor draws them as a component's
  properties. Its defaults are the engine's material, which a collider without one has: friction
  0.5, no bounce, the density of water.
- **Extras**: `Extra.<Name> = value`, a name of letters, digits and _: named entries, the property
  `Extras` written `Extra` (`TextName = Extra`). Nyx stores and serves them
  and never reads them: `GetExtra(material, "Flammability", fallback)`. A game defines what it
  needs; a material without one gives the fallback.
- **Colliders** (box, sphere, capsule, mesh) each have a `Material` asset reference
  (`AssetType = PhysicsMaterial`), so the details panel picks from the physics materials and takes
  one dragged from the asset browser. One collider an entity makes its body, so one material a
  body. A `PhysicsMaterialComponent` still sets one entity's friction and restitution over it.
- **`PhysicsMaterials`** (Sim), owned by the game world, reads each material on first use, by its
  path in the asset folders (the project's, then the engine's), and keeps it. A file that isn't
  there or can't be read is logged once and has the engine's values. `GetPhysicsMaterial(world,
  materials, entity)` gives the material of the collider that makes the entity's body;
  `GameWorld::GetPhysicsMaterial(entity)`, and `GetPhysicsMaterial(context, entity)` for systems
  (`SimContext::Materials`), say the same.
- **Bodies**: `PhysicsBodies` gives each body its collider's material: friction and restitution to
  Jolt, the density to the shape (Jolt's `ConvexShapeSettings::mDensity`), which gives the mass and
  inertia unless the rigid body gives a mass. A changed friction or restitution reaches the body
  as it moves (`PhysicsWorld::SetFriction()`, `SetRestitution()`, which wake it and what touches
  it, so a crate asleep on a slope made icy slides); a changed density that gives the mass makes
  the body again, as any value that makes it does. Jolt mixes two bodies' friction as the square
  root of their product, and their restitution as the greater.

### Editing them

Physics materials are a reflected asset type (`RegisterPhysicsMaterialAssetType()`), so the editor
edits them as it edits any ([Reflected assets](reflected-assets.md)):

- The asset browser lists them (type `PhysicsMaterial`); *New Physics Material* in a folder's
  right-click menu writes the engine's values into a new file there and opens it.
- Double-clicking one opens it in its window (`ReflectedAssetWindows`, *Physics Material: Wood*),
  which draws its values with the details table, extras as named entries. It is an object of the
  editor's undo (`EObjectDomain::ReflectedAsset`), as the input asset is, so an edit, its undo and
  redo, and a value being dragged are recorded and previewed like any other, and Ctrl+Z undoes them
  in turn with the scene's. Each material opened is another object of the undo: steps of one closed
  since find nothing and do nothing. *Save* or Ctrl+S, while the window has the focus, writes the
  file. Friction or density below 0 and restitution outside 0 to 1 are problems shown at their values.
- Every change writes the values into the file's lines and counts a revision; each frame the
  revision changed, the editor sends the whole text to the running games (`AssetValuesMessage`,
  link message 17), saved or not, and a game that starts gets the unsaved text first. The game's
  `EditorLinkLayer` hands it to `ApplyAssetValues()`, and the `GameLayer`, listening for
  `.nyxphysmat`, to its world's `PhysicsMaterials::SetText()`, so the next step's bodies have it.
- The game also watches the asset folders (`PhysicsMaterials::Watch()`, `ReloadChanged()` each
  frame) and reads a material in use again when its file changes: saved in the editor, or by hand.
- Why the editor's undo and not one of its own, as a material file has: a physics material has a
  few numbers and no shader to compile, its values are reflected, and sharing the undo gives it the
  details table, live previews and the games' updates for nothing.

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
- `NyxPhysicsMaterialTests`: a wood file read, its values and extras by name, a missing extra the
  fallback, written back unchanged; values changed rewrite their own lines alone, an extra gone
  loses its line, a new one gets one, a name that isn't one is left out, the comment and an unknown
  line stay; saved and loaded; a line that isn't one refused, a value that isn't a number listed;
  the registered type's problems: friction below 0, restitution above 1, an extra named twice and
  one without a name. On
  a 30 degree slope a rubber box holds and an ice box slides; the rubber's friction set live to 0.05
  slides the same body, and the file read again restores it. A cubic metre of pine weighs 600 kg,
  one without a material 1000, one given 5 kg 5; denser live, the pine's body is made again at
  2500 kg and the one given its mass is not. A system reads a straw bale's flammability through its
  context, the game world another extra; a collider without a material, one whose file isn't there
  and an entity without a collider have the engine's; live text with an extra Nyx never heard of is
  served, text that can't be read changes nothing. Scene files keep a collider's material.
- `NyxReflectedAssetEditorTests`: an edit changes its own line, a new extra gets one, undo and redo
  put the text back, a value being dragged reaches the text at once, a density below 0 is a problem;
  saving writes the file; a material opened again is untouched by the last one's undo, one that isn't
  there doesn't open; the window draws headless and Ctrl+S in it saves.
- `NyxLiveEditTests`: `AssetValues` reads back as written and is described by its path and lines.
