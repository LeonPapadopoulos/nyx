# Wind

One wind for a world that everything moving with the air reads: grass and trees, particles,
clouds, a game's own air. It is made to be the first channel of a weather system to come (rain and
storms will drive it together with clouds, fog and light), so it can be steered and stays smooth
while it changes, but it owns no weather of its own: no presets, no random drift.

Where it is:
- the contract: `Engine/Source/Runtime/Core/include/Wind.h` (`WindState`, `WindPattern`,
  `SampleWind()`, `SampleWindGust()`, `GetWindHeightFactor()`) and its twin in the shader library,
  `Engine/Shaders/Library/nyx/wind.slang` (`nyx::sampleWind()`, `nyx::sampleWindGust()`,
  `nyx::windHeightFactor()`);
- the producer: `WindComponent`, its drive (`WindDrive`, `SetWindTarget()`, `EvaluateWind()`) and
  `UpdateWorldWind()` / `GetWorldWind()` in `Scene/include/WindComponent.h`;
- a game's local air: `IWindField` in `Scene/include/WindField.h`;
- the readers' glue in the renderer: `VulkanRenderer::ExtractWind()`, `PrepareSceneViewWind()`, the
  view constants' `Wind` block, `VulkanLocalWind` (`LocalWind.h`), the debug view (`WindDebug.h`),
  and `nyx.worldwind` (`nyx::windAt()`, `nyx::localWindAt()`) for materials.

## Who depends on whom

Readers know the contract and nothing else; producers never know their readers.

```
                         Wind.h (Core)  /  nyx.wind (shaders)
                         WindState, SampleWind()
              ^                 ^                  ^                    ^
              |                 |                  |                    |
   WindComponent.h (Scene)   Particles.h       nyx.worldwind       a game's systems
   the values, the drive     (Render)          (+ nyx.view)        (SimContext::Wind)
              ^                 ^                  ^
              |                 |                  |
   GameWorld (Sim): turns   VulkanRenderer: the glue that finds the world's wind (its
   the drive each step      WindDriveComponent, or its own drive in the editor) and hands
                            the WindState to particles, the view constants and the debug view
```

- `Wind.h` includes only glm. `wind.slang` imports nothing.
- `WindComponent.h` includes `Wind.h`; nothing in it or in `Wind.h` includes a reader's header.
- `Particles.h` includes `Wind.h` and takes a `WindState` in `ParticleAir`; it never sees the
  component. `WindField.h` includes only glm.
- Materials import `nyx.worldwind`, which imports `nyx.view` (the view's `Wind` and local air) and
  `nyx.wind`. A shader that doesn't import it is untouched.
- A game's systems read `SimContext::Wind` (a `WindState`) and call `SampleWind()`. A weather
  system later writes the `WindComponent`'s values; it needs nothing of the readers either.

## The contract

`WindState` is 144 bytes of plain floats, the same on the CPU, in the view's uniform buffer and in
a storage buffer (laid out in whole 16 bytes, so std140, std430 and scalar layouts agree).
`SampleWind(state, position, time)` gives the wind's velocity at a point of the world at a time, in
metres a second, along the ground. It is the only way to read it.

What the wind is, at a point:

1. **Along the prevailing direction**, at *Speed* at the reference height.
2. **The height profile**: *Near Ground* of the speed at the ground (the wind's own ground height),
   rising to all of it at the *Reference Height* (`q(2 − q)`, flat at the top), then up to
   (1 + *Aloft*) times as fast at *Aloft Height* (the same easing). A simple profile, stronger
   aloft; clouds at 2 to 8 km get the top of it.
3. **Gust fronts**, bands across the wind that travel along it at *Frequency* × *Spacing* metres a
   second: at a place the wind is (1 + *Gustiness* × g) as fast, g rising sharply from 0 to the
   front's strength as it arrives and dying away slowly to 0 before the next. Each front's strength
   changes along it, patch by patch (*Patch Width*), from a hash of the front's number and the
   patch's: some places get a strong gust, others a weak one, so a squall sweeps visibly over the
   ground instead of the whole world flickering at once. Neighbours lean together, a field ripples.
4. **Eddies**, a 2D value noise of vectors, *Eddy Size* apart, carried by the wind (Taylor's frozen
   turbulence), *Turbulence* of the speed.

The fronts' phase and the eddies' offset are numbers in the state, at its `Time`: asked at another
time, they move on at the state's rates. They are wrapped by whole repeats of their patterns
(1024 fronts, 1024 eddies), so they stay small and exact after hours of wind.

### The same to the bit on the CPU and the GPU

`SampleWind()` and `nyx::sampleWind()` only add, multiply, floor and hash integers, in the same
order; every division is made beforehand into the state's numbers (`GustsPerMetre`, not a spacing).
Vulkan rounds additions and multiplications correctly, floors and integer arithmetic are exact,
so the two agree to the bit, with one catch: a driver may fuse a multiply and an add into one FMA,
which rounds once instead of twice. GLSL's and HLSL's `precise` stops that with SPIR-V's
`NoContraction`, but the Slang of the Vulkan SDK used here (1.4.341) doesn't emit it. So every
product in `wind.slang` goes through `windMul()`, which XORs its bits with `WindState::Exact`: a
uniform the compiler can't know (always 0), so the product must be rounded on its own. One integer
operation a product. The CPU side is built with MSVC's default `/fp:precise`, which doesn't contract
either; a build with `/fp:fast` or `/fp:contract` would break the agreement (and the determinism of
the simulation, which matters more).

`NyxWindGpuTests` holds the two to the same bits at 4096 points of four winds (a hand-made one
mid-fade, the component's default, the default after 100 hours, and one turning to a storm), near
the origin and 5 km out, low and 3 km up.

## Driving it

`WindComponent`'s values are the wind's targets. Whoever drives the wind sets them: the editor, a
live edit, a game's code, a weather system later. The wind turns to them over *Change Time*
(`WindDrive`):

- **Every number blends** from where it is to its target along a smoothstep: speed, gustiness,
  frequency, turbulence, the height profile; the direction by the shortest way round. *Blowing* off
  is a speed of 0, so the wind dies down and picks up again.
- **The phases never jump.** The fronts' phase and the eddies' offset are the integrals of their
  rates (the frequency, the wind's velocity), worked out exactly over the smoothstep
  (`∫ smoothstep = s³ − s⁴/2`), from the moment of the change. So a frequency or speed that
  changes keeps every front where it was and changes how fast it moves.
- **Patterns fade, they don't turn.** Which way the fronts lie, their spacing and patches and the
  eddies' size form a pattern (`WindPattern`). Turning a pattern about the world's origin would
  sweep fronts over the land far from it (a 30° turn over 8 s races them at 50 m/s 500 m out), so
  instead the state holds the pattern before and the new one, and the wind fades from one to the
  other over the change (`PatternBlend`). While steady, only one is sampled. A new pattern set while
  one fades in waits for it (`WindDrive::Pending`, the latest wins), so a driver that turns the wind
  a little every step gets one fade after another, while the mean direction follows at once.
- **The same targets at the same times give the same wind**, at any time, whatever the steps or
  frames between: the drive is a function of when its targets were set and of the time asked,
  nothing accumulated per step. `NyxWindTests` checks a drive stepped at 60 Hz and at 2 Hz against
  one set twice, to the bit.

How a driver sets targets (the test's `StormDriver`, a sim system that stands in for the weather):

```cpp
void Step(const SimContext& context) override
{
    context.World.Each<WindComponent>([&](Entity, WindComponent& wind)
    {
        wind.ChangeSeconds = 6.0f;
        wind.Speed = time < 2.0 ? 0.0f : (time < 6.0 ? 4.0f : 18.0f);
        wind.Gustiness = time < 6.0 ? 0.5f : 1.2f;
        wind.DirectionDegrees = time < 6.0 ? 20.0f : 75.0f;
    });
}
```

`GameWorld::Step()` turns the drive at each step's start (`UpdateWorldWind()`, after the time of
day), keeps it on the wind's entity (`WindDriveComponent`, run-time, shown in the editor's runtime
section) and hands the wind to the systems (`SimContext::Wind`, `GameWorld::GetWind()`). The
renderer draws a game's world from that drive, evaluated at the time it draws (between steps), so
the game's systems and the GPU see the same wind. The editor's worlds have no steps: the renderer
keeps a drive of its own for them, turned from frame to frame, so a value dragged in the Details
panel blends too. A test drives two game worlds alike from calm to a storm and gets the same wind to
the bit, no step jumping more than 1.5 m/s.

The world's first `WindComponent` counts; without one, the air is still.

## Readers

### Particles

`ParticleAir` holds the world's `WindState` and a game's `IWindField`: each particle is pulled
towards `SampleWind()` at its place plus what the field adds there (one call a frame per emitter, for
all its particles). The environment's old *Wind* (a vec3, the same everywhere) is gone: scenes saved
with it get a steady `WindComponent` on the environment's entity as they load (no gusts, no eddies,
the same at every height: the same air), unless they have a wind already
(`MoveLegacyEnvironmentWind()`, a scene load step). Its up-and-down part, which nothing in Nyx's
scenes used, is dropped with a warning. `Assets/Scenes/ParticlesTest.nyxscene` is saved again with
the new component.

### Grass, leaves, trees

`nyx::windAt(position, time)` (`nyx.worldwind`) is the view's wind at the world's time plus the local
air. `Shaders/VertexColorWind.slang` reads it at each instance's origin, a little above it
(*Wind Height*), so a whole plant leans as one. The material keeps only how it answers:

- *Response*, 0 to 1: 0 holds still in any wind.
- *Strength*: how far a tip leans at most; *Stiffness*: the wind (m/s) that leans it about 70 %
  of that, about in proportion below it (`x / sqrt(1 + x²)`), never past it.
- *Flutter*, *Flutter Frequency*: the quick sway, each copy at its own phase, as strong as all the
  air moving it, so rising air (a fire's updraft) shakes it too. The frequency doesn't follow the
  wind's speed, which would make the phase jump as the speed changes.

A tip moves less than *Strength* × (1 + *Flutter*), 0.2025 m with the defaults; the material's
*Max Vertex Offset* is 0.21. `Assets/Scenes/GroundTest.nyxscene` gets a default `WindComponent`.

### A game's local air for vegetation

A game's air (`IWindField`) is asked each frame for a coarse texture round each view's camera
(`VulkanLocalWind`): *Texture Cells* along x and z (48), *Texture Layers* high (12), *Cell Size*
metres (1), on whole cells of the world so it doesn't swim, from two thirds of its height below the
camera to a third above. `IWindField::SampleWindGrid()` fills it; a field that knows where its air
is writes only those cells, `SampleWindGridAtCentres()` asks for every one. The texture
(RGBA16F) is copied to the GPU in the frame's own commands only when what it holds changed; with
nothing in it the view has none (`ViewConstants::LocalWind` 0) and the shaders read the world's wind
alone. It fades out over its outermost cells, so its edge never shows.

### Clouds

The high cloud layer (`CloudLayerComponent`) and the volumetric clouds (`VolumetricCloudsComponent`)
take the world's wind at their altitude: the layer at its *Altitude*, the band at the middle of its
*Bottom* and *Top Altitude*. They read it as a `CloudDrift` (`CloudLayer.h`, made by
`GetCloudDrift()` from the world's `WindState`): the mean wind there, without gusts or eddies, which
clouds kilometres across wouldn't show, and how far it has carried them, the wind's own travel at
its reference height (`GetWindTravel()`, the exact integral of its velocity, in doubles, never
wrapped) times its height profile at their altitude. So clouds at 4 km in the default wind (4 m/s,
*Aloft* 1) drift at 8 m/s, their streaks along the wind, and turn and speed up with it, smoothly.

- **Their own wind stays, as an override**: *Own Wind* on, the clouds blow by their *Wind Direction*
  and *Wind Speed* as before. A world without a `WindComponent` always uses them, so every scene
  saved before the wind looks as it did, and so do the clouds' tests and pictures.
- **Old scenes**: one whose environment's particle wind is moved into a `WindComponent` as it loads
  gets *Own Wind* on its clouds in the same step, so they still look as they did.
- **Limits**: the travel is scaled by the profile as it is now, so a change of *Aloft* or the
  reference height moves the clouds a little at once (by the change times how far the wind has
  carried them); the layer's streaks turn with the wind about the world's origin while it turns.
  Both are slow at cloud scale. The motion the volumetric clouds take back between frames is the
  drift's velocity, as it was the own wind's.
- Tests (`NyxWindTests`): a hand-made wind at 4 km carries both kinds twice as fast as at its
  reference height, as far as its travel says, the layer's streaks along it; *Own Wind* and a world
  without a wind give the clouds their own; the travel steady, across a change, after it; an old
  scene's clouds keep their wind.

## The debug view

The wind component's *Debug View*: *Arrows* draws an arrow every 3 m round each view's camera, 1 m
above the wind's ground height, along the wind and as long as half a second of it, from blue in a
lull to red at a front's peak; *Arrows And Gust Fronts* adds the crest of each front within 60 m, a
line across the wind with strokes up from it as tall as it is strong there. Lines through the debug
lines of the view (`SceneViewInstance::WindLines`), in Debug builds (as all debug drawing).

## Tests

- `NyxWindTests`: a calm and a steady wind with hand-made states; the profile; fronts carried
  downwind at their speed, coming and going at a place, stronger in places along their crest; long
  winds; turning smoothly (no frame of a ramp from calm to a storm jumps more than the storm's own
  gusts move it; the same targets at the same times give the same bits whatever the steps; a turn
  started halfway through another; dying down); a driver ramping a game world's wind from calm to a
  storm, read by a system, the same in two worlds; a game's air on a grid; the debug view; an old
  scene's wind moved.
- `NyxWindGpuTests`: the bits; a probe material showing `nyx::windAt()` at a point: still without a
  wind, the editor's wind, a game's drive mid-turn, a game's local air near it and not away from it,
  its texture copied once while it stays and again when it changes; grass bends in a gale and not
  at a *Response* of 0.
- `NyxParticleTests`: particles in a hand-made wind, with a fake game's air on top and alone, near
  the ground in a gusty wind; an old scene's environment wind moved into a wind.
- `NyxParticleGpuTests`: its winds as steady `WindComponent`s, the pictures as before.
- `NyxGameWorldTests`, `NyxGroundTests`: a world keeps its game's air; the ground sample scene
  has a wind.

## What it costs

Release, 1280 x 720, RTX 3070 Ti, `NyxInstancingBenchmark wind` and `instanced ... VertexColorWind`
(medians of 300 frames):

| What | Without | With |
| --- | --- | --- |
| `AddWind()` on the CPU, a point, steady / fading between patterns | | 27 ns / 51 ns |
| 50 000 cubes in `VertexColorWind` (1.8 million vertices), GPU, from above | 0.90 ms (calm) | 0.89 ms (the default wind) |
| The ground test scene's 3 041 blades of grass, GPU | 0.55 ms (still) | 0.56 ms |
| A game's local air changing every frame (48 x 12 x 48 cells, every cell asked), scene CPU | 0.05 ms | 0.25 ms |
| `UpdateParticles()` for 10 emitters of 2 000 particles, CPU | 0.29 ms (still) | 0.70 ms (the default wind) |

- **The GPU pays nothing measurable** for the wind in a vertex function: about 150 arithmetic
  operations and 6 hashes a vertex, within the noise even at 1.8 million vertices. The vertex
  function itself costs what it did ([Vertex data](vertex-data.md#what-it-costs)).
- **Particles** pay for being carried by a wind that changes from place to place: an emitter's
  particles take the wind from a lattice round them (`AddParticleWind()`, a quarter of an eddy
  apart, at most 2 m), so 2 000 particles cost a few dozen samples and the blend; the 0.4 ms for
  20 000 particles is mostly that blend and copying their places. A steady wind costs the same; a
  calm world nothing.
- **A game's local air** costs what its field takes to answer: the benchmark's field answers every
  one of the 27 648 cells through `SampleWindGridAtCentres()`, 0.2 ms; a field that writes only its
  own cells (a sparse field's bricks) costs a fraction. The texture (432 KB) is copied only when
  it changes.
- **The drive** costs nothing to speak of: a few hundred operations a step and a frame.

## Later, and where it goes

- **Weather.** A weather system sets the `WindComponent`'s values over time, as the test's
  `StormDriver` does, and owns its presets and drift; nothing in the wind changes. Storm squalls are
  stronger *Gustiness* with closer *Spacing* and narrower *Patch Width*.
- **Wind turning with height** (veering): a second direction in the profile, `WindState` growing by
  a direction aloft and `SampleWind()` turning between them with the same easing as the speed. The
  readers don't change; clouds would get the turn at their altitude (`GetCloudDrift()`).
- **The wind pushing physics bodies**: a sim system reading `SimContext::Wind` and applying drag
  forces to bodies by their area, as a game's own air may push them today; it would
  live in `Sim` beside the physics systems, a reader like any other.
- **Rain**: particles already carried by `SampleWind()`; a rain system would be one more reader.
- **A pivot nearer the players** would not be needed: patterns fade instead of turning.
- **Vertical wind in the prevailing wind** (thermals, downbursts): a third component of the
  velocity in `SampleWind()`; today only a game's local air rises.
- **The previous frame's wind** for motion vectors: `nyx::windAt(position, time)` already takes the
  time; the local air's texture would need the frame before's too.
