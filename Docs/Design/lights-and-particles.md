# Lights and particles

Light from points, such as a lamp or a fire, and how it gets to the pixels; and why it works as it
does. The settings are on `PointLightComponent` (`Scene`); the math is in
`Engine/Source/Runtime/Render/PointLights.h`, the renderer's part in `VulkanRenderer`
(`ExtractPointLights()`, `CullSceneView()`, `UpdateSceneUniforms()`), the shaders in
`Engine/Shaders/Library/nyx/view.slang` and `lighting.slang`.

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

## Seeing it

- In the editor's scene views, each light has an icon, a star of lines in its colour; a click on
  it selects the light. A selected light's range is drawn round it as three circles in the
  selection's colour.
- `Assets/Scenes/LightsTest.nyxscene` (written by `NyxPointLightGpuTests --write-scene`): the
  atmosphere test scene at dusk, the sun just set, with six coloured lights.

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

## Later

- Clusters of lights, when views see more than a few dozen (above).
- Shadows of a few point lights (above).
- Spot lights: a point light with a cone, the same list, two more numbers a light.
- The fog lit by the lights: a glow round a light in fog (the fog adds the sun's and the sky's
  light only).
