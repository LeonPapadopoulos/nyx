# Lightning

Bolts of light between the sky and the ground: the short, flickering strike of a storm, and a
sustained column that stands for seconds, a braid of filaments boiling round one axis. Both glow
past the bloom's threshold, light the world round them, light the clouds they come out of from
within, and part and pile up those clouds.

Lightning owns no weather. A storm (the weather system to come) schedules strikes by placing
lightning entities and setting their start times and seeds, as it will drive the wind, the clouds
and the light; nothing here depends on it. Nothing here is a game's either: a game can use the
column as an effect of its own by the same component.

Where it is:
- the settings: `LightningComponent` (`Scene`), with `ELightningMode`;
- the plain math, no GPU: `Engine/Source/Runtime/Render/include/Lightning.h` (when a strike or
  column is on, how bright, its paths, its strips as a view draws them, its lights, its cloud
  lights and channel);
- the renderer's part: `VulkanRenderer::ExtractLightning()`, `CullSceneViewLightning()`,
  `VulkanLightning` and `Engine/Shaders/Lightning.slang` (the strips), the clouds' lightning pass in
  `VulkanVolumetricClouds` and `VolumetricClouds.slang` (step 2), `CloudModifierComponent` and the
  clouds' density in `nyx.volumetricclouds` (step 3).

The work comes in four steps, one patch each: the bolt; the clouds lit by it; cloud modifiers; a
test scene.

## What it should look like

Two references set the look.

- **A strike over a town at dusk** (a painted still): one jagged, near vertical bolt from the
  clouds to the roofs, white-yellow and thin, glowing. The cloud undersides round it glow yellow
  from within, brightest by the bolt and falling off over a few hundred metres; behind it the
  clouds part in a channel and pile up round it. The whole picture is graded yellow-green.
- **A sustained column** (an animated clip, about 3 s): a braid of 3 to 6 orange-yellow filaments
  (a white core, an orange body, red edges) twisting round one vertical axis from the sky to the
  ground. The filaments take a new shape about every second frame, about 12 times a second (drawn
  "on twos"), so it boils rather than flickers; they vary in thickness, with blobby knots at their
  kinks. Now and then a side arc whips outward, or a lone bolt strikes beside it. Embers swirl
  round it. At its base a billowing cloud of dust or steam rolls outward, lit white-yellow from
  within, over a large glow on the ground like a half dome of light. Dark smoke stands behind it.

## The component

A *Lightning* component (`LightningComponent`, reflected, with undo, saved in scenes, sent to a
running game) on any entity: the entity's position is where its bolts reach the ground, its
rotation and scale change nothing. Every value is in data and can be tweaked; the categories:

- **Timing**: *Lightning* (on or off), *Mode* (*Strike* or *Sustained*), *Start Time* (the world's
  time of the first, in seconds), *Repeat*, *Interval* and *Interval Jitter* (again every so many
  seconds on average, each start moved at random by up to the jitter's part of the interval),
  *Duration* (a column's; 0: for as long as it is on), *Seed*.
- **Shape**: *Height* (from the ground to where the bolt starts, in the clouds), *Strike Radius*
  (each strike lands anywhere within it, so one entity strikes all over a town), *Top Spread*
  (how far from above its landing point a strike starts), *Jaggedness*, *Detail* (how many times
  each stretch is split), *Branch Chance*, *Branch Length*, *Branch Depth*.
- **Strike**: *Leader Time* (the leader's way down), *Min Strokes* and *Max Strokes* (the return
  strokes down the same channel), *Stroke Interval*, *Stroke Decay* (how fast each flash dies),
  *Fade Time* (the channel's afterglow).
- **Column**: *Reform Rate* (new shapes a second), *Min Filaments* and *Max Filaments*, *Column
  Radius*, *Twist*, *Column Kinks* and *Column Jaggedness*, *Joins*, *Filament Width* and *Width
  Variation*, *Knots* and *Knot Size*, *Glow Height* and *Glow Patchiness*, *Side Arc Rate* and *Side
  Arc Length*, *Stray Bolt Rate* and *Stray Bolt Distance*.
- **Look**: *Core Color*, *Body Color*, *Glow Color*, *Brightness* (the core's, in the units of the
  sun's light: past white), *Body Brightness* and *Glow Strength* (against the core's), *Width*,
  *Core Width*, *Glow Width*, *Sharpness*, *Min Pixel Width*, *Branch Brightness*, *Leader
  Brightness*, *Cloud Hiding* (step 2).
- **Light**: *Lights*, *Light Count*, *Light Color*, *Light Intensity*, *Light Range*: point lights
  along the bolt's lower part.
- **Cloud Light** (step 2): *Cloud Light*, *Cloud Light Color*, *Cloud Light Intensity*, *Cloud
  Light Reach*.
- **Channel** (step 3): *Channel*, *Channel Radius*, *Channel Clear*, *Channel Pile Up*, *Channel
  Swirl*, *Channel Open*, *Channel Linger*.

The world's other components don't change: a scene without lightning is what it was, to the bit.

## Step 1: the bolt

### Everything from the world's time

A bolt has no state. What it shows at a moment is worked out from its component, its entity's
position and the world's time alone (`EvaluateLightning()`): which strike or column is on, how far
into it, its shape, how bright. So:

- it is the same every time, to the bit: the same seed and time give the same bolt on every
  machine, in the editor and the game, in a test;
- it pauses with a game's clock, moves on by one step with *Step*, and plays in the editor's scene
  views as it is edited (`RenderWorld::Time`, as the particles and the clouds use); a scrubbed or
  replayed time shows what that time shows, with nothing to reset;
- nothing in the world feels it (no physics, nothing written to the registry).

**When.** Time from *Start Time* on is cut into slots of one *Interval*; the bolt of slot `k` starts
at `Start + k · Interval + jitter_k`, `jitter_k` from the seed and `k`, within *Interval Jitter* of
the slot, never so late that it would still be on when the next one starts. Finding the bolt on at
a time is a division and a hash, however long the world has run; two bolts of one entity never
overlap. Without *Repeat*, only slot 0. A game strikes now by setting *Start Time* to its world's
time (and a new *Seed*), which is also how a storm will schedule them.

**Each bolt its own randomness**: a PCG stream from the seed and the slot (and for a column the
reform frame), so a strike's shape doesn't depend on anything before it.

### A strike

Real lightning comes down as a faint stepped leader, then the return stroke lights the channel it
made, then a few more strokes come down the same channel, then it fades. The component keeps that
order and makes it slow enough to see:

1. **The leader** (*Leader Time*, 0.12 s): the path is revealed from the top to its tip, the
   branches with it, faint (*Leader Brightness*), its tip brighter.
2. **The return stroke**: when the leader reaches the ground, the whole channel at full brightness,
   the branches at *Branch Brightness*; it dies away as `exp(-t / Stroke Decay)`.
3. **More strokes**: between *Min Strokes* and *Max Strokes* in all, *Stroke Interval* apart (from
   half to one and a half of it at random), each down the main channel only (no branches: a dart
   leader doesn't branch), each a little dimmer.
4. **Fade**: after the last stroke the channel glows on and fades out over *Fade Time*.

The brightness is the sum of the strokes' decaying flashes and the afterglow, so between strokes
the channel dims and flashes again: it flickers by itself.

### A column

From *Start Time* for *Duration* (fading in over 0.15 s and out over 0.3 s), re-formed *Reform
Rate* times a second (12: "on twos" at 24 frames a second). Each reform frame, from its own seed:

- **Filaments**: between *Min Filaments* and *Max Filaments* polylines from the ground to *Height*,
  through 18 control points a filament, closer together near the ground (the square of the way up:
  strokes a few metres long at the foot, tens of metres higher), so it is drawn in big, angular
  strokes, as the footage's are, not a fine wiggle. Each point's angle round the axis turns by
  *Twist* turns a hundred metres up and is thrown up to 50 degrees either way; its distance from the
  axis is from 0.4 to 1 of *Column Radius* (the braid is wide, about a quarter of its visible height
  in the footage), drawn in towards the foot. *Column Kinks* splits each stroke again (1 by default)
  by *Column Jaggedness*. At *Joins* of the levels, some of the filaments meet at one point and split
  apart again after, so they make open loops between the joins, with a fat hot blob where they meet.
  A filament is *Filament Width* times the *Width* thick, going from thick to thin along it by *Width
  Variation*; at a few kinks (*Knots*) another blob. (A blob is a round sprite of its own: as one wide
  point of the strip it would be cut flat by the stretches on either side of it.)
- **Its glow** is strong low down and patchy higher up: each point's share of the glow halves every
  *Glow Height* up (60 m) and comes and goes along a filament by *Glow Patchiness*, so the column has
  no even sleeve of glow up its whole length.
- **Side arcs** (*Side Arc Rate* a second on average): a short forked path whipping outward from a
  filament, *Side Arc Length* long, living two reform frames, longer in the second.
- **Stray bolts** (*Stray Bolt Rate* a second): a strike of its own, short (one or two strokes),
  from the sky to the ground at about *Stray Bolt Distance* from the axis.
- **Brightness**: each reform frame a little brighter or dimmer at random (75 to 100 %), so it
  boils rather than holding still between shapes.

Between reform frames the shape holds: the column changes 12 times a second whatever the frame rate.

### Paths: midpoint displacement with forks

`MakeLightningPath()` starts from a straight line from the top to the bottom and splits every
stretch *Detail* times: each split moves the middle at random across the stretch (a random
direction at right angles to it) by *Jaggedness* times its length, so the bolt is jagged at every
scale and stays within a band round the line, its ends exactly where they were. At each new middle
of the first splits, with *Branch Chance* (halving at each depth), a branch starts: from that
middle, heading down and away (20 to 50 degrees off the stretch), *Branch Length* of what is left
of the stretch below, itself split as many times as are left, up to *Branch Depth* deep. A budget
of points and paths bounds it however the values are set (`MaxLightningPoints`,
`MaxLightningPaths`).

Each point keeps its **arrival**: how far down the leader has come when it gets there, as a part of
the main channel's length (a branch's from where it starts), so the leader reveals the main channel
and its branches at one speed. Widths taper along a branch.

Paths are made again each frame (a few hundred points: microseconds), not kept.

### Drawn as strips

Each path is drawn as a strip facing the camera (`BuildLightningStrips()`, on the CPU, per view):
at each point the strip's side is at right angles to the path and to the way to the camera, mitred
at the joins (the mitre held to twice the width at sharp kinks), its two edges at plus and minus the
glow's half width, its two ends pushed out by that half width. Each quad's vertices carry the
stretch of path it draws (its ends, the half width and brightness at each), and each pixel measures
its own distance to that stretch, to the nearer end past them: so the strip is round at its joins
and its ends, whatever shape the mitred quads take (an across coordinate interpolated over a skewed
quad smears along its diagonal instead). Across it, the fragment shader (`Lightning.slang`) draws:

- the **core**: *Core Color* times *Brightness*, narrow (*Core Width* of the *Width*), the only part
  past white;
- the **body**: *Body Color* times *Body Brightness*, as wide as the *Width*;
- the **glow**: *Glow Color* times *Glow Strength*, falling off softly to nothing at *Glow Width*
  times the *Width*, times the stretch's share of the glow.

The core and the body are soft peaks, or as hard-edged as *Sharpness* says (0.85 by default): a
step at their edges a pixel wide, as a drawn line is inked, the glow soft outside it.

**The look, after the references.** The defaults are the footage's: an orange body (1, 0.5, 0.08),
red-orange edges (0.9, 0.15, 0.03), a thin hot core (1, 0.95, 0.8), a warm light. The levels are
set so that the body stays just under white after the exposure (4 times 0.2: 0.8), where the tonemap
keeps its hue, and only the core clips to white: a core ten times past white bloomed over the narrow
body round it and washed every stroke out to cream. `NyxLightningGpuTests --look` renders a column
and a strike framed as the references are (a column 70 m off, the camera low, the ground at four
fifths of the height; a strike 400 m off with its foot there too) for comparing them side by side.

The bloom spreads the rest (the bloom is the glow far from the bolt). Knots are round
sprites: a square facing the camera whose stretch's two ends are its middle.

**Overlaps.** A jagged path's stretches are short against a wide glow, so at sharp kinks the mitred
quads fold over each other, and paths cross (a column's filaments, a branch where it leaves its
parent). Added straight onto the scene, the overlaps would count their light twice: bright hairs at
every kink. So the strips are drawn into the view's own lightning picture first (RGBA16F, its size,
cleared), blended by their maximum, each pixel the brightest of the strips over it, then that
picture is added to the scene in one triangle over it. The union of round strokes, as a painter's
stroke is, never brighter where two cross; it costs a picture of the view's size (16 MiB at
1920 × 1080, made the first frame a view draws a bolt) and one pass over it.

A bolt kilometres away would be thinner than a pixel and flicker in and out as it moves: the strip
is widened to *Min Pixel Width* pixels for the core at its distance (the view's angle a pixel), its
brightness lowered by the square root of the widening, so far bolts stay thin, steady and bright.

**Their passes**: `VulkanLightning`, per view, after the ambient occlusion and before the particles
(so smoke in front of a column covers it, and embers add over it), with no depth buffer: like the
particles, each pixel tests itself against the scene's depth, which it reads, and fades out over a
short distance in front of what is behind it. Through the fog's transmittance, and through the
clouds before it (`nyx::cloudCoverBefore()`), so a bolt's top inside the clouds is hidden and the
cloud's own glow (step 2) shows instead. One stream of vertices a frame for every view
(`VulkanLightning::Upload()`), one draw a bolt into the lightning picture, its colours and widths in
the draw's push constants, then the picture added. With MSAA, after the resolve, as the particles
are.

**Culled per view** by each bolt's box (its paths widened by the glow) against the view's frustum's
sides; `SceneViewCullingStats::Lightning`, `LightningCulled`.

**Past the far plane.** Storms are kilometres off, and a camera's far plane is often 1 km (the
default). A bolt is a thing of the sky, so it isn't cut off there: it is culled by the frustum's
sides alone, its depth is held at the far plane in the vertex shader, and a pixel over the sky (the
scene's depth at the far plane) counts as infinitely far, so the bolt shows in front of it. Over a
surface, the test is by the bolt's own distance along the view, not its held depth.

**Into the clouds.** The clouds before a bolt dim it by their transmittance (`cloudCoverBefore()`)
raised to *Cloud Hiding* (4): the light of a line 40 times past white that gets through a cloud is
mostly scattered off the line, so the line should go into the cloud and the cloud's glow (step 2)
take over, not shine through it white as a transmittance of 0.1 would leave it.

### It lights the world

*Light Count* point lights (`RenderPointLight`) along the main channel's lowest stretch, the lowest
at the ground, of *Light Color* times *Light Intensity* times the bolt's brightness, shared between
them, each of *Light Range*. They go into the world's point lights after the entities' own
(`ExtractLightning()`), so every lit surface, decal and lit particle takes them through the same
list, culled per view by their range like any other (Lights and particles). They have no icon and
can't be picked; their entity is the lightning's. While no bolt is on, there are none.

*Light Intensity* is in the point lights' units: what a white matte surface 1 m away shows. A
strike lighting a street 50 m away as a dusk sun does needs about 0.5 · 50², 1 250; the default is
4 000 for a bright flash.

### In the editor

Each lightning entity has an icon, a small zig-zag of lines at its position, which a click selects.
A selected one shows its *Strike Radius* as a circle on the ground and a line up to its *Height*.

## Step 2: the clouds lit by it

The volumetric clouds take the bolt's light as a **line light**: light scattered towards the camera
by the cloud round the bolt, dimmed by cloud along a short path towards the bolt, only within its
reach.

### What lights them

Each bolt on gives the view a **cloud light** (`LightningCloudLight`): the main channel's top part
as a polyline of 4 points (from its top to below the cloud's base), its colour times its
brightness (*Cloud Light Color*, *Cloud Light Intensity*) and its reach (*Cloud Light Reach*,
metres). At most 4 a view (`MaxLightningCloudLights`), the brightest kept; the view's constants
carry them (camera-relative, in kilometres, as the march works).

At a point of cloud, a cloud light gives:

`L = color · I / (1 + d / d0) · (1 - (d / reach)²)² · through(τ)`

`d` the distance to the nearest point of its polyline, `d0` 50 m: a line's light falls off as `1 / d`
(a point's as `1 / d²`), softened near it and windowed to nothing at the reach, so the reach is
where its cost ends. `τ` is the cloud's optical depth along a short path from the point towards the
bolt (two samples of the shapes, up to 300 m), and `through(τ) = 0.7 e^(-τ) + 0.3 e^(-τ / 4)`: the
light that comes straight through and, softer, the light scattered on through the cloud, so the
glow carries into a cloud's body and its far side stays dark.

### Why a pass of its own

The clouds' march keeps one pixel of each 4 x 4 (or 2 x 2) a frame and carries the rest from the
frames before. A flash lasts 50 ms, three frames: marched into that picture, a pixel would show the
flash in the one frame of 16 it is marched in and keep it for the next 15, and a carried pixel
would show the flash after it ended. Two ways round it:

1. **March every pixel near the bolt** in the march itself, while one is on and for a while after,
   so the carried picture holds no stale flash: the march would march a rectangle of pixels round
   the bolt every frame, the whole march (sun, sky, details) at every pixel of it, about the cost of
   *Every Pixel* over that part of the picture, and the frames after a flash would still have to
   march it again to take the flash out.
2. **A separate pass for the bolt's light alone**, every pixel near it, every frame, never carried:
   the clouds' own picture stays as it was, and the bolt's light is added on top where the clouds
   are read.

Step 2 takes the second: the march and its history never see the bolt, so nothing can smear, the
frame after a flash is exactly the picture without it, and the pass costs only the bolt's part:
the samples within its reach, no sun, no sky. Measured (Numbers): 0.24 ms at 1920 × 1080 for a
column whose reach covers half the clouds' picture, where marching every pixel of that half would
cost about 3 ms (half of the 6 ms of marching them all), and every frame after the flash again until
the 4 x 4 picture had been marched anew; about 1 ms where a bolt's reach covers the whole view of a
dense storm.

**The lightning pass** (`volumetricCloudsLightningFragment` in `VolumetricClouds.slang`) runs at the
size the clouds are marched at, after their resolve, over a rectangle of the picture round the
cloud lights' reach (their boxes projected; the whole picture if a corner is behind the camera),
into a picture of its own (RGBA16F, cleared). Each pixel:

1. finds where its ray is within the reach of any cloud light and inside the band, up to the
   march's farthest distance;
2. takes the clouds before that stretch, coarsely (8 samples of the shapes alone), for what of its
   light gets back to the camera;
3. marches the stretch as the clouds' own march does: 32 coarse steps probing the shapes alone,
   and where one finds cloud, it and the step before again in 2 fine samples each, with the details
   and each cloud light's light at them (one sample of the shapes half way towards the bolt for the
   cloud between), scattered towards the camera as the march scatters the sun's
   (energy-conserving, *Brightness*);
4. fades it into the air between as the clouds' own light fades (*Aerial Perspective*).

Its samples sit at their steps' middles, the same for every pixel. A dither from pixel to pixel
(the first version had one, interleaved gradient noise over 24 even steps) isn't evened out by
anything here, as the carried picture evens out the march's: it showed as a hatched pattern over
the lit cloud.

`nyx::volumetricCloudsAt()` adds this picture's light, filtered up the same way, to the clouds'
light, so the sky, the surfaces among and beyond the clouds and the light shafts all see it. While
no cloud light is on, the pass doesn't run and nothing reads it: every picture is the same to the
bit as without lightning.

Its GPU time is in the frame statistics: `SceneViewGpuTimes::CloudLightning`,
`EFrameStat::GpuCloudLightning` (CSV `gpu_cloud_lightning_ms`, after `gpu_volumetric_clouds_ms`).

## Step 3: cloud modifiers

A *Cloud Modifier* component (`CloudModifierComponent`, `Scene`) holds shapes (`CloudModifier`) that
change the volumetric clouds' density where they are, round its entity's position and the up axis,
each animated by the world's time:

- **Clear Column**: a vertical column, *Radius* wide with a soft edge *Width* wide, where the clouds
  thin out by *Strength* (1: none left in its middle);
- **Thicken Ring**: a ring round the axis, *Radius* to its middle, *Width* wide, where they grow
  denser and cover more by *Strength* (0 to 4), as if pushed aside and piled up;
- **Swirl**: the clouds turned round the axis by an angle that grows with the time (*Strength* turns
  a minute at the *Radius*), as 1 / the distance squared beyond it: a vortex, the clouds near the
  axis wound into a spiral the longer it goes on, unwinding as it fades.

Each has *Bottom* and *Top* (metres above the world's height 0; the whole band when *Top* isn't above
*Bottom*, soft over half its *Width* at its ends), a *Start Time*, a *Duration* (0: for ever), *Fade
In* and *Fade Out* (`GetCloudModifierFade()`), so a channel can open over a second and close over ten.
A selected modifier's shapes are drawn as circles in the scene views.

**How they change the clouds.** `nyx::volumetricCloudDensity()` asks what the shapes do at every
density sample (`cloudModifiersAt()`, the same math as `GetCloudModifierEffect()` on the CPU, which
the tests check): the columns' clearing multiplies the coverage and the density (*Keep*), the rings'
thickening adds to the coverage (half of it) and multiplies the density by 1 + it (*Add*), so a ring
grows real cloud from the weather's own shapes rather than a solid tube; the swirls turn where the
shapes and details are looked up. Every sample goes through it: the march, its samples towards the
sun, the sky's occlusion, the lightning pass. Without a shape, nothing changes: every picture is the
same to the bit.

**Per view, a few.** The world's shapes are worked out each frame (`ExtractCloudModifiers()`, then the
lightning's channels, `RenderWorld::CloudModifiers`); a view takes the 8 whose edge is nearest its
camera (`PickViewCloudModifiers()`, `MaxCloudModifiers`), 48 bytes each in its constants
(`ViewConstants.CloudModifiers`, camera-relative, in kilometres).

**Per ray, fewer.** A loop over every shape at every density sample cost as much as the march itself:
8 shapes 50 km away, changing nothing, took the march from 5.4 to 11 ms at 1920 × 1080. So each ray
works out once which shapes its samples can come near (`volumetricCloudModifierMask()`: the ray's
stretch over the ground against each shape's reach, widened by twice the light's reach for the
samples towards the sun), and the samples go through those alone (a bit each, `firstbitlow()`): the
same far shapes then cost nothing that can be measured.

**Lightning uses them** (*Channel*): for each strike that started within *Channel Linger* (and each
column standing, or ended within it), a *Clear Column* of *Channel Radius* round where the bolt starts
(a column's axis) with *Channel Clear*, and a *Thicken Ring* twice as far out with *Channel Pile Up*,
opening over *Channel Open* and closing over the second half of the linger; round a column a
*Swirl* too (*Channel Swirl*). Worked out from the world's time like the bolt: the strikes of the
last few slots, each one's place from its seed (`GetLightningStrikePlace()`,
`AddLightningChannelModifiers()`), nothing kept. *Channel* off, none.

**With the carried picture**: a modifier changes the clouds slowly (over seconds): the carried
pixels catch up within 16 frames (a quarter of a second at 60 frames a second), held to what this
frame's marched pixels round them span, so an opening channel's edge is soft for those frames, not
smeared. A swirl moves the clouds near its axis all the time; the carried picture follows the wind,
not the swirl, so close to a fast swirl's axis *Every Pixel* is the sharper choice. The lightning's
light, which changes within a frame, is the part that needs the pass of its own.

## Step 4: a test scene

`Assets/Scenes/LightningTest.nyxscene`, written by `NyxLightningGpuTests --write-scene` like the
other test scenes: gentle hills (the generated ground, nothing built on it) at dusk under an
overcast of volumetric clouds, their base at 500 m; a lightning entity striking somewhere within
300 m of a point 900 m ahead every 4 seconds or so, and a sustained column 1 km ahead off to the
right standing 4 seconds in every 8, both lighting the clouds round them from within and parting
them; the scene's camera on the ground framing both. Its test: it loads, and a strike's flash
brightens its clouds.

## Tests

- `NyxLightningTests` (CPU): the schedule (slots, jitter, never overlapping, *Repeat* off, a late
  time as exact as an early one); a strike's brightness (the leader, the strokes, the afterglow, 0
  after); a column's reform frames at its rate, its fade in and out; paths (ends fixed, every point
  within the band jaggedness allows, branches heading down, budgets, the same seed the same path to
  the bit, another seed another); the leader revealing by arrival; strips facing the camera, mitres
  held, widened to the pixel width far off; the lights along the channel, none while off; the cloud
  lights' polylines and rectangles (step 2); the modifiers' density on the CPU twin (step 3).
- `NyxLightningGpuTests`: no lightning, the same picture to the bit as before; a bolt draws bright
  pixels along it, blooms, is hidden behind a wall and in the clouds; it lights the ground under it;
  the same time the same picture; the clouds lit near a bolt and not far from it, the frame after a
  flash the same as without one in every update mode, a still picture in 4 x 4 close to every pixel
  marched during a flash (step 2); a modifier clears a column and thickens a ring (step 3); the
  test scene (step 4); GPU times at 1920 x 1080.

## Numbers

### The bolt

At 1920 × 1080 on an RTX 3070 Ti, Debug, the medians of 60 frames (`NyxLightningGpuTests`): the
bolt's passes (both: the strips into the picture and the picture onto the scene), and the CPU's time
to work out the bolt and build one view's strips.

| Bolt | Vertices | GPU | CPU (Debug) |
| --- | --- | --- | --- |
| A strike at its first flash, 80 m away | 1 548 | 0.03 ms | 0.6 ms |
| A 400 m column, 60 m away, filling the middle of the picture | 1 470 | 0.05 ms | 0.42 ms |

What it says:

- The GPU's part is small: a few thousand triangles and one pass over the picture.
- The CPU's part is a Debug build's: glm's operators called rather than inlined; a Release build
  calls none of them. (The first column, a fine wiggle of 12 450 vertices, took 3.5 ms; its big
  strokes take a tenth of the points.) Were columns many, the strips could be built in a vertex
  shader from the points (each vertex finding its stretch's ends by its index), leaving the CPU the
  points alone.

### Cloud modifiers

The cloudy test scene at 1920 × 1080, the clouds marching every pixel (the dearest, where a cost
shows most), Debug, the medians of 30 frames:

| Shapes | Their march |
| --- | --- |
| None | 5.6 ms |
| 8 shapes 50 km away, changing nothing | 5.6 ms |
| A column's channel (a cleared column and a ring, 1.4 km and 2.8 km across) over most of the view | 7.0 ms |
| (Before the per-ray mask: the 8 far shapes) | (11 ms) |

What it says: shapes away from a pixel cost it nothing; near it, their own math and the cloud they
add (the ring grows more cloud to march through, with its light) cost about a quarter of the march
here, a view filled by a channel. One pixel of each 4 x 4 a frame pays a sixteenth of that a frame.

### The clouds lit by it

The cloudy test scene (`NyxLightningGpuTests`: an overcast at dusk, its base at 400 m, a column
2.5 km away with its top in it, the camera on the ground looking up at it), 1920 × 1080, the clouds
at half size, Debug, the medians of 60 frames:

| Pass | GPU |
| --- | --- |
| The clouds' lightning pass, over 54 % of their picture (518 x 540 of 960 x 540) | 0.24 ms |
| Their march, one pixel of each 4 x 4 a frame | 1.6 ms |
| Their march, every pixel | 6.0 ms |

What it says:

- The lightning pass is cheap for what it does: 32 probes of the shapes and a few fine samples in
  cloud, at every pixel of the rectangle; in an overcast most rays are opaque after a few samples,
  and the rest of the picture is only cleared. (The first version, 24 even steps with a dither,
  took 0.10 ms here and showed its pattern; 3 fine samples a step and 2 towards the bolt took
  0.38 ms.)
- Marching every pixel near the bolt instead would have cost about 3 ms in the flash's frames
  and in the frames after it, against 0.24 ms.
- Its time is in the frame statistics (`gpu_cloud_lightning_ms`); 0 in frames without a bolt.

## Later

- Cloud-to-cloud lightning: a bolt that stays in the band, lighting the clouds alone.
- The sky's own flash: the sky's light and the ambient lifted for a moment over the whole picture.
- Thunder, by the bolt's distance (a game's audio).
- Lightning from a storm's own schedule (the weather system), and strikes landing on what is there
  (a game's ray cast, giving the landing point).
- Bolts casting shadows (a point light's shadow, Lights and particles' Later).
