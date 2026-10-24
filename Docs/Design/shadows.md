# Shadows

How the primary directional light casts shadows, and why it is done this way. The code is in
`Engine/Source/Runtime/Render` (`ShadowCascades.h`, `VulkanShadows`,
`VulkanRenderer::PrepareSceneViewShadows()` and `DrawShadowCascades()`), the shaders in
`Engine/Shaders/Shadow.slang` (the casters) and `Engine/Shaders/Library/nyx/shadows.slang` (what a
surface looks up), the settings on `DirectionalLightComponent`.

## The shape of it

Cascaded shadow maps: the camera's view, from its near plane out to the shadow distance, is cut
into up to four slices, nearer ones smaller; each slice gets a square shadow map of its own, the
light's depth of what casts into it. A surface finds its slice by how far it is along the camera's
view, and compares its depth with the map's texels around it.

Each frame, per scene view, after the worlds are extracted and the view culled:

1. **Extraction** keeps, per world, the box around every object that casts
   (`RenderWorld::CasterBounds`): its Mesh Renderer's *Cast Shadows*
   (`MeshRendererComponent::bCastShadows`) and at least one of its sections' materials' (the
   `CastShadows` option, `Material::bCastShadows`), both on by default. See
   [What casts](#what-casts).
2. **The light** is the world's first primary `DirectionalLightComponent`, whichever camera the view
   has: the editor's free camera too, so the editor shows the scene lit and shadowed as the game
   does (`ExtractSceneLight()`). Without one, the default sun casts nothing. Its settings are
   brought into range there (`SceneViewShadowSettings`).
3. **The cascades** (`PrepareSceneViewShadows()`, plain math in `ShadowCascades.h`): the splits
   (`ComputeCascadeSplits()`), a sphere round each slice (`FitCascadeSphere()`), the light's
   orthographic view of each sphere snapped to its texels (`MakeShadowCascade()`), reaching back
   towards the light as far as the casters do (`GetCasterReach()`).
4. **Culling**: each cascade culls with the same code as the view's camera (`CullDraws()`,
   `SceneCulling.h`) against its own frustum, of the objects that cast only: entities by their
   bounds, instanced ones cell by cell, entities that draw alike batched into the frame's instance
   stream. So a cascade a few metres across draws only what is near it.
5. **Drawing**: just before the view's scene pass, each cascade is drawn into its layer of the shadow
   map (`DrawShadowCascades()`), depth only (`Shadow.slang`'s `shadowVertex`), each section with the
   faces its material draws.
6. **Lighting**: the view's set (`ViewConstants`) holds each cascade's light view and projection,
   where it ends, the size of its texels, the map, and the light's settings. `nyx::sunShadow()`
   gives the sun's share at a point, which `lightSurface()` multiplies the sun's light by; the sky's
   light isn't shadowed.

## Stable cascades

A shadow map that moves with the camera by fractions of a texel makes every shadow edge crawl as
the camera moves; one that changes size as it turns makes them swim. Two things keep a still
scene's shadows still:

- **A sphere, not a box, round each slice.** `FitCascadeSphere()` is the smallest sphere round the
  slice's eight corners, worked out on the camera's axis (`c = (n + f)(1 + k²) / 2`, clamped to the
  far plane, `k` the corners' spread a metre out). It depends on the field of view, the aspect
  ratio and the split distances only, so its radius, and with it the cascade's size and its texels'
  size in the world, never changes as the camera moves or turns. It costs some resolution against a
  box fitted tightly each frame: the price of no swimming.
- **Snapped to texels.** The light's view is turned only, never moved (`MakeLightView()`, the same
  for every cascade and frame while the light keeps its direction), and the sphere's centre is
  rounded to whole texels across it. So the cascade moves over the world in whole texels, and a
  still point stays where it was within its texel. `NyxShadowCascadeTests` walks and turns a camera
  over 40 steps and checks that a still point's place in its texel doesn't drift.

The radius is rounded up to 1/64 m, so rounding in the matrices can't make it differ frame to frame.

## Depth

- **Reversed**, as the scene's (`NearPlaneDepth` is 1): the orthographic projection is made with its
  near and far planes swapped, the map cleared to 0, nearer the light greater, `DepthTestNearer`. Y is
  turned for Vulkan as the camera's projection turns it, so the light sees the front faces
  counter-clockwise, as the camera does. All of clip Y turns, its offset with its scale: a
  cascade's box is off the light view's axis (the light's view is turned only, about the world's
  origin), and turning the scale alone, as the camera's centred projection may, mirrors the box
  about the axis. The map then covered another square than the sphere, the further from the origin
  the further off, and the view's points beyond it read its edge: shadows over sunlit ground that
  moved with the camera. `NyxShadowCascadeTests` checks cascades centred on spheres far from the
  origin, `NyxShadowGpuTests` a large ground's shadows from cameras all round.
- **Reaching back to the casters**: a caster between the light and the slice's sphere, a tree
  beside the slice or a hill behind it, must still be drawn into it. The cascade's near plane is as
  far towards the light as the corner of the world's caster bounds that reaches furthest, or the
  sphere's near side if that is further. Without depth clamping (a device feature Nyx doesn't ask
  for), that is what keeps them; float depth keeps a 500 m reach precise to well under a millimetre.

## One map for every view

The map is one depth texture array (`D32_SFLOAT`), a layer per cascade, owned by `VulkanShadows`,
in the bindless array. Every view's cascades are drawn into it in turn, each just before the view's
scene pass reads it: the editor's scene views and its material preview don't each hold four
layers of up to 4096² (64 MB at 2048², 256 MB at 4096²). With one frame in flight and the views
drawn one after the other, the render pass's dependencies are enough: its layers are drawn after
the last view's scene pass has read them, and read after they are drawn.

- **Resolution**: each view draws as many texels as its light asks for, from the layer's corner;
  the map is as big as the largest any view asks for this frame, and made again (waiting for the
  GPU) when that changes. A surface reads only the texels of the part its view drew.
- **The cost** is that the map holds only the last view's cascades once the frame is drawn, so the
  debug view draws the maps into each view's own picture, right after its scene pass, rather than
  into a window of their own.

## Filtering and biases

- **PCF**: the texels out to *Softness* (0 to 3) on each side of the point, each tap bilinear over
  four texels, as a comparison sampler's linear filter weighs them: 1 tap at 0, 9 at the default 1,
  49 at 3. The texels are loaded (`Load()`), not gathered: Slang's gather from the bindless heap
  asked for the `ImageGatherExtended` capability, which Nyx's device doesn't turn on, and a
  comparison sampler would be a fifth fixed sampler, and a depth image in the heap, for one use.
- **Biases**, both in texels of the point's cascade, so they are as right in the far cascades as in
  the near one: *Depth Bias* moves the point towards the light; *Normal Bias* moves it out along its
  surface's normal (the geometry's, not a normal map's), the more the surface is turned from the
  light and the further the filter reaches. A surface the light grazes would otherwise find itself
  in the texels around it: before the scaling, the editor's default scene showed faint stripes of
  acne on the cubes' sides turned from the sun, two levels deep; with it, the picture is the same
  to the bit as before shadows. Surfaces turned away from the light skip the lookup: they get none
  of its light anyway.
- **Fading**: shadows fade out over the last tenth of the shadow distance, so they don't end at a
  line.

## Faces

The shadow pass draws the faces the scene pass draws (`SetMeshFaces()`): a one-sided material's
front faces, as the light sees them, a two-sided material's both. So a leaf facing away from the
sun still casts, and a closed mesh's back faces cost nothing. A mirrored entity winds the other way
round there too.

## What casts

Two switches, both on by default, and a mesh section casts only with both on: its entity's
*Cast Shadows* on the Mesh Renderer, and its material's (`CastShadows = false` in the `.nyxmat`,
*Cast Shadows* under *Material* in the material editor, inherited by instances like any option).
The entity's switch is for one thing; the material's for everything drawn with it at once, such as
thousands of small instanced plants scattered over many entities, from one place.

- **Where it is decided.** Extraction makes an entity a caster (`RenderObject::bCastShadows`) only
  if its Mesh Renderer casts and one of its sections' materials does. An entity that isn't one is
  skipped by every cascade's `CullDraws()` before its bounds are looked at: no cell of an instanced
  one is culled, no draw is made, nothing is recorded, and it doesn't stretch `CasterBounds`, so
  the cascades don't reach back for it either. A field of non-casting instances costs the shadow
  passes nothing.
- **Mixed entities.** Casting is decided per entity, materials per section. An entity with both
  kinds, a trunk that casts beside leaves that don't, is a caster: it is culled as a whole, and
  `DrawShadowCascades()` leaves out the sections whose materials cast nothing. Its bounds stay the
  whole mesh's, a little larger than its casting sections need.
- **Live.** The option is read into the material's handle (`Material::bCastShadows`), as
  `bTwoSided` is, whenever the material is set from the editor or its file is saved, and the
  extraction reads the handles every frame: the next frame's cascades follow, the scene untouched.
  A material that draws as the missing one casts.
- Either way, what doesn't cast is still drawn and still receives shadows.

## Seeing it

- *Debug > Shadow Cascades* in the editor, for every scene view; F5 in a Debug build of the game;
  `IRenderer::SetSceneViewShadowDebug()`. The picture is tinted by the cascade each surface takes
  its shadow from (red, green, blue, yellow, nearest first: a cascade's tint on a coloured material
  mixes with it), and each cascade's map is drawn in a square along the picture's bottom left,
  white nearest the light, framed in its tint.
- The profiler has a zone per cascade drawn (*Shadow cascade 0* to *3*, `DrawShadowCascades()`).
- `VulkanRenderer::GetSceneViewShadows()` gives a view's cascades and what each culled and drew.

## Numbers

`NyxInstancingBenchmark instanced`, the instancing note's field ([Instancing](instancing.md#numbers):
50 000 cubes in 200 cells and 300 spheres) on a ground plane of 200 × 100 m under a sun 50° up, in a
1280 × 720 view, with the light's defaults: four cascades of 2048² out to 80 m. Median of 40 frames
in Debug and 300 in Release, vsync off, a hidden window, an RTX 3070 Ti and an i7-12700K.

| Build | Shot | Shadows | Frame | Scene CPU | Scene views GPU |
| --- | --- | --- | --- | --- | --- |
| Debug | above | off | 6.25 ms | 4.33 ms | 0.89 ms |
| Debug | above | 4 cascades | 12.27 ms | 7.05 ms | 3.08 ms |
| Debug | inside | off | 5.25 ms | 4.08 ms | 0.29 ms |
| Debug | inside | 4 cascades | 12.42 ms | 6.96 ms | 2.51 ms |
| Release | above | off | 1.38 ms | 0.15 ms | 0.81 ms |
| Release | above | 4 cascades | 2.41 ms | 0.23 ms | 1.67 ms |
| Release | inside | off | 0.77 ms | 0.13 ms | 0.27 ms |
| Release | inside | 4 cascades | 1.97 ms | 0.23 ms | 1.28 ms |

What it says: in Release, four cascades cost about 1 ms of GPU time and 0.1 ms of the renderer's
CPU time (culling the field's 200 cells and 300 spheres four more times, and recording the
cascades). The GPU's share is the casters drawn four times at 2048², most of them the field's
50 000 cubes, and the lookups. *Inside* pays as much as *above*: the cascades are spheres round the
camera, not its frustum, so they hold most of what is near it whichever way it looks. In Debug the
CPU's extra 2.7 ms is the same culling unoptimised.

## Later

- Cascades end at a line in the picture where the next, coarser one starts; a blend across the
  split, or dithering, would hide it.
- The shadow distance is the same whatever the scene's size; fitting the last split to what is in
  view would give the near ones more texels.
- Casters are drawn again every frame. A cascade whose casters and light didn't move could keep its
  layer, if the views didn't share the map.
- No GPU timestamps per cascade yet: the scene views' time includes the shadows.
- Materials that cut holes (alpha masks) would need a fragment shader in the shadow pass.
- Only the primary directional light casts; point and spot lights would need maps of their own.
- Instances that mirror one by one aren't turned round (as in the scene pass).
