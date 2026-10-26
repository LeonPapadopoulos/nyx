# Atmosphere

The world's air, and what a camera and the sky's light make of it: height fog, a sky that follows
the sun (made by the air's scattering, or the sky's cube map tinted), a high layer of cloud in it,
the time of day that turns the sun, bloom, ambient occlusion, light shafts, volumetric clouds and
colour grading, and why they work as they do. The settings are on `EnvironmentComponent`,
`CloudLayerComponent`, `VolumetricCloudsComponent`, `TimeOfDayComponent` and
`ColorGradingComponent` (`Scene`); the math is in `Engine/Source/Runtime/Render` (`Atmosphere.h`,
`SkyAtmosphere.h`, `CloudLayer.h`, `VolumetricClouds.h`, `Bloom.h`, `AmbientOcclusion.h`,
`LightShafts.h`, `ColorGrading.h`), the passes in `VulkanSkyAtmosphere`, `VulkanCloudLayer`,
`VulkanVolumetricClouds`, `VulkanBloom`, `VulkanAmbientOcclusion`, `VulkanLightShafts` and
`VulkanColorGrading`, the shaders in `Engine/Shaders/Library/nyx/atmosphere.slang`, `sky.slang`,
`clouds.slang`, `cloudnoise.slang`, `volumetricclouds.slang`, `surface.slang`,
`SkyAtmosphere.slang`, `CloudNoise.slang`, `CloudLayer.slang`, `VolumetricCloudNoise.slang`,
`VolumetricCloudWeather.slang`, `VolumetricClouds.slang`, `Skybox.slang`, `Bloom.slang`,
`AmbientOcclusion.slang`, `LightShafts.slang`, `ColorGrading.slang` and `Tonemap.slang`.

## The environment component

One component holds the world's air and the camera's response to light: *Fog*, *Sky*, *Bloom*,
*Ambient Occlusion* and *Light Shafts* (`EnvironmentComponent`, reflected, with undo, saved in scenes, sent to a
running game). The world's first one counts; a world without one is
drawn as before, to the bit, and so is one whose fog and sky are both off (`NyxAtmosphereGpuTests`
and `NyxSkyAtmosphereGpuTests` check both). Its *Particles* settings, the wind and the most particles in the world, are in
[Lights and particles](lights-and-particles.md#particles).

Why a component of its own, not more settings on the `DirectionalLightComponent`:

- Fog and the sky belong to the world's air, not to a light: a night scene has fog and no sun, and
  a world could have several lights but has one air.
- The light already has a dozen settings (its shadows); the air's would double them.
- The air acts on the light: it reddens and dims the sun as the sun goes down. A light setting
  that changes the light's own colour by its direction would be odd; the air doing it is how it
  is.

What the renderer makes of it, per view, each frame: `VulkanRenderer::ExtractSceneEnvironment()`,
after the view's light is found, calls `MakeSceneViewAtmosphere()` with the light's direction and
colour and the camera's place, which brings the settings into range and works out what the shaders
need (`SceneViewAtmosphere`, in the view's set: `ViewConstants.FogColor` to `SunGlow`), and
`MakeSceneViewSkyAtmosphere()`, what the sky's tables are made from in the Atmosphere mode
(`SceneViewSkyAtmosphere`, below). The editor's free camera shows it as the game's camera does.

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

The sky's *Mode* is one of three: *Atmosphere* (the default), the sky made from the sun's place by
the air's scattering (the next section), which follows the sun by what it is; *Tinted Cube Map*,
the sky's cube map tinted by the sun, as follows; or *Cube Map*, the cube map drawn as it is,
whatever the sun does, and nothing of the air's sky made. The tinted mode's settings say so in
their tooltips, and so do the air's (the Details panel shows a category's settings whatever the
mode; none of them can contradict it).

**Follow Sun, from older scenes.** The sky had a *Follow Sun* switch before it had three modes: off,
the cube map as it is, whatever the mode said, the air's sky included. Scenes keep it: the
component's post-load step (`FoldLegacySkyMode()`, registered in `RegisterComponentTypes()`) folds
it into the mode as a scene loads, so a scene looks as it did: *Follow Sun* off, in either mode, is
*Cube Map*; on, the mode as saved; a scene older still, from before the modes, has the default
mode, *Atmosphere*, as it has had since. *Follow Sun* is then on, hidden (`NYX_PROPERTY(Serialize,
Hidden)`), and read by nothing else. *Cube Map* is the mode's last value, as modes are saved by
their number. `NyxAtmosphereTests` saves environments with *Follow Sun* off and on in both modes
and checks how they load.

In the *Tinted Cube Map* mode, the sky's cube map, its light (the irradiance and the specular cube)
and the fog's colour are multiplied by a tint, and the sun's light by what the air leaves of it:

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

The sky stays the cube map it is: the clouds keep their shapes, only tinted. The tint gives the
warm, hazy low sun without a scattering model; the Atmosphere mode is that model.

## The sky made by the air

The *Atmosphere* mode makes the sky from the sun's place, by the light the air scatters towards the
camera, as Hillaire's sky does (Hillaire 2020, *A Scalable and Production Ready Sky and Atmosphere
Rendering Technique*): blue by day, orange and red at sunset, the zenith deep blue, haze round the
sun; for a camera kilometres up, the horizon below eye level and the sky above darker. No texture is
painted: the sky is what a planet with a shell of air under the sun gives.

Why this, and not more tint: a tinted cube map has one colour for the whole sky at a time; the air
gives each direction its own, from how far the light went through it: the horizon towards a low sun
gold while the zenith is still blue, the sky opposite it pink over a darker band. The step after,
clouds lit by the sun through the air, needs the same numbers, and a camera above the ground needs a
sky that knows its height. A path traced sky would be exact but slow; the precomputed scattering
of 2008 (four-dimensional tables, made again only when the air changes) is exact for single
scattering but heavy to make as the sun moves; Hillaire's makes small tables each time the sun or
the camera's height changes, in a fraction of a millisecond, and its multiple scattering is close to
a path tracer's for earth-like air.

### The air

A planet of *Planet Radius* (6360 km) with air up to *Atmosphere Height* (100 km) above its ground,
the world's height 0. The air is three things, each a colour times a scale, per kilometre of it at
the ground (`MakeSceneViewSkyAtmosphere()` brings them into range, `SceneViewSkyAtmosphere`):

- **Molecules** (Rayleigh): scatter blue most (*Rayleigh Scattering* and *Rayleigh Scale*, the
  earth's (5.8, 13.6, 33.1) × 10⁻³), falling off with height by *Rayleigh Height* (8 km), the
  phase `3 / 16π (1 + cos²)`: what makes the sky blue and the low sun red.
- **Haze** (Mie): scatters about white, mostly forwards (*Mie Scattering*, *Mie Scale*, 4 × 10⁻³;
  *Mie Absorption*, 0.44 × 10⁻³, taken away without scattering), falling off by *Mie Height*
  (1.2 km), the phase Henyey and Greenstein's of *Mie Anisotropy* (0.8): the glow round the sun.
- **Ozone**: absorbs orange most (*Ozone Absorption*, *Ozone Scale*, 1.9 × 10⁻³ at its thickest), a
  tent from 10 to 40 km up, thickest at 25 km: what keeps the sky blue, not grey, at dusk.

*Ground Albedo* is the planet's ground below the horizon, as the sky sees it: the light it sends
back up lights the air from below, and it is the ground the sky shows below the horizon where the
world has no mesh. *Warmth* (-1 to 1) tints the sky's light warmer or cooler at the same luminance
(`GetSkyWarmthTint()`: red × 2^(w/2), blue × 2^(-w/2), over its luminance): a stylising offset on
the air's colours, for the sky, its light and the fog alike; the sun's own light is the air's.

### The tables

`VulkanSkyAtmosphere`, per view, by the compute shaders of `SkyAtmosphere.slang`, into the frame's
command buffer before the view's shadows and scene pass, only when what they depend on changed
(`GetSkyAtmosphereChanges()`):

1. **Transmittance** (256 × 64, when the air changes): how much light gets through from a height,
   at an angle, to the top of the air: 40 steps along each ray. Laid out as the 2008 precomputed
   scattering's: u by the distance to the top between its least and most at that height, v by the
   distance to the horizon, so the near-horizontal rays, which change fastest, get most of it.
2. **Multiple scattering** (32 × 32, when the air changes): by height and the sun's angle, the
   light scattered two and more times, as a factor of the sun's: a group a texel, its 64 threads 64
   directions over the sphere, 20 steps each, summed in shared memory; the second order `L₂` and
   the part `f` of light that scatters again give all orders as `L₂ / (1 - f)` (Hillaire's
   equations 5 to 10).
3. **Sky view** (192 × 108, when the air, the sun or the camera's height changes): the sky round
   the camera at its height, 4 to 30 steps along each ray (more along the long, low ones), once
   scattered light from the sun through the transmittance table, the multiple scattering on top,
   the planet's ground lit by the sun where a ray ends on it. u is the angle round the up axis from
   the sun (squared, so more of it is near the sun; the sky is the same either side), v from the
   zenith through the horizon to the nadir, most of it near the horizon wherever the horizon is at
   that height. Its light is the sun's illuminance (π times the light's colour, what a white
   surface facing the sun shows times π) times what the air gives per unit of it, times the
   warmth, plus the night's light (below).
4. **The sky as a cube** (64 a side, 7 mips), from the sky view, without the sun's disk, which
   lights the world by itself; its **irradiance** (8 a side, the diffuse light over π, summed from
   its 4 × 4 mip) and **specular** cube (64 a side, 6 mips, GGX prefiltered with 32 samples, as
   `Environment.slang` does the cube map's): the world's light (below).

Each image is written whole, from Undefined to General layout, and ends in ShaderReadOnlyOptimal
for the passes after; each dispatch is followed by a barrier for the next. The tables are about
0.8 MB a view (0.4 MB more while the debug window shows them), made at the first frame the view
draws the air's sky and let go of when it no longer does (no environment, a cube map mode, a studio or black sky in the view).

**What is made again, when:** the air's settings change, everything; the sun turns by more than
0.02°, its light changes, or the warmth or the night's light changes, the sky view and the cube;
the camera rises or falls by more than a metre and 1 % of its height, the sky view and the cube
(`GetSkyViewHeightTolerance()`: the horizon then never more than a few hundredths of a degree off).
A camera moving sideways changes nothing: the camera is always above the planet's middle. Nothing
changed, nothing is recorded and the sky's passes take no time.

### The sky, the sun and the night

The sky pass (`Skybox.slang`, the same cube at the far plane) reads, per pixel, the sky view by the
pixel's direction (`nyx::skyAtmosphereRadiance()`) at the height the table was made for, and adds
the **sun's disk** (`nyx::sunDiskRadiance()`): *Sun Disk Size* across (1° by default, the real
sun's 0.53°, larger for the stylised look), its light *Sun Disk Brightness* (40) times the light's
colour, through the air to the camera (the transmittance table), darker and redder towards its edge
(limb darkening, `μ^(0.397, 0.503, 0.652)` per channel, as Hillaire's Frostbite sky has it), its
edge smoothed over a pixel. Forty times what a white surface facing the sun shows blooms well past
any threshold, but stays far below the half floats' 65 504 (the real sun, about 46 000 times, would
not with a bright light). Then the height fog, as on the cube map's sky.

The **sun's light** on the world is the light's colour times the air's transmittance from the
camera's height towards the sun (`GetSunLightThroughAtmosphere()`, integrated on the CPU along the
same 40 steps as the table, within 5 × 10⁻⁴ of it), times the part of its disk above the horizon,
so it sets over the disk's own width rather than going out at once. *Sun Reddening* and the tint's
settings are the tinted mode's; this light is the air's. With a time of day, its *Horizon Fade*
fades that light too, below the level horizon (the time of day's section); the sky itself is lit by
the light before that fade (`ExtractedSceneGlobals::SunLightColor`), since the air high up still
scatters the sun's light after it has set below: the dusk and the night's light stay
(`NyxSkyAtmosphereGpuTests` checks the dusk sky 4° after sunset the same with a time of day as
without).

**At night** the air gives nothing: with the sun well below the horizon its sky is black, as a
moonless night is. *Night Sky Brightness* is an artistic light it keeps instead: a radiance, in
*Night Sky Color* (a dark blue by default), added to every direction of the sky view as the sun
goes down (the dusk of `GetDaylight()`: none from 0.2 above the horizon, all from 0.1 below), so
it is in the sky, in its light on the world and in the fog's colour alike, and no part of the day
sky. It doesn't depend on the sun's light, so a time of day's fade doesn't take it. **0 is none**:
the air's own sky, black at midnight (`NyxSkyAtmosphereGpuTests` reads every texel of the sky view
0, a wall lit by the sky alone black). The default is 0.02, a dark blue night (the sky 1 11 32 of
255 at midnight, a white wall lit by it as much), so that a scene isn't black at night unless asked:
the tinted mode keeps 5 % of its cube map's light at night too. The tinted mode's *Night
Brightness* is its own. (Until now the air's night took 0.01 of the sun's illuminance a steradian
times the tinted mode's *Night Brightness*: a mean of 0.0016 above the horizon at the defaults, at
most 13 of 255 on the screen, which read as black.) Moon and stars are later.

### Its light lights the world

The world's ambient light, in this mode, is the sky's, at the camera's height: `nyx.environment`'s
diffuse light samples the irradiance cube by the normal, and its specular light the specular cube
by the reflected direction at the roughness's mip, in place of the cube map's spherical harmonics
and prefiltered cube, untinted; the split-sum table is the same. So a surface the sun doesn't reach
takes the sky's light: bluish by day, dimmer and warmer towards the sun at golden hour, and from
below the planet's ground's (`NyxSkyAtmosphereGpuTests` checks a wall in the sun's shadow against
the cubes it read back, within 1 of 255). Ambient occlusion dims it as it dims the cube map's.
With a high cloud layer that lights the world, the cube has its clouds in it ([It lights the
world](#it-lights-the-world)).

The sky is much darker than a white surface in the sun, as it is outside: with the sun 70° up, the
sky above the horizon is 0.12 on average, a sunlit white about 0.8, and the zenith deep blue. The
picture is a photograph's at a fixed exposure; the colour grading and the view's exposure make it
brighter or more painted.

### The look: the sky's light, the exposure, the haze

The air's light is the physical one, and next to the painted cube map its shade looked flat and
dim: in the crate yard at noon a grey crate's face in its own shade showed 68 72 78 of 255 under the
air's sky against 130 123 130 under the cube map, the sunlit ground about the same in both. Three
settings on the environment are the artistic handles, each 1 or 0 for the air as it is (with all
four at those values the picture is today's to the pixel: `NyxSkyAtmosphereGpuTests` draws the crate
yard at sunset, with fog, bloom and ambient occlusion, against `References/SkyAtmosphereNeutral.png`,
drawn before they were there):

- **Sky Light Intensity** (*Sky*, the Atmosphere mode's): what the sky's light on surfaces is
  multiplied by, the irradiance and the specular cube alike (`View.SkyTint.a` in
  `nyx.environment`), not how the sky looks; the night's light and a cloud layer's are in it too.
  **2.5 by default**: the crate's shade is then 115 119 128, near the cube map's, still cooler and
  bluer than it, as shade under a clear sky is, and the sunset stays a sunset (its shade 48 35 37
  against 42 24 12). The cube map modes' light is the painted cube map's, already as bright as it is
  meant to be, so it leaves them as they are (the reference pictures of the tinted mode unchanged).
- **Exposure Compensation** (*Exposure*, every mode): stops added to the view's own exposure
  before tonemapping (`ExposureScale`), so the bloom's threshold and the light shafts see the same
  light; 0 by default, since the air's sunlit ground is already as bright as the cube map's.
- **Haze Brightness** and **Haze Tint** (*Fog*, the Atmosphere mode's): what the fog's colour, the
  sky's the way each ray looks, is multiplied by (in the fog's colour constant, which the air's sky
  doesn't use otherwise), as *Color* is the fog's own in the cube map modes: a brighter or a warmer
  haze over the distance. 1 and white by default. The aerial perspective of the plan's next step
  would take them too.

`NyxSkyAtmosphereGpuTests` checks each alone: the sky's light doubles the shade, not the sky (the
sky's pixels the same); one stop brightens both; a warm, brighter haze tints the fogged distance
more than the near ground; in the tinted mode the sky's light and the haze change nothing.

### The fog takes its colour

In this mode the fog's own colour (`nyx::heightFogAlong()`) is the sky's the way the ray looks
(`nyx::skyAtmosphereFogColor()`: the sky view in the ray's direction; for a ray below the horizon,
its last row above it, by the angle round from the sun), instead of *Color*: brighter and warmer
towards the sun, pinker and dimmer away from it, and always what the sky behind it shows, so
distant fogged hills melt into the horizon, and fog over the sky changes nothing there. The fog's
own scattering of the sun (*Sun Scattering*, *Sun Lobe*) is added as before.
`NyxSkyAtmosphereGpuTests` checks a wall hidden by fog against the sky view's horizon, within 1 of
255.

The colour first came from the horizon alone, by the angle round from the sun, for every ray: the
light along a level ray. A ray up the sky above a low sun then took the horizon's glow next to the
sun, and the fog's thin part over the sky drew it as a band of the sun's colour from the sun up to
the zenith, narrowing there (a constant angle round, the sky view itself was right: 60° up on the
sun's side it gave 2 14 36 of 255 without fog, and 66 23 8 with it, against 2 13 35 at the side).
Taken from the ray's own direction, the glow is a halo round the sun, as the air's phases make it;
`NyxSkyAtmosphereGpuTests` checks, with the sun 2° up, that 60° up on its side is no brighter than
60° up at its side by more than the air's own phases give (1.5 times: the molecules' phase is 1.28
times stronger 58° from the sun than 88°, the haze is thin that high), with fog and without, that
fog changes the sky there by at most 2 of 255, and that the halo 5° above the sun is more than 3
times brighter.

### The camera high up

The tables take the camera's **height above the ground**, planet-centred, as Hillaire's do: from 2 m
the horizon is at eye level; from 2 km it is 1.44° below it (`GetHorizonCosine()`), the haze above
it brighter than the ground below, and the sky straight up darker, through thinner air
(`NyxSkyAtmosphereGpuTests` finds the horizon's edge 19 rows below eye level in a 20° view, as
computed, and the zenith darker). The sun sets later up there: from 2 km a sun a degree below the
level still shines. The ambient cube is made for the camera's height too, so a treetop sees the
sky's light as it is up there.

**Units and precision.** The world is in metres; the air's math is in kilometres, as Hillaire's,
which keeps a planet's radius and a camera a metre up apart: a single float holds 6360 km to about
half a metre, so a height is never taken as the difference of two radii. Everything that needs the
height near the ground works from it as it is: the distance to the horizon `√(h (2R + h))`, the
distance to the ground and to the top of the air (`r² - R²` as `h (2R + h)`), the height along a ray
(`(h (2R + h) + t (t + 2 r μ)) / (|p| + R)`). The camera's height is clamped to at least a metre and
just under the top of the air: a camera above the air (a view from orbit) would need the ray moved
to the top first, which isn't done.

### What the next steps take from it

- **A 2D cloud layer**: built so ([The high cloud layer](#the-high-cloud-layer)): lit by the sun
  through the transmittance table at the shell's height, its ambient from the irradiance cube, seen
  through the air between it and the camera by the table's ratio along the ray, its shell met by
  the same `h (2R + h)` forms.
- **Volumetric clouds** the camera can be inside or above need, per sample: the transmittance
  table towards the sun from the sample's height, the multiple scattering table for their ambient
  part, and the aerial perspective (step 3's froxel volume, or for now the transmittance and
  sky view) between the sample and the camera. Above the cloud layer, the sky view below the
  horizon is the planet's ground under air, which the cloud sea then covers.
- **Aerial perspective** (step 3) is the same integration as the sky view, into a froxel volume
  along the view, with the same tables.
- Tables per world rather than per view: views of one world make the same transmittance and
  multiple scattering tables; sharing them saves their cost when the air changes, which is rare.

### Seeing it

- *Debug > Sky Tables* in the editor (a dev tool, in Debug builds): a scene view's tables as
  pictures, as the GPU made them for its camera and sun: the transmittance as it is, the multiple
  scattering (× 50), the sky view, and the sky's cube unfolded as a cross, exposed by a slider
  (`IEditorRenderer::GetSceneViewSkyTablePictures()`, written by `skyPreview` in the frames they
  are asked for).
- The sky's passes are timed on the GPU per view, in the frames they run (`SceneViewGpuTimes::Sky`,
  *sky* in the frame statistics, F8 in a game, `gpu_sky_ms` in a benchmark's CSV).

### Its cost

On the RTX 3070 Ti, `NyxSkyAtmosphereGpuTests` at 1280 × 720, the median of 60 frames each:

| What | GPU |
| --- | --- |
| The sky view, the cube, its irradiance and specular light, made again (the sun turning) | 0.126 ms |
| of it: the sky view | 0.016 ms |
| of it: the irradiance | 0.031 ms |
| of it: the specular cube | 0.068 ms |
| The scene pass, a ground plane and the sky, with the air's sky | 0.044 ms |
| The same with the tinted cube map | 0.046 ms |

Within the plan's 0.3 ms for the sky and its tables, made in the frames something changed and
nothing in the others; the sky per pixel costs no more than the cube map's (two bilinear taps of
small tables and a few square roots, against a cube map's tap). The first version took 0.23 ms to
make again: the irradiance summed a 8 × 8 mip into 16 × 16 faces (0.07 ms), and the specular cube
took 64 samples (0.11 ms); both are smooth for a sky without its sun, so a 4 × 4 mip into 8 × 8
faces and 32 samples look the same at half the time. A game turning its sun every frame (a time of
day) pays it every frame the sun has turned more than 0.02°.

In the game (`NyxGame --benchmark 10`, the atmosphere test scene, the frame statistics'
`gpu_sky_ms`): with a benchmark camera rising from 2 m to 2 km in the 10 s, 286 of the 5781 frames
made the sky view again, 0.115 ms each (the median), 0.006 ms a frame on average; with the scene's
own camera, still, no frame did (the tables were made while the scene settled). The scene views'
GPU time is the same in a Debug and a Release build: the shaders are compiled alike.

## The time of day turns the sun

The fog, the sky and the shadows follow the primary light's direction, so the time of day only has
to turn the light: a *Time Of Day* (`TimeOfDayComponent`, `Scene`) sets its sun's rotation from
the time, and nothing in the renderer, the fog or the sky knows the time. The math is in
`TimeOfDay.h` (`Scene`), the running time in `TimeOfDayClock` (`Sim`).

- **The path** (`GetSunDirection()`): the sun turns once a day about the path's axis, at 15 degrees
  an hour, its hour angle 0 at noon. In the path's frame (east where it rises, *Sunrise Direction*
  round the up axis from +X towards -Z; the axis's side 90 degrees to the left of that), the
  direction towards the sun at hour angle `H` is `noon · cos H - east · sin H`, with
  `noon = up · cos φ - side · sin φ` and `φ` the *Path Tilt*: it is the sun's path at a latitude
  `φ` on the days the sun rises due east (no seasons). So the sun rises at 6, is highest at noon,
  `90 - φ` degrees up, sets at 18 and is lowest at midnight, as far below; 12 hours of day, 12 of
  night, whatever the tilt.
- **The rotation** (`GetSunRotation()`): the light's +Z towards the sun, its +X the path's axis,
  which is at right angles to every direction of the path. So the light turns about one fixed
  axis all day: smoothly through noon and midnight (a look-at with a fixed up would flip at the
  top), and it never rolls. Its position is left as it is.
- **The sun**: the entity *Sun* names, if it has a `DirectionalLightComponent`, or the world's first
  primary one when it names none (`FindTimeOfDaySun()`, through the lights only, not every entity).
- **Each frame** the editor and a game turn the sun to the time as it is (`ApplyTimeOfDay()`, next
  to `UpdateScatters()`): a time dragged in the Details panel, undone, or sent to a game as a live
  edit, turns the sun at once, also while the game is paused. Nothing else is sent: the editor's
  frame turns its own sun, the game's its own, from the same time. The sun's rotation is derived,
  like a ground's mesh; turned by hand, it turns back.
- **The time runs** in a game, in its steps (`GameWorld::Step()`, before the systems, so they see
  the step's sun). The time after a step is `from + (t - t0) · 24 / day length`, wrapped into
  0 to 24: `from` the time it ran from, `t0` the world's time then, `t` the world's time at the
  step's end, in doubles. A function of the steps, not of the frames, so a recording played again
  or another machine with the same steps gets the same sun to the bit, and there is no drift from
  adding a small step to a float 60 times a second (about a hundredth of an hour in 20 000 steps).
  A time that isn't the one the last step wrote was set from outside (an edit, a live edit, a
  load); it, a new day length, or time starting to run again, starts it running from there. The
  editor's world isn't stepped, so the time doesn't run there: it would change the scene as it is
  being edited.
- **Below the horizon** the sun's light fades out over *Horizon Fade* degrees (3 by default, a
  smoothstep from the horizon down; `GetSunHorizonFade()`): the renderer multiplies the time of
  day's sun's light by it as it reads the light (`GetTimeOfDayLightFade()`), so a sun under the
  ground lights nothing from below, with an environment or without. In the tinted mode the air
  fades it too, by about the same angle, after reddening it; in the Atmosphere mode the air takes
  it once the sun's disk is below the horizon. A light that gives no light casts no
  shadows (`UpdateViewportSceneGlobals()`), so the cascades aren't drawn at night.
- **A moon later**: a second directional light on the same path half a day off
  (`GetSunRotation(path, hours + 12)`), its own colour and intensity, faded the same way, and a
  time of day naming it as well as the sun; the renderer lights the scene by one directional light
  today, so the moon waits for a second one, or takes the primary light's place while the sun is
  down.
- **At any height**: the sun's direction is the same everywhere, and nothing here knows where the
  camera or the ground is. Elsewhere: the horizon the sun fades at, here and in the air, is the
  level one, not the horizon seen from up high, which dips below it (about 0.7 degrees at 500 m,
  1.4 at 2 km), so from a treetop at cloud height the sun's light goes a little before the sun
  reaches the visible horizon; a sky made from the planet's size would fix both. The fog is exact
  at every height, above it too.

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

## Ambient occlusion

The sky's light reaches less far into creases, corners and the feet of things than onto open
ground; the sun's light is shadowed already. Screen-space ambient occlusion finds, from the scene's
depth, how much of the sky above each pixel what is near it hides, and dims the sky's light alone
by it (`VulkanAmbientOcclusion`, per scene view):

1. **The sky's light apart.** A forward renderer lights a pixel in one go; to dim only the sky's
   part, the scene pass writes it to a second target (`VulkanViewportTarget`'s ambient light,
   RGBA16F), the rest to the first: `lightSurfaceApart()` gives the sun's and the sky's light
   apart, `surfaceFragment` and `decalFragment` write `SceneTargets` (a decal blends into both, by
   its opacity). Through fog, the second target gets the sky's light dimmed by the fog's
   transmittance, the first the fog's own light: the occlusion dims what of the sky's light reaches
   the camera, not the fog. Without occlusion (no environment, or it off), all of the light goes to
   the first target, summed as before, and the second is black: every picture of a scene without
   it is the same to the bit. A depth prepass, the other way, would draw every mesh twice (every
   blade of a field of grass) and need each material's vertex stage; the second target costs a
   write a pixel.
2. **The depth, halved** (`occlusionDepthFragment`): each half-size pixel takes the depth of the
   top left of its four, as metres along the view (R32F). One of the four, not their mean: a mean
   across an edge is a depth nothing is at. The scene's depth is read in place (its layout turned
   to ShaderReadOnlyOptimal for the passes and back for the picture pass's overlays).
3. **The occlusion at half size** (`occlusionFragment`): ground truth ambient occlusion (Jimenez
   et al. 2016, as Intel's XeGTAO does it). The normal comes from the depth (across and up, the
   neighbour each way whose depth is nearer, so a step at an edge isn't taken for a slope); for a
   few slices round the view direction, the horizons on both sides are found by stepping out across
   the screen up to *Radius* metres (what lies beyond counts less and less over its outer 60 %),
   and the cosine-weighted visible arc between them is integrated exactly (`GetSliceVisibility()`
   on the CPU, for the tests), times the normal's length in the slice; raised to *Intensity*.
   *Quality* is the slices and steps: Low 2 × 4, Medium 3 × 6, High 4 × 8. Each pixel's slices and
   steps are turned by its place in a 4 × 4 tile, mixed so neighbours are far apart, and a
   point's depth is placed at its own full-size pixel (a quarter texel off the half-size middle,
   which made an open slope lean into itself).
4. **The blur** (`occlusionBlurFragment`): along x, then y, five taps four texels wide (the ends
   half), so the 4 × 4 tile's noise evens out; each tap weighed down by how far its depth is from
   the pixel's, so it doesn't run across edges.
5. **Added** (`occlusionCompositeFragment`): the sky's light times the occlusion, filtered up by
   depth (the four half-size pixels round each pixel, weighed by nearness and by how near their
   depth is to its own: a bilateral upsample), added to the scene before bloom and the tonemap.

- **No temporal accumulation**: Nyx has no temporal antialiasing to hide a noisier, cheaper
  estimate under, so the noise is evened out in space; a still picture stays still.
- **What it can't see**: only what is on the screen occludes; a plane seen nearly edge on finds a
  little of itself above its horizon (the test box's top, seen from half a metre above it, 2.5 %
  darker; the far floor under 0.5 %).
- **Not occluded**: the sky, a material's debug views, the sun's light (`NyxAmbientOcclusionGpuTests`
  draws a scene lit by the sun alone the same to the bit with it and without).
- **Seeing it**: *Debug > Ambient Occlusion* in the editor and F6 twice in a Debug build of the
  game show the occlusion, white where all of the sky's light reaches
  (`ESceneViewDebug::AmbientOcclusion`). Its passes are timed on the GPU per view
  (`SceneViewGpuTimes::AmbientOcclusion`).

## Light shafts

The sun's light streaming through the gaps in what stands before it: between columns and branches,
past the edge of a wall, in shafts out from the sun across the picture. `VulkanLightShafts`, per
scene view, after its particles, from its HDR scene and its depth, at half its size; the math in
`LightShafts.h`, the shader `LightShafts.slang`. On with an *Environment*, under *Light Shafts*.

It is the screen-space radial blur of crepuscular rays (Mitchell, *GPU Gems 3*, 2007): the cheap,
stylised stand-in the plan allows before its volumetric fog (step 9), which gives real shafts in
the air.

1. **The light that may stream, halved** (`lightShaftsPrefilterFragment`): for each half-size
   pixel, its four pixels' brightness (luminance), exposed as the tonemap exposes it and held below
   4 (the sun's disk, forty times a white surface in the sun, would drown the sky round it), each
   as much as the depth lets through: the sky, at the far plane, all of it; what stands nearer
   than *Depth Threshold* none; all of it from twice as far, smoothly between. Times a glow round
   the sun: the cosine between the pixel's way and the sun's, raised to the 8th (half at about 24
   degrees from the sun, a tenth at 45), so the blue sky far from the sun doesn't stream. The
   cosine is taken in view space, from the projection, so it is the world's whatever the lens.
2. **Gathered along the line to the sun** (`lightShaftsBlurFragment`): 48 bilinear samples
   (`LightShaftsSamples`) from the pixel towards the sun's place on the picture, over *Density* of
   the way, each weighing the decay times the one before; their mean by those weights. What lies
   off the picture gives nothing. So a gap's light streams out from it, away from the sun, over
   what stands nearer, and fades as it goes.
3. **Evened out along the line** (`lightShaftsSmoothFragment`): each line starts up to a step
   further on, by interleaved gradient noise (Jimenez 2014); without it, the steps show as copies
   of every gap's edge further out. Four taps over a step along the line, each its own turn of the
   noise, even the noise out again. Nothing temporal: a still picture stays still.
4. **Added by the tonemap**, filtered up from half size, times the shafts' colour: the sun's hue
   as it reaches the camera (its light through the air at luminance 1), times *Tint*, *Intensity*
   and the fade below. One channel, the shafts' brightness, in R16F: two targets of 640 × 360 at
   1280 × 720, about 0.9 MB.

**With the bloom, not before it.** The tonemap adds the shafts as it adds the bloom, rather than a
pass adding them into the HDR scene before the bloom reads it:

- They are as soft as the bloom's glow already; bloomed, the brightest of them, round the sun,
  would only spread again and double the glow the sun's disk blooms with.
- No full-size pass to add them: one bilinear tap a pixel in a pass that runs anyway.
- They are still tonemapped and graded with the picture, as the bloom is: a warm grade warms them.
- The bloom reads the scene as it was, so it is the same with the shafts or without.

**The settings**, held in range by `MakeSceneViewLightShaftsSettings()`:

- *Intensity* (0.5, 0 to 16): how much of the light they gather they add.
- *Length* (1, 0.01 to 10): how far the light carries from the gap it comes through, as a part of
  the way to the sun: the decay is made so that the light from that part of the way falls to a
  tenth, `decay^(48 · length / density) = 0.1` (`GetLightShaftsDecay()`), so a length means the
  same whatever the density or the samples.
- *Density* (0.9, 0.05 to 1): how far towards the sun the lines reach. Less keeps the shafts away
  from the sun and makes them finer, as the samples are closer together.
- *Depth Threshold* (1000 m): what stands nearer blocks the light, from twice as far it lets it
  through as the sky does. At its default only the sky streams (the scene's far plane is a
  kilometre away by default); lower, a far, fogged hillside glows through too.
- *Tint* (white): times the sun's colour.

**Off by default.** They are a look a scene asks for, not part of every world's air; every scene
saved before, and every reference picture, stays as it was; and the volumetric fog of the plan's
step 9 takes their place where it is built. The *Light Shafts* setting on the environment turns
them on; `Assets/Scenes/LightShaftsTest.nyxscene` has them on.

**When the sun gives none** (`MakeSceneViewLightShaftsSettings()`, under the view's light as
`ExtractSceneEnvironment()` has it):

- **Its place on the picture** is the point at infinity towards it through the view's projection:
  the camera's place doesn't count, only where it looks. Behind the camera, or level with it,
  none.
- **Leaving the picture**: all of them while the sun is on it; beyond its edge they fade, to none
  half a picture past it (the farther of its two coordinates, -1 to 1 on the picture, from 1 to 2,
  a smoothstep: `GetLightShaftsScreenFade()`). A sun just off the picture still streams in.
- **Setting**: by the part of the light's own colour the air and the time of day leave it (the
  light as it reaches the camera, against the light as it gives it: `GetLightShaftsSunFade()`),
  all of them down to a tenth, none at nothing: in the earth's air they start to go about a degree
  above the horizon and are gone before the sun's light is. That is the light the renderer
  extracts, through the air from the camera's height, so a camera at cloud height, which still
  sees the sun after it has set for the ground, keeps its shafts that much longer. With *Follow
  Sun* off and no time of day, nothing fades the sun below the horizon, and so nothing fades its
  shafts.
- **No sun**: a world without a primary directional light has none.

Then nothing is drawn, the tonemap reads nothing, and the picture is the one without them, to the
bit; the targets are kept for when the sun comes back, and let go of when the shafts go off.
**Not drawn** either in a material's debug view (its outputs aren't light), or with a studio's or a
black sky (their light isn't the sun's).

### Its limits, against volumetric fog

- **Only what is on the picture.** A gap off the picture streams nothing, a sun far off it gives
  no shafts at all, and turning away fades them; a shaft between the trees beside the camera is
  still there in the air.
- **On the picture, not in the air.** The light is smeared over everything along the line, near or
  far: it can't fall onto the ground as a lit stripe through haze, or be seen from the side, or
  stand round a camera inside it, and a sun behind the camera (shafts going away towards the
  point opposite it) gives none. A froxel volume of the air, lit by the sun through its shadow map
  (step 9), gives each of these, at its place in space.
- **Blocked by the depth on the picture**, not by the shadow map: a branch blocks as much as the
  pixels it covers; what is behind the camera or off the picture blocks nothing.
- **Its brightness is the sky's round the sun**, not light scattered by the air: under a clear sky
  the sky streams by its brightness; where the cloud layer covers it, only as much as the clouds
  let through (below); there is no fog density to set, only an intensity. Fog makes no shafts
  brighter or longer.
- What it does well: a few hundredths of a millisecond (below), any height, any sky, no volume.

### The camera at cloud height, and the clouds

Nothing in it knows where the ground is: the sun's place is a direction, the occlusion comes from
the depth and the light on the picture, and the fade is the light the air leaves at the camera's
height. `NyxLightShaftsGpuTests` draws the same wall and sun 1.5 km up, with the same shafts.

The cloud layer gives the shafts **the clouds' transmittance along each view ray** (1 clear sky,
0 thick cloud): its pass's target, at the view's size, holds it in its alpha, and the prefilter
takes it for the sky's pixels in place of the depth's 1, one lookup ([The high cloud
layer](#the-high-cloud-layer)). So a thick layer over the sun holds its shafts back as a whole,
while the gaps between cloudlets still stream. Not the depth: the layer is kilometres away, beyond
any useful threshold, and its edges are soft, which a depth can't say. `NyxCloudLayerGpuTests`
finds the shaft through a window 25 of 255 brighter under a clear sky, 1 to 7 under an overcast
layer. The volumetric clouds give it the same way (what gets through them and the layer both); a
camera in or above them needs the froxel fog, which takes the shafts' place there.

### Seeing it

- *Light Shafts* on an *Environment*, every value edited live, in the editor's views and a running
  game. `Assets/Scenes/LightShaftsTest.nyxscene` (written by `NyxLightShaftsGpuTests
  --write-scene`): a ground, two rows of columns under a roof, the camera looking through them,
  hazy air with the shafts on, and a time of day at 17:12 with the sun low behind the columns;
  scrubbing the time moves the shafts and sets them with the sun.
- Their passes are timed on the GPU per view (`SceneViewGpuTimes::LightShafts`, *shafts* in the
  frame statistics, F8 in a game, `gpu_light_shafts_ms` in a benchmark's CSV).

## The high cloud layer

A thin layer of high cloud in the sky made by the air: a field of small cloudlets, a mackerel sky,
or streaks of cirrus, made from seeded noise and carried by the wind with the world's time, lit by
the sun through the air at its height, gold at sunset and dark after it, their thin edges bright
against the sun, fading into the haze towards the horizon, and lighting the world through the sky's
light. Seen from the ground, from a treetop at its height and from above it. The settings are a component of their own, `CloudLayerComponent`
(*Cloud Layer*, `Scene`); the math in `CloudLayer.h`, the pass `VulkanCloudLayer`, the shaders
`CloudNoise.slang` (its noise), `CloudLayer.slang` (the clouds per pixel) and
`Library/nyx/clouds.slang` (the shell, and the clouds before what lies beyond it). It is the plan's
step 6; the volumetric clouds of step 7, which the camera can be inside, are apart
([The volumetric clouds](#the-volumetric-clouds)).

Why a component of its own, not more settings on `EnvironmentComponent`: the air's settings are
already two dozen; a cloud layer is a look of its own that a world may have or not with the same
air; and the volumetric clouds are another component (`VolumetricCloudsComponent`) beside it, not
a mode of it: a world may have both, the high layer above the band. The world's
first one counts; without one, with it off, or with the sky in another mode than *Atmosphere*
(whose tables light it), there are no clouds and every picture is the same to the bit
(`NyxCloudLayerGpuTests`, and the other GPU tests' pictures against the build before it, all 71 of
five of them the same to the bit).

### The shell

The layer is a sphere round the planet's middle, *Altitude* (4000 m) above its ground, as the sky's
tables are planet-centred, the camera always above the middle, in kilometres
(`GetCloudShellHits()`, `nyx::cloudShellHits()`): a ray from the camera `h` up meets it where
`t² + 2 r μ t + c = 0`, `r` the camera's distance from the middle, `μ` the ray's cosine with the up
axis, `c = (h - a)(2R + h + a)` the difference of the squared radii taken by the heights, exactly,
never as a difference of 6360 km numbers; each root the form without cancellation.

- **From below** (`c < 0`) every ray leaves the shell once: overhead a few kilometres away,
  towards the horizon farther, about 225 km on the level from the ground for a layer 4 km up.
- **From above** a ray down meets it once if it then hits the ground, and twice if it grazes past
  the planet (in, and out on its far side, the second behind the first); a ray up never does.
- **Nothing behind the ground**: a crossing farther than the ground along the ray is dropped.

`NyxCloudLayerTests` checks the crossings against a ray marched in steps of 2 m, from 10 m, 3 km,
just below and just above the shell and 2 km above it, every way from straight up to straight down:
the same count, within a metre (a part in 10⁵ hundreds of kilometres off). Each pixel's ray comes
from the view's rotation and projection without its place (`CloudInverseSkyViewProjection`), so
it is as exact far from the world's middle as near it; the shell's height uses the camera's own
height (not the sky table's, which may be a metre and 1 % off: the wrong side of a layer the camera
is close to).

### Shapes from noise

The noise is a square of 256 × 256 texels that tiles, RGBA16F with its mips (0.7 MB), made by a
compute shader at the first frame the layer is drawn and when its *Seed* changes
(`CloudNoise.slang`), never otherwise:

- **r**, cloudlets: soft round domes, `(1 - d²)²`, one in each of 16 × 16 cells, each at a place
  and of a radius (0.4 to 0.75 of a cell) of its own from the seed, merged where they meet: the
  cellular (Worley-like) pattern of a mackerel sky;
- **g**, the same in 32 × 32 cells, finer, which break up their edges (*Detail*);
- **b**, fractal gradient noise of five octaves from 4 cells across, which the shader stretches
  four times along the wind into streaks of cirrus;
- **a**, fractal noise of four octaves from 2 cells across, large and slow: the domain warp and the
  coverage's patches.

Every octave's cells divide the square and their hashes wrap with it (Jarzynski and Olano's PCG
hash of the cell and the seed), so it tiles; the mips are halvings. **Why a texture, not noise
worked out per pixel:** the clouds are read at up to a few hundred kilometres along grazing rays,
where a pixel covers dozens of cloudlets. A texture's mips, filtered anisotropically over each
pixel's footprint, give their mean there for free; analytic noise would alias to glitter on the
horizon unless each octave were faded by the footprint by hand, and it would pay its hashes three
times a pixel (the cloud, the shadow towards the sun, a second crossing) where a texture pays a
tap each. Made once, it costs nothing per frame; a new seed makes it again in a few hundredths of a
millisecond.

Per pixel (`CloudLayer.slang`), at the point the ray meets the shell:

1. **Where over the pattern**: the point's offset from the camera over the ground, in the wind's
   frame (along it and across it) and in squares (*Cloudlet Size* × 16, 6.4 km by default), plus
   where the camera is over the pattern at the world's time (`Origin`, below).
2. **Its footprint** comes from ray differentials, not from the neighbouring pixels' points: the
   change of the ray to the next pixel (`ddx`, `ddy` of the direction, which is smooth everywhere)
   carried onto the plane the shell touches there, `dP = t (dd - d (n·dd) / (n·d))`; so the edge
   of the layer at the horizon, or between its first and second crossing, has no seam of a wrong
   footprint. It is widened to at most four times as long as wide: a grazing ray's is far longer,
   and wider samples along it cost less than sixteen of them (a 1080p view from 0.26 to 0.2 ms) and
   look the same on the horizon's haze.
3. **The field**: the pattern bent by the large noise (*Warp*), and the coverage there, moved up
   and down by the largest noise but not at none or all; two trilinear taps.
4. **The shape**: the cloudlets, cells a little shorter along the wind than across it (rows across
   the wind), and the streaks along it, blended by *Type*; broken up by the fine cells; thresholded
   by the coverage (`1 - 1.15 coverage`: overcast at 1), thicker towards each cloudlet's middle, the
   edges soft over *Softness* of the way in. As the footprint covers more than a cell or two, the
   density goes over to the coverage's mean, which a threshold of the filtered noise doesn't give:
   so the layer thickens to a haze towards the horizon, as many cloudlets seen edge on do.

### The wind and the world's time

Where the camera is over the pattern (`MakeSceneViewCloudLayerSettings()`) is its place over the
ground, along the wind (*Wind Direction*, round the up axis from +X towards -Z, as the time of day's
sunrise) and across it, less *Wind Speed* times the world's time along it, in squares, in doubles on
the CPU, kept between 0 and 16 squares, the period at which every scale the shader reads repeats.
So:

- the clouds are anchored in the world (a camera moving sideways passes under them) and carried by
  the wind, the pattern over a point at a time a function of the seed, the point and the time
  alone: the same sky for the same seed and time, on any machine;
- **the world's time** is the renderer's for the world (`RenderWorld::Time`): a game's its steps',
  drawn between the last two (`GetDrawnWorldTime()`), standing still while paused; the editor's,
  whose world isn't stepped, the renderer's own, so the clouds drift there as a decal's pulse moves;
- the origin never grows: after 100 hours of wind the GPU's numbers are as exact as at the start
  (`NyxCloudLayerTests`: a step of the wind after 360 000 s within 10⁻⁶ of a square of its speed's).

`NyxCloudLayerGpuTests` draws the same seed and time in a new view, its noise made again: the same
picture to the bit; 30 s later another; and the picture 30 s later against the one a camera 600 m
upwind saw 30 s before (the wind 20 m/s along +X): the same to the bit.

### Lighting

- **The sun at the layer**: the light's colour as it gives it (`ExtractedSceneGlobals::SunLightColor`,
  before the time of day's fade, as the air's sky is lit) times the transmittance table from the
  layer's height towards the sun, with the sun's cosine to the shell's own up there; none once the
  sun is below the horizon seen from that height, fading over about half a degree as its disk goes
  down. So it is gold and red as the sun sets, and the layer, 4 km up, keeps the sun about two
  degrees after the ground has lost it, then goes dark.
- **What a cloud does with it** is two-stream scattering of a slab whose optical depth is the
  fake thickness from the noise (the density times *Thickness* times 60 per kilometre: 18 at a
  cloudlet's middle by default), along the sun's slanted path: it lets `1 / (1 + 0.11 τ)` through,
  scattered, and sends the rest back (0.11 is `3/4 (1 - g)`, the droplets' forward scattering `g`
  about 0.85). Seen from the side the sun lights, what it sends back; from the other, what it lets
  through less the light that goes straight through unscattered; each per part of what the cloud
  hides, as it is blended. So from below a cloudlet's thin edges are bright and its middle greyer,
  and from above its tops bright; the side is the sign of the sun and of the ray to the shell's up,
  smoothed over a few degrees, so a sun in the shell's plane lights both alike.
- **A forward lobe** on the light let through (Henyey-Greenstein, `g` 0.6), the more the thinner
  the cloud: clouds near the sun are much brighter than away from it (`NyxCloudLayerGpuTests`: at
  noon 206 197 183 of 255 towards the sun, 167 160 149 away).
- **A rim**, the silver lining: a sharper lobe (`g` 0.75, at most 12 times the even light) on the
  thin edges, `(1 - density)²`, by *Rim*.
- **A low sun's light comes from the side**: the density a third of a cell towards the sun shadows
  it, by `1 - |cos|` of the sun's angle to the shell's up: a sunset lights each cloudlet's sunward
  side and leaves its far side darker; a sun overhead lights through tops and bases.
- **The sky's own light**: the irradiance cube's light from above and below (the night sky's
  light in it at night), more of the side the camera sees, by *Ambient*. At midnight the clouds are
  the night sky's blue, dark (`NyxCloudLayerGpuTests`: 0 3 12 on average, against 127 136 143 at
  noon at the same exposure).
- *Brightness* times all of it.

A sun of intensity 3 lights a white surface three times what the screen shows as white, and a
sunlit cloud is as bright, so at exposure 0 the clouds are near white, as a sunlit wall is; the
view's exposure or the grade brings their shading back. The GPU tests look at them a stop and a
half darker.

### Through the air, and near the camera

- **The haze between**: the transmittance along the ray from the camera to the cloud, from the
  table, the camera's over the cloud's (or, for a ray down into the ground, along it turned back).
  The cloud's light is multiplied by it, and it hides what is behind it by its opacity times its
  mean: so far off, where the air lets little of it through, the layer becomes the sky's own colour
  behind it, which stands in for the air's light between (no aerial perspective of its own yet).
- **Opacity**: `1 - e^(-τ / |μ|)`, `μ` the ray's cosine to the shell, at least 0.03: grazing rays
  see the clouds as thick as at about 1.7 degrees, so the layer closes up towards the horizon.
- **Near the camera** (`GetCloudNearFade()`): a cloud a quarter of the *Thickness* or nearer is
  gone, all of it there from twice the thickness on, smoothly between. A thin 2D layer is a
  surface; without this, a camera passing through it would see a sheet sweep through the picture.
  With it, the camera's distance to the shell is the nearest any part of it can be: from 10 m below
  the layer, everything within 82 degrees of straight up is clear (the sky to the bit, in the test),
  the layer seen only as a band towards the horizon, which narrows as the camera rises, is gone at
  its height (the rays up leave the shell at once, the rays down meet it far off), and from above
  the near clouds below fade in as the camera rises away. `NyxCloudLayerGpuTests` draws the level
  view from 20 m and 5 m below, at the layer and 5 m and 20 m above: the steps change the picture by
  0.4 and 0.6 of 255 on average round the layer itself, 2 and 2.5 for the 15 m steps beside it,
  against 56 from 2 km below to 2 km above.

### What it is drawn over and under

The pass runs per view before the scene pass, after the sky's tables it reads, into a target of the
view's size (RGBA16F: the clouds' light, and in alpha the part of the light from behind them that
gets through; 16.6 MB at 1920 × 1080), written whole, one triangle. Then:

- **the sky** (`Skybox.slang`): its light and the sun's disk times what gets through, plus the
  clouds' light, then the height fog over them, so fog lies over the clouds;
- **what lies beyond the shell** (`nyx::cloudCoverBefore()`, in `surfaceFragment`,
  `decalFragment` and the particles): a surface whose point is on the other side of the shell from
  the camera (by the planet-centred heights) takes the clouds at its pixel the same way, before its
  fog: from above a low layer the ground under it is covered, a column standing above it isn't
  (`NyxCloudLayerGpuTests`, a layer 300 m up seen from 800 m). Nothing else is: a surface on the
  camera's side of the shell is the same to the bit, so the layer never draws a sheet over a tree
  below it seen from the ground, nor over a treetop above it seen from above;
- **the light shafts**: the sky's pixels stream only as much as the clouds let through (above);
- **the world's sky light**: the clouds are in the sky's cube that lights it (below); the sun's
  light isn't shadowed by them (cloud shadows are later).

### It lights the world

While *Lights The World* is on (the default) and *Light Strength* above 0, the sky's cube, from which
the world's diffuse and specular light are made ([Its light lights the world](#its-light-lights-the-world)),
has the clouds in it, as the painted cube map had its clouds: white clouds brighten and whiten the
fill light, golden ones at sunset warm it, heavy cover softens it, and from above the layer its
sunlit tops below the horizon light the world from below.

- **Drawn with the same code** as the pass: `skyCube` (`SkyAtmosphere.slang`) works out the clouds
  in each texel's direction with `nyx::cloudLayerAlong()`, from what describes them
  (`CloudParams`), here from the sky's own constants rather than the view's set: the layer's
  settings, its noise and where the camera is over the pattern, the same as the view's; each
  texel's footprint the ways to its neighbours; the camera's height the table's. The sky's light on
  the clouds comes from the sky view, its mean over five directions above and five below (the
  irradiance is made from this cube, after it). The texel is the sky times what gets through plus
  the clouds' light, mixed with the sky alone by *Light Strength*: 1 as they look, 0 none.
- **Off, or at strength 0**, the cube is made as without the layer, to the bit: `skyCube` takes no
  other path (`NyxCloudLayerGpuTests`: a white wall lit by the sky alone, the same to the bit).
- **Made again only when it must be** (`IsCloudCaptureChanged()`): with the sky view, as before
  (the air, the sun, the camera's height); and for the clouds alone, the cube, its mips, its
  irradiance and its specular light but not the sky view, when they come or go, a setting or their
  noise changes, or the pattern moves under the camera by more than a 32nd of a cell
  (`CloudCaptureTolerance`: 12.5 m at the default *Cloudlet Size*), carried by the wind or by the
  camera going sideways. The cube's texels are 1.4 degrees, about 100 m of a layer 4 km overhead,
  so a smaller move doesn't show in its light. A wind of 10 m/s makes it again every 1.25 s; the
  test's 20 m/s, 15 times in 600 steps of a 60th of a second, 0.117 ms each: about 0.003 ms a frame
  on average.
- **The noise first**: a new seed's noise is made before the sky's tables, in the same frame, so the
  cube never reads it before it is there; its time then counts in the sky's.
- Near the layer, the table's camera height may be a metre and 1 % off the camera's (40 m at 4 km),
  so the cube's clouds may be a little off the picture's there; the near fade keeps it soft.
- `NyxCloudLayerGpuTests`, a white wall the sun doesn't reach, at exposure 0: at noon 153 158 169
  under a clear sky, 226 217 203 under a white overcast layer, 194 191 187 with half its light; at
  sunset 75 37 9 under the layer, 26 20 35 without its light; a ceiling 2 km above the layer, lit
  from below, 244 237 228 by the clouds against 197 192 189 by the ground under the air.

### Its cost

`NyxCloudLayerGpuTests`, the median of 60 frames, RTX 3070 Ti, from 10 m looking 30 degrees round
and 20 up (every pixel meets the layer once), three runs:

| View | The cloud layer (cloudlets) | Cloudlets and streaks blended |
| --- | --- | --- |
| 1280 × 720 | 0.11 ms | 0.15 ms |
| 1920 × 1080 | 0.20 ms | 0.26 to 0.28 ms |

About nine texture taps a pixel (two trilinear for the field, two anisotropic for the shape and its
detail, one more towards the sun, three of the transmittance table, two of the irradiance cube, the
same for every pixel), and a second crossing only where a ray from above grazes past the planet.
The noise, when its seed changes: about 0.03 ms more in that one frame. The sky's cube with the
clouds in it, made again as they move: 0.117 ms each time (above), in the sky's time. Every pixel of the view
pays it, also those a mesh covers: the sky pass can't tell the pass before it which those are. Half
size with a careful upsample would halve it again, if a scene ever needs it; the cloudlets' sharp
edges are why it is full size. The scene pass is the same with it and without, within the spread.

### Seeing it

- *Cloud Layer* on an entity, with the *Environment*'s sky in its *Atmosphere* mode: every value
  edited live, in the editor's views and a running game.
- `Assets/Scenes/CloudLayerTest.nyxscene` (written by `NyxCloudLayerGpuTests --write-scene`): the
  plan's frame 1, a field of small cloudlets receding to the horizon at dusk, the time of day at
  17:44 with the sun 3 degrees up behind them, and a colour grading whose key at that height is
  the gold-green dusk; scrubbing the time turns them from white to gold to dark.
- Its pass is timed on the GPU per view, with the noise in the frames that make it while it doesn't
  light the world (`SceneViewGpuTimes::Clouds`, *clouds* in the frame statistics, F8 in a game,
  `gpu_clouds_ms` in a benchmark's CSV, the editor's frame time tooltip); the sky's cube made again
  for it, and the noise while it does, in the sky's (*sky*, `gpu_sky_ms`).

### Its limits, and what the volumetric clouds take from it

- **A surface, not a volume**: it has no height of its own; up close it fades rather than
  surrounds the camera, and it can't be flown into, round or between. The plan's step 7 is the
  volume, with the camera inside it.
- **One layer, one shape a pixel**: no second layer of other cloud at another height, and the type
  blends the two shapes everywhere rather than placing each by a weather map.
- **No cloud shadows** on the ground, the trees or the fog (step 8); the clouds are in the sky's
  light round the camera, the same everywhere, not shading the world under each cloud.
- **The haze between** is the sky blended in by the air's transmittance, not the air's own light
  along the shorter path: right in the earth's air and for a layer a few kilometres up, a little
  off for a very low layer under thick haze. The aerial perspective (step 3) is the exact way.
- **Lit from the camera's height's sky**: its ambient is the irradiance cube made for the camera,
  not for the layer's height.
- **Lighting is a slab's**: two-stream scattering with a fake thickness, no light between
  cloudlets bouncing off each other, no light from below off the ground beyond the cube's.

What the volumetric clouds (step 7) were to reuse from it, as planned here (what they did is in
[The volumetric clouds](#the-volumetric-clouds)): the planet-centred shell math (their band
is two shells; `GetCloudShellHits()` gives where a ray enters and leaves each, from below, inside
and above), the camera's own height for it, the ray from the view without its place; the sun's
light at a height from the transmittance table (`nyx::cloudSunLight()`), the air between by the
table's ratio, the sky's light from the irradiance cube, all behind `CloudParams`, callable from a
pixel or a compute pass; drawing into the sky's cube, so they light the world the same way; the
wind's origin in doubles kept small, and the
world's time; the noise made from the seed by a compute shader (their 3D noise the same way, with
these hashes), filtered by ray differentials; the target with the light and what gets through,
which the sky, the surfaces beyond and the light shafts already read, so a raymarched band can
write the same target at a lower resolution and the rest needs nothing new; the near fade becomes
the camera inside the band, marching from where it stands. What they replace: the slab's lighting
by the band's march (Beer's law, the powder term, multiple scattering by octaves), the type blend by
a weather map, and the high layer stays above them for cirrus.

## The volumetric clouds

Clouds with volume: a band of them between two altitudes round the planet's curve, marched along
each pixel's ray, so they have bright sunlit tops and shaded bellies, thin edges that glow against
the sun and dark cores, and are seen from the ground, from inside the band and from above it. A
weather map places them, 3D noise shapes them, the sun through the air and through the clouds
themselves lights them, scattered many times. The settings are a component of their own,
`VolumetricCloudsComponent` (*Volumetric Clouds*, `Scene`); the math in `VolumetricClouds.h`, the
passes `VulkanVolumetricClouds`, the shaders `VolumetricCloudNoise.slang` and
`VolumetricCloudWeather.slang` (the noises), `Library/nyx/cloudnoise.slang` (tiling noise),
`Library/nyx/volumetricclouds.slang` (the march) and `VolumetricClouds.slang` (the passes). It is
the plan's step 7, its first part.

Why not more of the high cloud layer: a thin shell has one shape a pixel and a fake thickness; its
light is a slab's, and it can't have tops, bellies, sides or a camera among them, which are what
the cumulus of a summer sky are made of. Why a component of its own, not a mode of the layer: a
world may want both, the layer high above the band for thin cirrus, and their settings have little
in common. The world's first one counts; without one, with it off, or with the sky in another mode
than *Atmosphere* (whose tables light them), there are none and every picture is the same to the
bit (`NyxVolumetricCloudsGpuTests`, and the other GPU tests' pictures against the build before it).

### The band

Two shells round the planet's middle, *Bottom Altitude* and *Top Altitude* above its ground (1500
and 5000 m by default), in kilometres, planet-centred as the sky's tables and the high layer are.
Where a ray is inside the band (`GetCloudBandSpan()`, `nyx::volumetricCloudBandSpan()`) comes from
where it meets each shell (the layer's `GetCloudShellHits()`, exact by the heights):

- **from below**, from where it leaves the bottom shell to where it leaves the top one; none where
  it meets the ground first (from the ground, the level ray's span starts about 140 km off);
- **inside**, from the camera to where it leaves, through the bottom shell if it goes down into it,
  else the top;
- **from above**, from where it enters the top shell to where it leaves through the bottom, or past
  the planet's curve through the top again; a ray that would enter again beyond counts only its
  first span.

`NyxVolumetricCloudsTests` checks each end on a shell within a metre and the middle inside, from 2 m,
800 m, just below the bottom, inside, just below and above the top, 7 and 20 km, every way from
straight up to straight down; and that a ray found to miss has nothing of the band before the
ground. The march stops at *Max Distance* (150 km) from the camera.

### The weather map

A square of 512 texels a side that tiles, laid over the ground, *Weather Size* × 4 across (48 km by
default), carried by the wind (`VolumetricCloudWeather.slang`, made from the seed):

- **r, the coverage**: fractal gradient noise from 4 cells across, mixed with cellular noise, so
  there are patches and bands. *Coverage* is moved up and down by it, by *Coverage Variation* (0 an
  even field, 1 patches and bands with open sky between), but not at none or all: no coverage stays
  clear, all of it overcast.
- **g, the type**: larger, slower noise; *Type* moved by *Type Variation*: tall clouds here, small
  puffs there.
- **b, the density**: some clouds thicker than others (0.6 to 1.4 times *Density*).
- **a**, finer cells, not read yet.

### Shapes from noise

Two cubes of noise that tile, made by compute shaders when the clouds are first drawn and when the
*Seed* changes (`VolumetricCloudNoise.slang`), never otherwise, with their mips:

- **the shapes**, 128 texels a side (8 MB), over *Cloud Size* × 4: Perlin-Worley, fractal gradient
  noise of five octaves lifted by cellular noise (round, billowy blobs), worn where cellular noise
  of three finer octaves is low (Schneider's remap). That field is about normal, its mean 0.83 and
  its deviation 0.056, so a threshold of it covers all or nothing; it is spread out by its
  distribution (a logistic close to the normal's) to lie about evenly between 0 and 1, so that a
  threshold at `1 - c` leaves about `c` of space inside (0.22, 0.43 and 0.61 for 0.2, 0.4 and 0.6,
  measured). Its far mips, which average blobs and gaps together, are spread out again as much
  (their values huddle round the middle from the third mip on), so the clouds cover as much of the
  sky far off as near.
- **the details**, 32 texels a side, over *Detail Size* × 4: cellular noise of three octaves, the
  small billows and wisps.

Per sample (`nyx::volumetricCloudDensity()`), at its height `h` through the band (0 its bottom, 1
its top), with the weather there:

1. **How tall the type grows**: stratus a thin sheet at the bottom, small puffs the lower 20 to 45 %,
   towering cumulus up to most of the band, blended by the type; each flat at the base and rounding
   off towards its top (`volumetricCloudHeightProfile()`). The shapes' field is multiplied by it, so
   the threshold rounds the tops.
2. **Thresholded by the coverage**: `remap(field, 1 - coverage, 1, 0, 1)`.
3. **Worn by the details**, by *Detail*: cells worn into wisps at the base, round billows (the gaps
   between them worn, inverted cells) from a quarter of the way up, by *Billowiness*.
4. **Its edges** as soft as *Edge Softness* says (the density reaches its full value that far in),
   denser towards the middle so the light inside shades, times the weather's density.

The extinction is the density times *Density* times 40 a kilometre (a cumulus's 0.04 a metre: 200 m
of it lets a third through).

### The wind and the world's time

The noises are carried by *Wind Speed* along *Wind Direction* (round the up axis from +X towards
-Z) with the world's time, as the layer's: where the camera is over each of them
(`MakeSceneViewVolumetricCloudsSettings()`) is its place over the ground less the wind times the
world's time, in that noise's repeats, in doubles on the CPU, kept between 0 and 1; the shaders add
each sample's offset from the camera. So the clouds are anchored in the world and carried by the
wind, the same sky for the same seed and time on any machine, and the origins never grow
(`NyxVolumetricCloudsTests`: a second of the wind after 100 hours within 10⁻⁶ of a repeat of the
first). The world's time is the renderer's for the world (`RenderWorld::Time`), as the layer's.

### Lighting

Each sample in a cloud (`volumetricCloudSample()`):

- **The sun at the sample**: the light's colour as it gives it (`ExtractedSceneGlobals::SunLightColor`,
  before the time of day's fade, as the air's sky is lit) times the transmittance table from the
  sample's height towards the sun, with the sun's cosine to the sample's own up; none once the sun
  is below the horizon seen from there, fading as its disk goes down. So gold and red at sunset,
  the band's top keeping the sun a little longer than its bottom.
- **Through the clouds towards the sun**: Beer's law over the optical depth of *Light Steps* (6)
  samples further and further apart (quadratically) up to 35 % of the band's thickness, at most
  1.5 km, the first with its details; and one twice as far, for the shadow of a large cloud beyond.
  The samples are as jittered as the ray's, so their steps don't show as bands of shadow.
- **Scattered once and many times** (Wrenninge's octaves, as Hillaire's Frostbite clouds have them):
  four orders, each lit through 0.3 times the depth of the one before, by a lobe half as sharp, and
  *Multiple Scattering* times as strong; the orders after the first in *Shadow Tint*. So a cloud's
  inside, which the light reaches only by scattering, is lit softly and in the tint, not black.
- **The phase**: a dual-lobe Henyey-Greenstein, half a weak lobe backwards (`g` -0.2) and half a
  forward one (`g` from 0.9 to 0.5 by *Silver Lining Spread*), its light above even scattering
  times *Silver Lining*: the thin edges of a cloud against the sun glow.
- **The powder** of thin edges (Schneider's): seen away from the sun, a sample's light is dimmed by
  how thin it is, `1 - e^(-σ · 60 m)`, by *Powder*: the sunlit edges and creases are a little darker
  than the insides, which gives the cauliflower its relief.
- **The sky's light**: the irradiance cube's from above, 15 % of it at the band's bottom to 70 % at
  its top (the clouds above hide it), and a quarter of the ground's from below, by *Ambient*, in
  *Shadow Tint*: the bellies grey-violet, and at night the night sky's blue, dark.
- *Brightness* times all of it.

Each sample adds what it scatters towards the camera as much as it hides (`(1 - e^(-σ Δt))` of what
still gets through, the albedo taken as 1: energy-conserving, so a step's length changes how much
it hides, not how bright it is).

### The march

Along each ray inside the band, more steps along the longer rays towards the horizon, up to *Steps*
(128): half of them through the band straight up, all of them from about four times its thickness.
Coarse steps through the clear air test the shapes alone (no details, no light); where they find
cloud, the step before it (where it may have begun) and every step through it are taken again in
quarters, with the details and the light. The march stops once less than 0.5 % of the light gets
through. Where in each step a pixel's samples are is the 2 × 2 Bayer matrix's order by the pixel
(a quarter of a step apart), the same every frame, so a still picture stays still; the filter up to
the view's size, a 2 × 2 box, evens them out exactly. A 4 × 4 matrix's sixteenths showed as a fine
regular pattern on the near clouds' sides through any filter that kept their edges.

Each ray gives three numbers: the clouds' light, what of the light from behind them gets through,
and how far along the ray they are (where their light comes from on average, weighed by how much
each sample hid; where the band ends for a ray through none).

**Through the air**: the clouds' light is multiplied by the transmittance between the camera and
that depth (the table's ratio, as the layer's), and they hide what is behind them by their opacity
times its mean, so far off they melt into the sky behind them, which stands in for the haze's own
light between (no aerial perspective of their own yet).

### At half size, carried from frame to frame

The march runs at half the view's size each way (960 × 540 at 1920 × 1080), and with *Temporal* on
(the default) only one pixel of each 4 × 4 a frame, the one the frame's place in a Bayer matrix's
order says (`GetVolumetricCloudUpdateOffset()`: every pixel once in 16 frames, those of
neighbouring frames far apart). Two passes a frame (`VolumetricClouds.slang`):

1. **The march** (`volumetricCloudsMarchFragment`), into a quarter of its target each way: one ray
   for each 4 × 4 of the half size.
2. **The resolve** (`volumetricCloudsResolveFragment`), into one of two pictures of the half size,
   from the march and the other one, the frame before's: the pixels marched this frame from the
   march; every other pixel from where the clouds it looks at were in the frame before: as far
   along its ray as the frame before's clouds at the pixel were, moved back by how far the camera
   moved and the wind carried them since (`ViewConstants.VolumetricCloudMotion`), seen through the
   frame before's rotation and projection. A point that lands exactly where it was (within a
   hundredth of a texel) is taken as it was; else it is filtered by Catmull and Rom's cubic (nine
   bilinear taps): sharp, so clouds carried frame after frame as the camera turns don't blur, as
   bilinear filtering would blur them a little each time. Where that is off the picture, this
   frame's marched pixels round it, blended.

**Everything is marched again**, every pixel in one frame, when what the frames before were marched
with no longer holds (`IsVolumetricCloudsLookChanged()`): the first frame, a new size, a new seed,
any setting but the wind's place (a value dragged in the Details panel is seen at once, not over 16
frames), or the sun turning more than 0.03 degrees in a frame (a time of day running a day in 20
minutes turns it 0.005 degrees a frame at 60 frames a second; one scrubbed in the editor far more);
and when the camera jumps or the wind carries the clouds more than 500 m in a frame. A pixel is
marched again within 16 frames otherwise, so a sun turning slowly lights them all within a tenth of
a degree and a half of each other.

`NyxVolumetricCloudsGpuTests` checks that a still picture, carried for 17 frames, is the same to the
bit as every pixel marched; and with the camera turned 3 degrees a frame for 5 frames, the part that
was in view before within 1.4 of 255 on average of every pixel marched, which keeps, until a
pixel is marched again, the dither of the pixel it was marched at (the left quarter that came into
view, its pixels marched one of 16 a frame since, blocky until they are: 2.2 over the whole
picture).

**Read filtered up** (`nyx::volumetricCloudsAt()`): the sky, the surfaces and the light shafts read
the picture of the half size as the mean of four bilinear taps half a texel round each pixel (a 2 × 2
box over the bilinear filter): smooth, and the march's 2 × 2 dither evened out exactly, which a cubic
B-spline (tried first) only damps to a third. The clouds are worked out along each ray whatever
stands in the way, so no depth-aware filter is needed: the sky's pixels next to a wall's edge read
the clouds behind it as they are.

### What it is drawn over and under

The passes run per view after the high layer's pass, before the scene pass, reading the sky's
tables. Their pictures, RGBA16F (the light, and in alpha what gets through) and R32F (the depth, in
kilometres), three of each at the half size (the march, two pictures carried in turn: 12.4 MB at
1920 × 1080), with the noises (8.6 MB), made at the first frame they are drawn, let go of when they
go off, with the view, or when its world goes. Then (`nyx.clouds`, `cloudsAt()`):

- **the sky**: its light and the sun's disk times what gets through both, plus the volumetric clouds'
  light, and the high layer's behind them: the layer lies above the band (`volume + layer × what
  gets through the volume`). Its altitude may be inside the band; it is still drawn behind it;
- **what lies beyond them** (`nyx::cloudCoverBefore()`, the surfaces, the decals and the particles):
  a point farther along its ray than the clouds' depth at its pixel takes them the same way, before
  its fog: from above the band the ground under it is covered; from below, nothing on the ground is;
- **the light shafts**: the sky's pixels stream only as much as all the clouds let through;
- **the world's light**: not yet; the sun's light isn't shadowed by them either (below).

### Presets

*Preset* sets the clouds' and their lighting's values to a look at once
(`ApplyVolumetricCloudsPreset()`): *Small Puffs*, a dense field of small, puffy, cauliflower-topped
clouds low in the band; *Cumulus*, big heaps with bright tops and grey-violet bellies, smaller puffs
round them, open blue between; *Broken*, a dense broken layer whose thin worn edges glow against a
low sun over dark cores. *Custom* sets nothing. Whether the band is drawn, its seed, its wind and its
quality stay as they are.

It is the component's post-load step, which runs after the component is loaded, edited (also
undone and redone) or changed in a running game: it sets the values when *Preset* isn't the preset
they were last set to (*Applied Preset*, saved and hidden), so choosing one sets them once and they
can be changed after; a scene saved with them changed loads with them changed. Undoing the choice
of a preset brings back the preset before (whose values are set again, if it wasn't *Custom*), not
the values changed by hand before it.

### Its cost

`NyxVolumetricCloudsGpuTests`, the median of 60 frames, RTX 3070 Ti, from 10 m looking 30 degrees
round and 20 up, the component's defaults at a coverage of 0.5 (the shaders are compiled alike in a
Debug and a Release build):

| View | Carried (one pixel of each 4 × 4 marched) | Every pixel marched each frame |
| --- | --- | --- |
| 1280 × 720 | 0.79 ms | 2.05 ms |
| 1920 × 1080 | 0.79 ms | 3.50 ms |

Carried, the time hardly grows with the view: a quarter of the half size's 4 × 4 is one ray a 4 × 4,
14 400 rays at 1280 × 720 and 32 400 at 1920 × 1080, fewer than the GPU runs at once, so the march
takes about as long as its slowest rays, those towards the horizon through many clouds; the
resolve, nine bilinear taps a pixel of the half size, is a small part. Every pixel marched (the
frame a value is changed, the sun scrubbed): 3.5 ms at 1920 × 1080. The noises, when the seed
changes: the frame that makes them takes 3.9 and 5.2 ms, every pixel marched in it. The scene pass
is the same with them and without, within the spread. Within the plan's 1 to 2 ms at 1080p,
carried; a game that scrubs its sun or changes the clouds' values every frame pays the full march.

### Seeing it

- *Volumetric Clouds* on an entity, with the *Environment*'s sky in its *Atmosphere* mode: every
  value edited live, in the editor's views and a running game; *Preset* to start from a look.
- *Debug View*: the weather map where each ray enters the band (coverage red, type green, density
  blue), the sun's light alone, the sky's light alone, or which pixels were marched this frame (red),
  carried (grey) or had nothing to be carried from (blue).
- `Assets/Scenes/VolumetricCloudsTest.nyxscene` (written by `NyxVolumetricCloudsGpuTests
  --write-scene`): cumulus over a town's wall, a time of day in the afternoon, a grade gold-green
  at dusk; scrubbing the time turns them white to gold to dark. `NyxVolumetricCloudsGpuTests
  --pictures` renders it with each preset lit as its reference picture, at noon, at sunset against
  the sun and from the ground (into `NYX_SCENE_TEST_OUTPUT`).
- Their passes are timed on the GPU per view, with the noises in the frames that make them
  (`SceneViewGpuTimes::VolumetricClouds`, *volume* in the frame statistics, F8 in a game,
  `gpu_volumetric_clouds_ms` in a benchmark's CSV, the editor's frame time tooltip).

### Its limits, and the next steps

- **Not yet**: their shadows on the ground, the trees and the fog (they would need a shadow map of
  the band from the sun, the march's light depth from above); the light shafts masked by them in
  the air (now only on the picture, by what they let through); their light in the sky's cube that
  lights the world (the layer's capture is the way: the march from the cube's texels, at a low
  resolution, made again as they move); the camera flying into and through them at cloud height.
  The march already starts at the camera inside the band and the depth it writes covers a surface
  beyond it; what is missing there is the near clouds' detail at a pixel's footprint, a denser
  march close by, and the carried pictures' reprojection of near clouds as the camera flies.
- **The haze between** is the sky blended in by the air's transmittance, as the layer's, not the
  air's own light along the shorter path; the aerial perspective (step 3) is the exact way.
- **Their sky light** is the irradiance cube made for the camera's height, with a fixed occlusion
  by the height in the band, not the sky over each cloud.
- **Carried pictures**: a part of the picture that comes into view (a fast turn) is blocky for up to
  16 frames, and a cloud moving fast across the picture trails a little; a camera jump or a changed
  value marches everything again (a slower frame).
- **One span a ray**: from above, a ray grazing past the planet's curve doesn't see the clouds on its
  far side again.

## Colour grading

The picture graded as a painter or a colourist would: its white balance, contrast, shadows,
midtones and highlights, saturation, and its shadows and highlights tinted apart. The settings are
on a component of their own, `ColorGradingComponent` (`Scene`): the world's first one counts, and
without one, or with *Grading* off, every picture is the same to the bit (`NyxColorGradingGpuTests`).
The math is in `ColorGrading.h` (`Render`), the pass in `VulkanColorGrading`, the bake in
`ColorGrading.slang`, the lookup in `Tonemap.slang`.

Why a component of its own, not more settings on `EnvironmentComponent`: grading is how the
picture is shown, not the world's air. A world may want the air without a grade, or a grade
without fog; and the grade's keys, each a whole grade, would double the environment's settings.

### Where it is applied: after the tonemap

The grade works on the picture as the tonemap shows it, sRGB-encoded, 0 to 1, between the tonemap
(Khronos PBR Neutral) and the 8-bit picture:

- **Its controls have a fixed black and white.** Lift moves black and keeps white, gain the other
  way; split toning parts the shadows from the highlights by how bright they are shown. On the HDR
  scene, "the highlights" would have no top, and what counts as a shadow would change with the
  exposure and the sun's intensity; after the tonemap a grade looks the same whatever they are.
- **The table's range is exact.** The tonemap's output is 0 to 1 each way, so 32 texels a side
  cover all of it evenly in sRGB's steps, which are even to the eye: no shaper curve, no range to
  pick, nothing clipped. An HDR table would need a log shaper and a range of stops, and would spend
  its texels on light no screen shows.
- **The tonemap stays what it was.** PBR Neutral passes colours up to 0.76 through all but
  unchanged and keeps hues, so grading after it grades nearly the light itself in the shadows and
  midtones, where most of a grade's work is. Bloom is added before the tonemap, so it is graded
  too: a warm grade warms the glow.
- **White balance** is on linear light still: the encoded colour is made linear, adapted, and
  encoded again in the table, which costs nothing per pixel.

### The grade

`GradeColor()` on the CPU, `gradeColor()` in the bake, the same math in the same order, on each
colour the table stands for:

1. **White balance** (*Temperature*, *Tint*): a von Kries adaptation in CIECAM02's cone space
   (CAT02), from a white on the CIE daylight locus to D65. *Temperature* moves that white's x by
   0.06 a unit (from about 25 000 K for 1 to 4 300 K for -1), so a picture lit by a bluer white is
   made warmer; *Tint* moves its y by 0.04 a unit, across the locus, green to magenta. Scaled so
   white keeps its luminance; at 0 and 0, the identity. One 3 × 3 matrix, worked out on the CPU.
2. **Contrast**, on what is shown, round mid grey (0.18 linear, 0.461 encoded): a power curve each
   side, `p (x / p)^k` below and `1 - (1 - p) ((1 - x) / (1 - p))^k` above, as steep as `k` at mid
   grey from both sides; 0 and 1 kept.
3. **Lift, gamma, gain**, each red, green and blue, on what is shown:
   `x = gain (x + lift (1 - x))`, then `x^(1 / gamma)`. Lift moves black to itself and keeps white;
   gain moves white and keeps black; gamma bends the midtones, black and white kept.
4. **Split toning**, on light: by how bright a colour is shown (its luminance, encoded, moved by
   *Tint Balance* / 2), a smoothstep from shadows to highlights; the colour multiplied by each tint's
   hue at luminance 1, by its strength and its share. Grey keeps its luminance; black stays black.
5. **Saturation**, on light, round the colour's luminance, which it keeps.

Every value is held in range first (`MakeColorGradingConstants()`): *Temperature* and *Tint* -1 to
1, *Contrast* 0.01 to 4, *Lift* -1 to 1, *Gamma* 0.1 to 10, *Gain* and *Saturation* 0 to 4, the
strengths 0 to 1, the balance -1 to 1, so no grade gives a NaN or a colour outside 0 to 1. The
defaults are the identity: every texel of their table is its own colour, within 1e-4 on the CPU and
within 1e-3 on the GPU (half floats).

### The keys: graded by the sun's height

With *Use Keys*, *Keys* holds grades, each at a height of the primary light above the horizon, in
degrees, in any order. The view's light (`ExtractSceneLight()`, the same `DirectionalLightComponent`
everything else follows) gives the sun's height, `asin` of its direction's y; between the two keys
round it each value is mixed by where the sun is between them (`BlendColorGradingKeys()`), below the
lowest or above the highest that key is used. Without keys, or with *Use Keys* off, *Grade* is.

- **The values are mixed, not the tables or the constants**: halfway between two keys every
  control is halfway, as the editor shows them; the white balance's matrix is then made from the
  mixed temperature and tint.
- **Only the sun's height counts.** Nothing in the grade depends on the camera: a camera on the
  ground, in a treetop or a kilometre and a half up at cloud height grades alike (the GPU test
  moves it there). A grade by the camera's height, e.g. hazier and bluer high up, would be keys of
  its own on the same blend, if a look ever wants it.
- **Sunrise and sunset at the same height share a grade.** The keys know the sun's height, not
  which way it moves. A key for the morning apart from the evening needs the time of day (the
  sun's path), and could be a key's side of noon.
- Two looks of the plan as keys (`NyxColorGradingGpuTests`' `GoldenHourKeys()`, the test scene):
  a gold-green dusk 3 degrees up (warm, green-tinted, teal shadows lifted a little, gold
  highlights) and a warm backlight at 12 degrees (warmer, gold highlights over violet-blue
  shadows, more contrast), between a cool, desaturated night at -8 and a clear noon at 50.

### Baked into a table

Each view has a lookup table, 32 × 32 × 32 texels, RGBA16F (256 KB; a core storage format, filtered
everywhere), made at the first frame it grades and let go of when it stops (`VulkanColorGrading`):

- **Baked on the GPU** by one compute dispatch of 8 × 8 × 8 groups (`colorGradingBake`), its grade in
  push constants (128 bytes, the most every device takes), recorded in the frame's own commands
  before the view's picture pass, with a barrier each way: after the frames before read it, before
  this one's tonemap reads it. On the CPU the same bake would take milliseconds in a Debug build, a
  hitch whenever the sun moved.
- **Only when it moved noticeably**: the frame's grade, made ready for the bake, is compared with
  what the table was baked from, value by value; it is baked again only when one moved by more
  than 1/1024 (`IsColorGradingBakeNeeded()`), well under a step of the 8-bit picture. Compared with
  what was baked, not with the frame before, so a slow sun can't drift away from it: a sun rising
  20 degrees over 20 000 frames bakes a few hundred times (`NyxColorGradingTests` prints it), a
  still one never.
- **Looked up** in the tonemap (`View.ColorGradingTable`): the picture, sRGB-encoded, scaled to run
  from the middle of the first texel to the middle of the last, sampled with the linear clamp
  sampler; one lookup a pixel instead of the grade's handful of powers.
- **Per view, not per world**: views of one world bake the same table; at 256 KB and a dispatch
  when it changes, sharing it isn't worth a cache with its lifetimes.
- **Not graded**: a material's debug views (no table is made for them) and the editor's overlays,
  drawn over the picture. *Debug > Bloom* and *Ambient Occlusion* show their passes ungraded.

## Seeing it

- *Environment* on an entity in the editor: every value edited live, in the editor's views and a
  running game. `Assets/Scenes/AtmosphereTest.nyxscene` (written by `NyxAtmosphereGpuTests
  --write-scene`) is the ground test scene with about 24 000 small cubes scattered over its ground,
  a hazy environment, a low sun, and for bloom a glowing cube (`Materials/Emissive.nyxmat`) and a
  glowing ring (`MaskedDecal`) on a crate.
- *Color Grading* on an entity, the same way. `Assets/Scenes/ColorGradingTest.nyxscene` (written
  by `NyxColorGradingGpuTests --write-scene`) is the atmosphere test scene with a grading of four
  keys, a cool night at -8 degrees, a gold-green dusk at 3, a warm backlight at 12 and a clear noon
  at 50; turning its sun moves the grade between them.

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

Ambient occlusion, on top of the fog, the sky and bloom, at each quality; *AO GPU* is its passes
alone, by their own timestamps:

| Build | Scene, shot | Ambient occlusion | Frame | Scene CPU | Scene views GPU | AO GPU |
| --- | --- | --- | --- | --- | --- | --- |
| Debug | instanced, above | off | 10.45 ms | 6.89 ms | 2.19 ms | |
| Debug | instanced, above | Low | 9.39 ms | 5.85 ms | 2.15 ms | 0.08 ms |
| Debug | instanced, above | Medium | 9.72 ms | 6.45 ms | 2.02 ms | 0.09 ms |
| Debug | instanced, above | High | 10.42 ms | 6.32 ms | 2.48 ms | 0.12 ms |
| Debug | instanced, inside | off | 8.60 ms | 5.85 ms | 1.49 ms | |
| Debug | instanced, inside | Low | 9.50 ms | 6.68 ms | 1.59 ms | 0.08 ms |
| Debug | instanced, inside | Medium | 9.24 ms | 6.15 ms | 1.54 ms | 0.09 ms |
| Debug | instanced, inside | High | 9.39 ms | 6.30 ms | 1.68 ms | 0.12 ms |
| Debug | ground | off | 3.83 ms | 1.28 ms | 1.61 ms | |
| Debug | ground | Low | 4.08 ms | 1.38 ms | 1.69 ms | 0.08 ms |
| Debug | ground | Medium | 4.32 ms | 1.50 ms | 1.71 ms | 0.10 ms |
| Debug | ground | High | 4.05 ms | 1.42 ms | 1.71 ms | 0.12 ms |
| Release | instanced, above | off | 2.76 ms | 0.27 ms | 1.82 ms | |
| Release | instanced, above | Low | 2.85 ms | 0.29 ms | 1.92 ms | 0.07 ms |
| Release | instanced, above | Medium | 2.76 ms | 0.26 ms | 1.75 ms | 0.08 ms |
| Release | instanced, above | High | 2.84 ms | 0.25 ms | 1.95 ms | 0.10 ms |
| Release | instanced, inside | off | 2.28 ms | 0.26 ms | 1.35 ms | |
| Release | instanced, inside | Low | 2.31 ms | 0.25 ms | 1.52 ms | 0.07 ms |
| Release | instanced, inside | Medium | 2.53 ms | 0.40 ms | 1.50 ms | 0.08 ms |
| Release | instanced, inside | High | 2.27 ms | 0.24 ms | 1.38 ms | 0.10 ms |
| Release | ground | off | 2.31 ms | 0.09 ms | 1.62 ms | |
| Release | ground | Low | 2.59 ms | 0.12 ms | 1.84 ms | 0.08 ms |
| Release | ground | Medium | 2.58 ms | 0.11 ms | 1.86 ms | 0.10 ms |
| Release | ground | High | 2.55 ms | 0.11 ms | 1.78 ms | 0.12 ms |

What it says: the occlusion's passes take 0.07 to 0.12 ms of GPU time at 1280 × 720, Low to High,
whatever is drawn: they work on the depth at half size, so 50 000 instanced cubes, or 24 000
scattered ones, cost them no more than a plane. The scene views' totals move by more (up to
+0.24 ms), for the passes waiting on each other and the spread from one run to the next; the CPU
records five passes in about 0.02 ms in Release. The scene pass's second target costs nothing that
can be measured with the occlusion off (the same scenes on patch 02's build and this one, without
an environment: 1.68 against 1.70 ms on the ground scene, 1.20 against 1.20 ms inside the field).

Noise, from `NyxAmbientOcclusionGpuTests` (480 × 270): on the open floor the picture's mean step
between neighbouring pixels is 0.05 of 255 with the occlusion and 0.06 without: no noise. Down
among 20 000 thin blades drawn instanced, like grass, the
occlusion is 0.43, 0.34 and 0.31 (Low, Medium, High) and its mean step between neighbours 14.3,
13.3 and 12.0 of 255, against the lit picture's own 16.4, 15.9 and 15.3 from the blades' edges:
the blur evens out the noise, and what is left follows the blades. Its passes there: 0.024, 0.029
and 0.035 ms.

Colour grading, on top of the fog, the sky, bloom and ambient occlusion: `NyxGame --benchmark 10`,
Release, 1280 × 720, a hidden window, 4 samples a pixel, an RTX 3070 Ti, the atmosphere test scene from its camera
against the colour grading test scene (the same scene and camera, graded by its four keys), three
runs each, alternating; the averages of each run:

| Scene | Frame | GPU frame | GPU scene | GPU bloom | GPU picture | GPU grading |
| --- | --- | --- | --- | --- | --- | --- |
| atmosphere test, not graded | 1.93, 2.05, 2.32 ms | 1.32, 1.38, 1.47 ms | 0.39, 0.42, 0.53 ms | 0.14, 0.07, 0.05 ms | 0.015, 0.015, 0.015 ms | 0 |
| colour grading test, graded | 2.20, 1.96, 2.00 ms | 1.50, 1.33, 1.35 ms | 0.54, 0.39, 0.41 ms | 0.05, 0.11, 0.09 ms | 0.017, 0.018, 0.017 ms | 0 |

What it says: the lookup costs about 0.002 ms a frame, the picture pass's 0.015 ms against 0.017 to
0.018; the rest moves by more from one run to the next. The sun stands still, so the table is baked
once, before the recording, and the grading's own time is 0. A bake, timed by
`NyxColorGradingGpuTests` (the frame that bakes, 32 768 texels): 0.005 to 0.006 ms. A sun moving
with the time of day bakes in a few frames of every hundred (282 times as it rises 20 degrees over
20 000 frames, `NyxColorGradingTests`), so it averages well under 0.001 ms a frame. The CPU makes
the grade ready (a 3 × 3 matrix, a dozen values) once a view a frame, too little to show in
*extract* or *record*.

Light shafts, on top of the sky made by the air: `NyxLightShaftsGpuTests` at 1280 × 720 (a wall
with a window in it, the sun behind it), the median of 60 frames, a Debug build (the shaders are
compiled alike in both): their three passes 0.055 ms, all at half size (48 bilinear taps a pixel for
the gathering, four for the prefilter and the evening out); the picture pass 0.015 ms with them
against 0.017 ms without, the same within the spread: the tonemap's tap costs nothing that can be
measured. Well within the plan's millisecond for fog and shafts together. In the frames the sun
gives none (behind the camera, off the picture, set), nothing is recorded and they take nothing.

The high cloud layer, on top of the sky made by the air: 0.11 ms at 1280 × 720 and 0.20 ms at
1920 × 1080 with cloudlets, 0.15 and 0.26 to 0.28 ms with cloudlets and streaks blended ([its
cost](#its-cost)); without it, nothing is recorded and nothing changes.

The volumetric clouds, on top of the sky made by the air: 0.79 ms at 1280 × 720 and at 1920 × 1080
carried from frame to frame, 2.05 and 3.50 ms with every pixel marched each frame ([their
cost](#its-cost-2)); without them, nothing is recorded and nothing changes.

## Later

- Ambient occlusion: a temporal filter, once there is temporal antialiasing (fewer samples, less
  noise); bent normals for the sky's light; the depth's mips for far samples (XeGTAO's), so wide
  radii cost no more.

- The air's sky: aerial perspective (a froxel volume of the air between the camera and what it
  sees); a camera above the air; the sky's tables shared by the views of one world; moon and stars.
- Volumetric fog: shadowed shafts of light, local fog volumes; it takes the light shafts' place,
  which could stay for what lies beyond its range.
- Light shafts: a debug view of them alone.
- The cloud layer: cloud shadows (the plan's step 8); its haze by the aerial perspective; its
  ambient from the sky at its own height; the sky's cube made from the camera's own height near the
  layer; a second layer, or a weather map placing its types; half
  size with a depth-aware upsample, if a scene needs the time; skipping the pixels a mesh covers.
- The volumetric clouds: their shadows on the ground and in the fog; the light shafts masked by them
  in the air; their light in the sky's cube that lights the world; the camera flying into and
  through them; their haze by the aerial perspective; their sky light from the sky over each cloud.
- The fog over a point light's or a decal's own light: the fog adds the sun's and the sky's only.
- Colour grading: keys for the morning apart from the evening, once the time of day gives the
  sun's path; a view's switch to see its picture ungraded beside the graded one; the grade's
  controls as curves, if a look needs more than lift, gamma and gain.
