# Lights and particles

Light from points, such as a lamp or a fire, and how it gets to the pixels; particles, such as
sparks, embers and smoke, moved on the CPU and drawn instanced; and why they work as they do. The
settings are on `PointLightComponent` and `ParticleEmitterComponent` (`Scene`), with the world's
particle budget on `EnvironmentComponent` and its wind on `WindComponent` ([Wind](wind.md)); the
math is in `Engine/Source/Runtime/Render/PointLights.h` and `Particles.h`, a game's air comes in
through `WindField.h` (`Scene`); the renderer's part is in `VulkanRenderer` (`ExtractPointLights()`,
`ExtractParticles()`, `CullSceneView()`, `UpdateSceneUniforms()`) and `VulkanParticles`, the
shaders in `Engine/Shaders/Library/nyx/view.slang`, `lighting.slang` and
`Engine/Shaders/Particle.slang`.

## Point lights

A *Point Light* (`PointLightComponent`, reflected, with undo, saved in scenes, sent to a running
game) shines all round from its entity's position: *Color*, *Intensity* and *Range*. The entity's
rotation and scale change nothing. A light moving with a body is drawn between the body's last two
steps, as its mesh is, so the two don't part.

- **Units, as the sun's.** The sun's colour is what a white matte surface facing it shows; a point
  light's intensity is the same a metre away. So a light of intensity 1 a metre above a white floor
  lights the floor below it as the sun overhead would, and the two can be balanced by eye.
- **Falloff.** The inverse square of the distance, as light from a point spreads, times a window
  that brings it to 0 at the range without an edge: `(1 - (d / r)^4)^2 / max(d, 0.1)^2`
  (`GetPointLightFalloff()`, `nyx::pointLightFalloff()`; the window is Unreal's and Frostbite's).
  Near the light the window is 1 and the light is physically right; towards the range it bends the
  curve down smoothly, so the range is where the light's cost ends, not a visible circle. 0.1 m is
  the size of a small bulb: nearer, the light is no brighter, so a light on a surface is finite.
- **Lit like the sun.** The same physically based model: Lambert diffuse and the GGX lobe, from
  the light's direction at the pixel (`nyx::directLight()`, shared by the sun and the lights). So
  metals reflect a light's highlight and rough surfaces spread it.
- **The direct part.** `nyx::lightSurfaceApart()` adds the lights to the sun's light, never to the
  sky's: ambient occlusion keeps dimming the sky's light alone. Their light then goes through the
  fog as all of a pixel's light does: dimmed by the fog's transmittance to the camera, the fog's own
  light added. Decals are lit as what they lie on; an unlit material isn't.
- **What gives no light isn't drawn**: an intensity or range of 0, or a black colour.

### Per view, a list

The renderer keeps nothing of a light on the GPU between frames; each frame:

1. `ExtractPointLights()` collects the world's lights once, for all its views (`RenderWorld::PointLights`).
2. Each view culls them by its camera (`CullPointLights()`): a light is kept if its sphere of
   light, its range round it, isn't wholly outside the view's frustum. A light behind the camera
   whose light reaches into view is kept; one far off to the side isn't. With the view's culling
   frozen (*Debug > Freeze Culling*), lights are culled by the frozen camera, as meshes are.
3. At most `MaxViewPointLights` (256) are kept; past that, those whose light reaches nearest the
   camera, ties by the world's order, so the choice doesn't flicker. The view's culling statistics
   count the lights, those culled and those dropped (`SceneViewCullingStats`).
4. `UpdateSceneUniforms()` writes the view's lights into its own storage buffer (the second binding
   of the view's set, `nyx.view`'s `PointLights`, 32 bytes a light, `ViewPointLight`), and their
   number into the view's constants (`ViewConstants.PointLightCount`). One frame in flight: the
   buffer is written after the last frame's fence, while no frame reads it.
5. Every lit pixel goes through the view's lights: past a light's range (`d² · 1/r² >= 1`, three
   multiply-adds and a compare) it skips it; within, it adds the light.

A storage buffer rather than an array in the view's constants: the constants stay 960 bytes for
views without lights, the buffer's size is not bound by a uniform buffer's 16 KB, and a cluster
grid (below) becomes a third binding beside it. Without lights, the loop runs no times: every
picture without them is the same to the bit (the reference pictures of the other tests are
unchanged).

### When tiles or clusters are needed

Every lit pixel pays for every light in view: a distance test each, and the lighting where it is
in range. The test is cheap, but it is per pixel and per light: 64 lights in view cost 0.25 to
0.5 ms at 1280 × 720 on an RTX 3070 Ti (numbers below), about 5 to 8 µs a light a million pixels.
The plain list stays right while:

- a view sees no more than a few dozen lights at once, and
- lights are small against the view, so most pixels skip most of them early.

Past that, at 1920 × 1080 with a hundred lights in view, or with many lights that cover much of the
screen, the tests alone take a millisecond or more and **clustered lighting** pays: the view's
frustum cut into a grid of cells (say 16 × 9 across and 24 slices in depth, the slices growing with
the distance), each cell with the list of lights whose sphere reaches it; a pixel finds its cell by
its place and depth and goes through that cell's lights alone. How it would slot in:

- `CullPointLights()` stays as the first step: the view's lights, as now.
- A step after it bins them into the cells: on the CPU for a few hundred lights (sphere against a
  cell's box, about 3 500 cells), or in a compute shader before the scene pass for more. The cells'
  offsets and counts and the lists of light indices go into a third binding of the view's set.
- `lightSurfaceApart()`'s loop goes over the pixel's cell's indices instead of all of the lights;
  nothing else in the shaders changes, and the lights' buffer stays as it is.

Tiled lighting (2D tiles with a depth range from a depth prepass) needs a prepass, which Nyx doesn't
have (the scene pass draws each mesh once); clusters don't, so clusters are the next step.

### Shadows, later

Point lights cast no shadows yet. Room is left for them:

- `ViewPointLight`'s fourth word is free: an index into the shadowed lights' maps, or none.
- The component would gain *Cast Shadows*; the renderer would pick a few shadowed lights a view
  (the nearest, a budget like 4), each with a cube map (six faces, or two paraboloids), its casters
  culled by the light's sphere as the cascades' are by their frustums, drawn into an atlas before
  the scene pass, with the materials' caster pipelines (`Material::ShadowPipeline`), so a vertex
  function moves a mesh's shadow too.
- In the shader, a light's term multiplied by its shadow, as the sun's is by `nyx::sunShadow()`.

### Emissive surfaces don't light

An emissive material or decal glows, and blooms, but lights nothing round it: emission is what the
surface sends towards the camera, not light in the scene. A game pairs a glowing surface with a
small point light where it should light its surroundings:

- the light on the same entity, or on one at the surface a few centimetres out along its normal,
  so it doesn't light the surface's own back;
- its colour the surface's, its intensity in proportion to the glow's brightness and size, its
  range small (1 to 3 m for a mark the size of a hand), which keeps it cheap;
- turned on, dimmed and off with the glow, by the same system that sets the glow (the light's
  *Intensity*, the decal's *Intensity* or *Reveal*);
- many glowing marks close together share one light at their middle rather than one each.

The lights test scene (`Assets/Scenes/LightsTest.nyxscene`) pairs its glowing cube and its glowing
ring with lights this way.

## Particles

A *Particle Emitter* (`ParticleEmitterComponent`, reflected, with undo, saved in scenes, sent to a
running game) gives off particles from its entity's position: sparks, embers, smoke, the flames of
a fire. Its settings:

- **Emission**: *Emitting*, *Rate* (particles a second), *Bursts* (many at once at moments of the
  emitter's life, once or again every *Interval*), *Lifetime* (a range), *Spawn Radius* (born
  anywhere in a ball).
- **Motion**: *Direction* and *Cone Angle* (each born heading somewhere in the cone, as likely
  anywhere in it), *Speed* (a range), *Gravity* (down), *Buoyancy* (up, as warm smoke rises),
  *Drag* (how fast a particle takes on the air's speed), *Spin*, *Local Space* (moving with the
  entity, like a torch's flame, or left where born, like a trail of smoke).
- **Look**: *Shape* (a billboard, a soft round quad facing the camera, or a small mesh, such as
  an ember: *Mesh*, the cube without one), *Size* (a range), *Size Over Life* and *Color Over Life*
  (a few keys each, between which it goes straight; colour and alpha), *Blend* (Additive or
  Alpha), *Emissive Intensity* (the colour times this: above 1 it glows and blooms), *Lit*, *Soft
  Distance*.
- **Budget**: *Max Particles*, and *Seed*.

The world's first *Environment* has the rest, under *Particles*: *Max Particles*, the most alive
in the world at once, every emitter's together (50 000 without an environment). The air that
carries them is the world's wind (its first *Wind*, [Wind](wind.md)) and a game's air on top.

### Simulated on the CPU, for the eye alone

Particles are moved on the CPU, by the renderer, as it extracts each shown world
(`UpdateParticles()`, `StepParticleEmitter()` in `Particles.h`, plain code that tests step as the
renderer does). Nothing in the world feels them: they are never in the registry, never in the
physics, never in the simulation's steps, so a game plays the same with or without them, and the
editor and the game draw the same particles from the same components.

- **The world's time.** An emitter moves on by the world's time since the last frame
  (`RenderWorld::Time`, at most 0.1 s a frame, so a hitch doesn't make particles leap): a game's is
  its simulation clock's at the drawn moment, which stands still while the game is paused and
  moves on by one step at a time with *Step*, as the decals' pulse does; the editor's own world's
  runs on, so particles play in the editor's scene views as they are edited (a preview without
  playing). A jump back in time starts every emitter again.
- **The same every time.** Each emitter has its own random numbers (PCG's output on a linear
  congruential state), started from its *Seed*; a particle's numbers are drawn in one order whether
  or not it is born. The same seed and the same steps give the same particles to the bit
  (`NyxParticleTests`); a new seed starts the emitter again, which is also how a test or an edit
  replays it.
- **Moving.** Each step: aged, those past their lifetime gone; gravity and buoyancy as an
  acceleration; the drag pulls the velocity towards the air's speed by `1 - e^(-drag · dt)` (exact
  for any step length); then moved. Particles born during a step are spread over it, each as if
  born when it was due, so a low frame rate doesn't make them come in clumps.
- **Budgets in data.** An emitter never has more than its *Max Particles*; the world never more
  than its environment's *Max Particles*, taken from by the emitters in the world's order; a
  particle that would pass either isn't born (counted, `ParticleEmitterState::Refused`).
- **Kept per world**, by entity, from frame to frame (`RenderWorld::Particles`); an emitter gone is
  forgotten.

### Carried by the air

Each particle is carried towards the air's speed where it is: the world's wind there
(`SampleWind()` on the world's `WindState`, gusts, eddies and the height profile included,
[Wind](wind.md)) and what a game's air adds to it. `IWindField` (`WindField.h`) is the seam a game's
air comes in by: its own winds, the draught of a fire, the air a moving body pushes, whatever field
it keeps, as what it adds to the world's wind. A game gives one to a world
(`IRenderer::SetWorldWindField()`): a game module sets it on its game world as it makes its systems
(`GameWorld::SetWindField()`), and Nyx's game hands it on; the renderer asks it once per emitter a
step for what it adds at every one of the emitter's particles, in the world, at the world's time
(and for the grass's texture of local air, [Wind](wind.md#a-games-local-air-for-vegetation));
without one, the world's wind alone. Scenes saved with the environment's old *Wind*, the same
everywhere, get it back as a steady `WindComponent` as they load. *Drag* says how much a particle
follows the air: sparks a little, smoke a lot. One call an emitter rather than one a particle, so a
game's field can look up its cells for many particles at once; never written to, so the field may be
anything that can answer.

### Drawn instanced

- **One stream a frame.** Each world's particles become `ParticleInstanceData` (48 bytes: where,
  how big, the colour times the emissive intensity and the alpha, the turn) once a frame; each view
  takes those of its emitters in view into this frame's particle stream, one host-visible vertex
  buffer for every view (`VulkanParticles::Upload()`), written while no frame reads it (one frame in
  flight).
- **Culled by bounds per view**: an emitter's box round its particles as drawn, against the view's
  frustum (`SceneViewCullingStats::ParticleEmitters`, `ParticleEmittersCulled`, `Particles`). An
  emitter out of view still moves: its particles are where they should be when it comes back.
- **Sorted only where needed**: alpha blended emitters are drawn first, the farthest first by their
  boxes' middles, each one's particles back to front from the camera (`SortParticlesBackToFront()`:
  one key a particle, its distance and its index in one integer); additive ones after, in any order,
  as adding doesn't care. Particles of two alpha emitters that overlap aren't sorted between them.
- **Billboards** are two triangles an instance, their corners from the vertex's index, turned about
  the view by each particle's angle, facing the camera's plane (not its position, so they don't
  swing as they pass the edge of the view); their alpha falls to 0 at their rim. **Meshes** are the
  mesh's triangles an instance, turned by each particle's quaternion; their back faces are culled
  (no depth buffer sorts a mesh's faces, so the meshes should be convex), shaded a little by how
  they face the camera.
- **Lit** particles take, at their middle, the sun's light, the sky's from above and the point
  lights' (all of it, whichever way it comes, as a puff of smoke scatters light): smoke is lit by
  the fire beside it. Unlit ones show their colour: glows and sparks.
- **Fog**: additive particles' light is dimmed by the fog's transmittance; alpha ones are fogged
  as a decal is (premultiplied).

### Their pass, and soft particles

The particles have a pass of their own (`VulkanParticles`, `Particle.slang`) after the view's
ambient occlusion and before its bloom: over the HDR scene, with **no depth buffer**, each pixel
tested against the scene's depth in the shader, which reads the depth the ambient occlusion
already reads (`ViewConstants.SceneDepth`, now always set), turned to a readable layout and back
around the pass as the occlusion does. So:

- behind what the scene drew, a particle's pixel is dropped: hidden as with a depth test;
- in front, its alpha is faded over the emitter's *Soft Distance* to what is behind it: **soft
  particles**, with no hard line where smoke meets the ground or a flame a log. It costs one depth
  read and a division a pixel.
- after the occlusion, a particle isn't darkened by the occlusion of what is behind it, and covers
  the occluded light as it should; before the bloom, emissive particles bloom.

Why not in the scene pass, tested against its depth buffer: the depth can't be read in the pass that
writes it (no soft particles), and the particles would have to write the ambient occlusion's
second target too. Four pipelines (billboard and mesh, additive and alpha) with the scene pipelines'
layout; the emitter's soft distance, lit and blend in the draw's push constants.

### In the editor

Each emitter has an icon, a small box of lines, which a click selects (also an emitter with no
particles, such as one whose burst is over); a selected emitter's particles' bounds are drawn in
the selection's colour. Particles play in the scene views as they are edited; a new *Seed* replays
the emitter.

### A fire, as an example

`Assets/Scenes/ParticlesTest.nyxscene` (written by `NyxParticleGpuTests --write-scene`): the lights
test scene with a fire: flames (additive billboards, rising by their buoyancy, growing then
shrinking, yellow to red and gone), embers (small cubes, additive, glowing above the bloom's
threshold, thrown up and spinning, falling back slowly), smoke (alpha, lit, rising and growing,
drifting in the world's steady wind), and a warm point light at the flames. A game would carry the
smoke by its own air field and flicker the light with the flames.

## Seeing it

- In the editor's scene views, each light has an icon, a star of lines in its colour; a click on
  it selects the light. A selected light's range is drawn round it as three circles in the
  selection's colour.
- `Assets/Scenes/LightsTest.nyxscene` (written by `NyxPointLightGpuTests --write-scene`): the
  atmosphere test scene at dusk, the sun just set, with six coloured lights.
- Each particle emitter has an icon too, a small box of lines; a selected emitter's particles'
  bounds are drawn round them.
- `Assets/Scenes/ParticlesTest.nyxscene` (written by `NyxParticleGpuTests --write-scene`): the
  lights test scene with a fire's flames, embers and smoke in a wind.
- *Debug > Culling Bounds* (F4 in a Debug build of the game) shows the lights' ranges and the
  emitters' particles' boxes beside the meshes' bounds, green where drawn, red where culled.

## Numbers

`NyxInstancingBenchmark lights`: the atmosphere test scene (about 24 000 small cubes scattered over
its ground, its environment: fog, the sky following the sun, bloom, ambient occlusion) from its
camera, 1280 × 720, vsync off, a hidden window, an RTX 3070 Ti and an i7-12700K, the median of 60
frames in Debug and 300 in Release. Lights of range 6 m at random in front of the camera, among the
cubes, all of them in view. *Frame* is `DrawFrame()` on the CPU, *scene CPU* the renderer's
extraction, culling and recording, *GPU* the scene views' passes, by timestamps.

| Build | Point lights | Frame | Scene CPU | Scene views GPU |
| --- | --- | --- | --- | --- |
| Debug | 0 | 5.27 ms | 1.72 ms | 1.85 ms |
| Debug | 1 | 5.02 ms | 1.71 ms | 1.79 ms |
| Debug | 16 | 4.90 ms | 1.73 ms | 1.87 ms |
| Debug | 64 | 6.68 ms | 2.02 ms | 2.09 ms |
| Release | 0 | 3.06 ms | 0.18 ms | 1.92 ms |
| Release | 1 | 3.02 ms | 0.23 ms | 1.90 ms |
| Release | 16 | 3.01 ms | 0.17 ms | 1.93 ms |
| Release | 64 | 3.37 ms | 0.18 ms | 2.24 ms |

What it says:

- One light and 16 cost nothing that can be measured: the GPU time moves by less than 0.1 ms, the
  spread from one run to the next.
- 64 lights in view cost 0.25 to 0.5 ms of GPU time (0.32 ms here, 0.49 ms in a second Release
  run with other work on the machine): every lit pixel tests all 64, and those near a light light
  it. That is about 5 to 8 µs a light a million pixels, which is where the estimate above, a
  millisecond for a hundred lights at 1920 × 1080, comes from.
- On the CPU, a light costs its extraction and a sphere test a view: nothing in Release. In Debug,
  64 lights add 0.3 ms, nearly all of it the editor's icons' lines, which a Debug build draws into
  every world's overlay lines (Release builds draw no debug lines). A star of seven lines a light
  rather than a ball of 96 took that from 1.6 ms.
- The scene's 24 000 cubes are lit by every light within reach, instanced or not, at no cost of
  their own beyond their pixels.

Particles: `NyxInstancingBenchmark particles`, the same scene, camera and machine, with 10 emitters
in front of the camera, 2 000 particles each (alive at once, after 300 frames of filling up): five of
sparks (additive billboards, 5 to 8 cm), five of smoke (alpha blended, lit, sorted back to front,
30 to 50 cm). *Particles GPU* is their pass alone, by timestamps of its own
(`SceneViewGpuTimes::Particles`).

| Build | Emitters | Particles | Frame | Scene CPU | Scene views GPU | Particles GPU |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | 0 | 0 | 5.13 ms | 1.66 ms | 1.82 ms | |
| Debug | 10 | 20 000 | 16.60 ms | 12.04 ms | 2.83 ms | 0.22 ms |
| Release | 0 | 0 | 2.77 ms | 0.15 ms | 1.80 ms | |
| Release | 10 | 20 000 | 4.00 ms | 1.34 ms | 1.88 ms | 0.16 ms |

What it says:

- In Release, 20 000 particles cost 1.2 ms of the CPU a frame, about 60 ns a particle: moved,
  turned into what the GPU draws, the smoke's 10 000 sorted, and copied into the frame's stream.
  The GPU draws them in 0.16 ms, soft against the scene's depth, fogged and lit; the scene views'
  time grows by less, as the pass overlaps the others.
- In Debug, 10.4 ms of the CPU, about 0.5 µs a particle: Debug builds call glm's operators rather
  than inline them. The hot loops are written in plain floats (moving a particle, its bounds) and
  the sort is one integer key a particle on a plain array, which took it from about 30 ms; a Debug
  build of a scene with a few fires (a few thousand particles) stays well within a frame.
- The emitters out of view still move; culling saves their drawing, not their stepping. Past about
  100 000 particles, the simulation belongs on the GPU (Later).

## Later

- Clusters of lights, when views see more than a few dozen (above).
- Shadows of a few point lights (above).
- Spot lights: a point light with a cone, the same list, two more numbers a light.
- The fog lit by the lights: a glow round a light in fog (the fog adds the sun's and the sky's
  light only).
- Particles: textures and flipbooks for the billboards (a soft disc for now), billboards stretched
  along their velocity for sparks, collisions with the ground, trails; their simulation on the GPU
  in a compute pass when hundreds of thousands are wanted (the CPU's is enough for the tens of
  thousands a scene of fires and dust has); alpha particles of different emitters sorted together
  where they overlap; an emitter far from every view moved less often; particles lit per pixel, and
  casting shadows.
