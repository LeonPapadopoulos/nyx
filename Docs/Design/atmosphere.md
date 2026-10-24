# Atmosphere

The world's air and what a camera makes of bright light: height fog, a sky that follows the sun,
and bloom, and why they work as they do. The settings are on `EnvironmentComponent` (`Scene`); the
math is in `Engine/Source/Runtime/Render` (`Atmosphere.h`, `Bloom.h`), the passes in `VulkanBloom`,
the shaders in `Engine/Shaders/Library/nyx/atmosphere.slang`, `Skybox.slang`, `Bloom.slang` and
`Tonemap.slang`.

## The environment component

One component holds the world's air and the camera's response to light: *Fog*, *Sky* and *Bloom*
(`EnvironmentComponent`, reflected, with undo, saved in scenes, sent to a running game). The world's first one counts; a world without one is
drawn as before, to the bit, and so is one whose fog and sky are both off (`NyxAtmosphereGpuTests`
checks both).

Why a component of its own, not more settings on the `DirectionalLightComponent`:

- Fog and the sky belong to the world's air, not to a light: a night scene has fog and no sun, and
  a world could have several lights but has one air.
- The light already has a dozen settings (its shadows); the air's would double them.
- The air acts on the light: it reddens and dims the sun as the sun goes down. A light setting
  that changes the light's own colour by its direction would be odd; the air doing it is how it
  is.

What the renderer makes of it, per view, each frame: `VulkanRenderer::ExtractSceneEnvironment()`,
after the view's light is found, calls `MakeSceneViewAtmosphere()` with the light's direction and
colour, which brings the settings into range and works out what the shaders need
(`SceneViewAtmosphere`, in the view's set: `ViewConstants.FogColor` to `SunGlow`). The editor's
free camera shows it as the game's camera does.

## Height fog

Exponential height fog: the fog's density falls off exponentially with height,
`density(y) = d · e^(-f (y - h))`: *Density* `d` at the fog's *Height* `h`, thinning upwards by
*Height Falloff* `f` (0: the same at every height), thicker below. Along a ray from the camera,
the fog in the way is an optical depth `τ`, and the light that gets through is `T = e^(-τ)`; what
is seen is `colour · T + inscattered · (1 - T)`.

- **Exactly, not by steps.** The density along a straight ray is an exponential of the distance,
  so its integral is closed: `τ = d · e^(-f (y0 - h)) · L · (1 - e^(-x)) / x`, `x = f · Δy`, from the
  ray's start height `y0`, over its length `L` and its rise `Δy`. Near `x = 0` (a level ray, or no
  falloff) the fraction is its series. The exponents are held below 80, and the two ends' terms are
  taken relative to the larger, so a ray deep under the fog's height is very deep rather than
  infinite or, by cancellation, nothing. `NyxAtmosphereTests` compares it with the fog summed in
  200 000 steps along level, rising, falling and almost level rays, above and below the fog's
  height, with and without a start distance: within 0.2 %.
- **Start distance**: the ray starts that far from the camera; nothing nearer is fogged.
- **Max opacity**: `T` is held above `1 - opacity`, so far hills and the horizon still show.
- **Inscattering**: the fog's own *Color*, tinted as the sky is (below), and the sun's light,
  through the air, by *Sun Scattering*, gathered round the sun by *Sun Lobe* (Henyey and
  Greenstein's phase function, `g` from 0, the same every way, to 0.95). So the haze glows
  towards the sun, in the sun's colour: warm towards a low sun.
- **Per pixel, in the surface shaders.** `surfaceFragment` fogs the lit colour, emission
  included, by the pixel's place (`nyx::heightFog()`). It costs a few exponentials a pixel, works
  the same for every mesh, instanced or not, two-sided or not, shadowed or not, and needs no depth
  buffer pass after the scene.
- **Decals** are blended over what is fogged already, in premultiplied alpha. The decal's light,
  emission included, is dimmed by `T`, and the fog's own light added only by the decal's opacity:
  `src · T + inscattered · (1 - T) · opacity`. Blended over `beneath · T + inscattered · (1 - T)`,
  that gives the fogged decal over the fogged surface; a decal that only glows (opacity 0) adds
  its dimmed light and nothing else, and a decal's empty parts leave the fogged surface as it was.
- **The sky** is fogged as a ray `SkyFogDistance` (10 km) long: thick on the horizon, where a ray
  stays low longest, clearer higher up.
- **Not fogged**: a material's debug views, and the editor's overlays (drawn over the picture).

`NyxAtmosphereGpuTests` checks it against the CPU's (`GetHeightFogTransmittance()`,
`GetHeightFogInscattering()`): a black wall under a black sky, with a sun that gives no light,
shows the fog alone, and its pixels are the CPU's numbers tonemapped, within 2 of 255, through
level fog, from a start distance, held at a maximum opacity, and over a ground the fog thins above;
the sky's horizon too, towards the sun and away from it.

## The sky follows the sun

With *Follow Sun* on, the sky's cube map, its light (the irradiance and the specular cube) and the
fog's colour are multiplied by a tint, and the sun's light by what the air leaves of it:

- **The light through the air** (`GetSunTransmittance()`): `e^(-β (m - 1))`, `m` the air mass along
  the sun's ray (Kasten and Young's formula: 1 overhead, about 38 on the horizon), `β` about
  Rayleigh's scattering with a little haze, `(0.05, 0.11, 0.25)` per air mass, times *Sun
  Reddening*. Overhead it is 1, so a light at noon is as it is set; at 30 degrees it is a little
  warm, at 5 degrees orange and a third as bright, at the horizon deep red and dim; below the
  horizon (by about three degrees) it is gone.
- **The sky's tint** (`MakeSceneViewAtmosphere()`): the light's hue (its colour at luminance 1,
  `GetChromaticity()`), mixed with white by *Sun Tint*, times the daylight: 1 by day, falling
  through dusk to *Night Brightness* (`GetDaylight()`). The hue has no brightness of its own, so the
  sky is as bright as the daylight says, whatever the light's intensity. The sky's light is tinted
  alike, so a shadow at dusk is lit warmer and dimmer, and at night hardly at all.
- **The glow round the sun**: the light's hue by *Sun Glow*, gathered by *Sun Glow Lobe*
  (Henyey-Greenstein again), added to the sky; gone once the sun has set.

The sky stays the cube map it is: the clouds keep their shapes, only tinted. A sky made from the
sun's place (a scattering model) would be the next step; the tint gives the warm, hazy low sun
without one.

## Bloom

Light much brighter than the screen can show spreads round what gives it off, as a lens and an eye
spread it: an emissive surface or decal glows. `VulkanBloom`, per scene view, between its scene
pass and its tonemap, from the HDR scene:

1. **The bright part, halved** (`bloomPrefilterFragment`): the scene exposed as the tonemap exposes
   it, so *Threshold* is in what the screen shows (1 is white); each colour's bloomed part is what
   is above the threshold, starting smoothly a *Knee* below it (`GetBloomFactor()`: a quadratic, as
   Unity's bloom has it), so nothing pops in as a light crosses the threshold. Halved into the
   chain's first level with 13 taps (Jimenez's downsample, five overlapping 2 × 2 groups), each
   group weighed down by its brightness (Karis's average): a few very bright pixels spread as
   wide as a dimmer patch, rather than flickering as they move or as the camera turns.
2. **Down** (`bloomDownsampleFragment`): each level halved into the next with the same 13 taps,
   for *Levels* (1 to 8) levels, but no smaller than 8 pixels on its shorter side
   (`GetBloomLevelCount()`): a smaller level, filtered up across the view, shows its texels as
   creases and squares in the widest glow. A 1280 × 720 view gets six (640 × 360 down to 20 × 11).
3. **Up** (`bloomUpsampleFragment`): from the last up, each level filtered up with a cubic B-spline
   (four bilinear taps; smooth and a little blurred) and mixed into the level above by *Radius*:
   `above · (1 - r) + below · r`, by blending, in place. The radius is how much each wider level
   weighs: 0 only the first level, a tight glow; near 1 mostly the widest.
4. **Added** by the tonemap (`ViewConstants.Bloom`), filtered up from half the view's size, times
   *Intensity*, before tonemapping, so a glow is tonemapped with what it lies over.

- **One image a view**, the levels its mips, each a render target and a texture in the bindless
  array; made at the first frame that blooms and when the view's size or levels change
  (`VulkanBloom::Prepare()`), let go of when bloom goes off, with the view, or when its world goes.
  B10G11R11 (4 bytes a texel, light above 1 kept) where the device can draw into it and blend it,
  else RGBA16F. 640 × 360 and its mips: about 1.2 MB.
- **Why not `VulkanBlur`**: the blur that UI glass reads is a chain of halvings too, but 8-bit, of
  the tonemapped picture, keeping only its smallest level, blurred by a Gaussian. Bloom needs light
  above 1, a threshold, and every level kept for the way up; bending the blur to that would give it
  a second format, three more modes and levels it doesn't need. The two share the idea and the
  fixed-function setup (one triangle a pass, a render pass a level), not the code.
- **Every view's set each frame.** A view whose world went away kept its last set, whose bloom slot
  pointed at the chain the view had just let go of: its tonemap read a freed image and the device
  was lost (found by `NyxAtmosphereGpuTests`). `UpdateSceneUniforms()` now writes every view's set
  each frame, with a world or without.
- **Not bloomed**: a material's debug views (their outputs aren't light); with the exposure low
  enough, nothing reaches the threshold and the picture is the one without bloom, to the bit.
- **Seeing it**: *Debug > Bloom* in the editor, for every scene view, and F6 in a Debug build of
  the game, show the bloom alone, tonemapped (`ESceneViewDebug::Bloom`). Its passes are timed on
  the GPU, view by view, for the first eight views (`VulkanRenderer::GetSceneViewGpuTimes()`;
  `NyxInstancingBenchmark` prints them).

## Seeing it

- *Environment* on an entity in the editor: every value edited live, in the editor's views and a
  running game. `Assets/Scenes/AtmosphereTest.nyxscene` (written by `NyxAtmosphereGpuTests
  --write-scene`) is the ground test scene with about 24 000 small cubes scattered over its ground,
  a hazy environment, a low sun, and for bloom a glowing cube (`Materials/Emissive.nyxmat`) and a
  glowing ring (`MaskedDecal`) on a crate.

## Numbers

`NyxInstancingBenchmark`, 1280 × 720, vsync off, a hidden window, an RTX 3070 Ti and an i7-12700K;
the median of 60 frames in Debug and 300 in Release. *Frame* is `DrawFrame()` on the CPU, *scene
CPU* the renderer's extraction, culling and recording, *GPU* the scene views' passes, by
timestamps. *Instanced* is the instancing note's field (50 000 cubes in 200 cells and 300 spheres)
on a ground, under a sun casting four cascades; *ground* the atmosphere test scene from its camera.

| Build | Scene, shot | Environment | Frame | Scene CPU | Scene views GPU |
| --- | --- | --- | --- | --- | --- |
| Debug | instanced, above | none | 9.13 ms | 5.84 ms | 1.92 ms |
| Debug | instanced, above | fog, sky | 8.69 ms | 5.57 ms | 1.98 ms |
| Debug | instanced, inside | none | 8.65 ms | 5.81 ms | 1.43 ms |
| Debug | instanced, inside | fog, sky | 8.38 ms | 5.64 ms | 1.48 ms |
| Debug | ground | none | 4.03 ms | 1.14 ms | 1.63 ms |
| Debug | ground | fog, sky | 3.81 ms | 1.18 ms | 1.57 ms |
| Release | instanced, above | none | 2.66 ms | 0.23 ms | 1.79 ms |
| Release | instanced, above | fog, sky | 2.71 ms | 0.24 ms | 1.80 ms |
| Release | instanced, inside | none | 2.23 ms | 0.24 ms | 1.21 ms |
| Release | instanced, inside | fog, sky | 1.97 ms | 0.23 ms | 1.20 ms |
| Release | ground | none | 2.51 ms | 0.10 ms | 1.68 ms |
| Release | ground | fog, sky | 2.30 ms | 0.10 ms | 1.54 ms |

What it says: the fog and the sky cost nothing that can be measured. The scene views' GPU time moves
by less than 0.1 ms either way, which is the spread from one run to the next (the ground scene a
second time in Release: 1.70 ms without, 1.63 ms with). A pixel pays a few exponentials and a
division for the fog, against the lighting's and the shadow lookup's 9 bilinear taps; the CPU finds
the component and works out a handful of values once a view. The ground scene's 24 336 scattered
cubes cost no more fogged than the field's 50 000.

Bloom, on top of the fog and the sky (the same scenes and shots); *bloom GPU* is the bloom's passes
alone, by timestamps of their own (`VulkanRenderer::GetSceneViewGpuTimes()`):

| Build | Scene, shot | Bloom | Frame | Scene CPU | Scene views GPU | Bloom GPU |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | instanced, above | off | 9.16 ms | 5.76 ms | 1.99 ms | |
| Debug | instanced, above | on | 9.10 ms | 5.69 ms | 2.06 ms | 0.06 ms |
| Debug | instanced, inside | off | 8.48 ms | 5.68 ms | 1.48 ms | |
| Debug | instanced, inside | on | 8.79 ms | 5.72 ms | 1.67 ms | 0.07 ms |
| Debug | ground | off | 3.80 ms | 1.22 ms | 1.51 ms | |
| Debug | ground | on | 4.05 ms | 1.35 ms | 1.60 ms | 0.05 ms |
| Release | instanced, above | off | 2.68 ms | 0.24 ms | 1.79 ms | |
| Release | instanced, above | on | 2.64 ms | 0.25 ms | 1.67 ms | 0.05 ms |
| Release | instanced, inside | off | 2.15 ms | 0.24 ms | 1.21 ms | |
| Release | instanced, inside | on | 2.24 ms | 0.26 ms | 1.27 ms | 0.05 ms |
| Release | ground | off | 2.21 ms | 0.09 ms | 1.53 ms | |
| Release | ground | on | 2.54 ms | 0.12 ms | 1.74 ms | 0.05 ms |

What it says: bloom's own passes take 0.05 ms of GPU time at 1280 × 720 (11 passes: the prefilter,
at half size, reads the full-size scene; then five halvings and five mixes back up, all small),
whatever the scene, as a pass over the screen does. The scene views' total moves by more than that,
up and down (from -0.12 to +0.21 ms), which is the spread from one run to the next plus the GPU
waiting between passes that depend on each other. The CPU records the passes in 0.01 to 0.03 ms.

## Later

- A sky made from the sun's place, by scattering (a sky-view lookup table), instead of a tinted
  cube map; and the environment's light made again from it as the sun moves.
- Volumetric fog: shadowed shafts of light, local fog volumes.
- The fog over a point light's or a decal's own light: the fog adds the sun's and the sky's only.
