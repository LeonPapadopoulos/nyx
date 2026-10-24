# Atmosphere

The world's air: height fog and a sky that follows the sun, and why they work as they do. The
settings are on `EnvironmentComponent` (`Scene`); the math is in `Engine/Source/Runtime/Render`
(`Atmosphere.h`), the shaders in `Engine/Shaders/Library/nyx/atmosphere.slang` and `Skybox.slang`.

## The environment component

One component holds the world's air: *Fog* and *Sky* (`EnvironmentComponent`, reflected, with undo,
saved in scenes, sent to a running game). The world's first one counts; a world without one is
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

## Seeing it

- *Environment* on an entity in the editor: every value edited live, in the editor's views and a
  running game. `Assets/Scenes/AtmosphereTest.nyxscene` (written by `NyxAtmosphereGpuTests
  --write-scene`) is the ground test scene with about 24 000 small cubes scattered over its ground,
  a hazy environment and a low sun.

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

## Later

- A sky made from the sun's place, by scattering (a sky-view lookup table), instead of a tinted
  cube map; and the environment's light made again from it as the sun moves.
- Volumetric fog: shadowed shafts of light, local fog volumes.
- The fog over a point light's or a decal's own light: the fog adds the sun's and the sky's only.
