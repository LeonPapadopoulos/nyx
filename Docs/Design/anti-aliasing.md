# Anti-aliasing

How the scene views smooth the edges of what they draw, and why it is done this way: multisample
anti-aliasing (MSAA) in the scene pass now, temporal anti-aliasing (TAA) perhaps later. The setting
is on the renderer (`IRenderer::SetMsaaSamples()`); the sample count's math is in
`Engine/Source/Runtime/Render/include/Multisampling.h`, the targets and the resolve in
`VulkanViewportTarget`, the pipelines that draw into the scene pass in `VulkanRenderer` (the sky)
and `VulkanMaterials`.

## What it is for

A pixel of a single-sampled picture is whatever covers its middle. Thin things, a branch, a blade
of a plant far away, a wire, are thinner than a pixel there, so they cover some middles and miss
others: they break up into dots and dashes that crawl as the camera moves, and every edge is a
staircase. A dense instanced field in the distance is all such edges, and it shimmers.

With N samples a pixel, coverage and depth are found at N points in the pixel, and the pixel is
shaded once for the samples of each triangle that covers it. At the pass's end each pixel's
samples are averaged: an edge pixel is the mix of what covers it, in the proportion it covers it,
and a blade a quarter of a pixel wide shows as a quarter of its colour instead of now all, now
nothing.

## The setting

- **1, 2, 4 or 8 samples a pixel**, 4 by default (`DefaultMsaaSamples`). Asked for more than the
  device can do (`framebufferColorSampleCounts` and `framebufferDepthSampleCounts` together), the
  view draws with the most it can of those; a count between is rounded down; 0 is 1
  (`ChooseMsaaSamples()`).
- **On the renderer, the same for every view.** A pipeline is made for one sample count, and every
  material's opaque and decal pipelines and the sky's are drawn into the scene pass of every view.
  One count for the renderer keeps one set of them; a count per view would make each material's
  pipelines once for every count in use, or make them again whenever a view of another count is
  drawn. Nothing asks for two counts at once: a material preview and the scene look alike at the
  same count.
- **Changed live.** `SetMsaaSamples()` takes effect at the next frame's `PrepareSceneViews()`:
  the GPU is waited for, every view's targets are made again (as for a resize, so the editor's
  panels and the blur take the new picture), then the sky's pipeline and the materials' scene
  pipelines (`VulkanMaterials::RebuildScenePipelines()`, from the SPIR-V each shader keeps).
  The frame that changes it takes 7 to 15 ms in all with a few materials and the pipeline cache
  warm (`NyxMultisamplingGpuTests` prints it); nothing else is made again.
- **Where it is set.** The editor has *Anti-aliasing* in *Window > Preferences* (a user preference,
  `EditorPreferences::MsaaSamples`, saved with the others), applied each frame like vsync, and
  passed to the games it starts (`--msaa <samples>` on NyxGame's command line,
  `GameLaunchOptions::MsaaSamples`). A Debug build of the game cycles it with F7. A game of its
  own calls `SetMsaaSamples()` from its options; it is a renderer setting, not a project one,
  because what a machine can afford differs and the picture means the same at every count.

## The scene pass, multisampled

With more than one sample, each view has three more targets, multisampled: the HDR colour, the
ambient light (the sky's light apart for the ambient occlusion) and the depth. The scene pass
clears and draws into those; the single-sampled targets of before become its resolve attachments,
written once at its end. Everything after the scene pass reads the single-sampled ones exactly as
it did, so none of it changed:

| Target | Resolved to | Read by |
| --- | --- | --- |
| HDR colour | the mean of its samples | the ambient occlusion's composite and the particles (added to), bloom, the tonemap |
| Ambient light | the mean of its samples | the ambient occlusion's composite |
| Depth | its **nearest** sample (`eMax`: depth is reversed), else its first | the ambient occlusion (halved and in the composite), the particles' soft fade, the picture pass's overlays (the grid, debug lines, the outline) tested against it |

- **A depth resolve** needs a render pass of the second version (`vkCreateRenderPass2`, core in
  Vulkan 1.2) with `VkSubpassDescriptionDepthStencilResolve`; the colours resolve as ever. With one
  sample, the pass is the one of before, built the same way, so a picture at one sample is the one
  of before to the bit (every reference picture is unchanged).
- **Why the nearest depth.** At an edge pixel the colour is a mix of the near thing and what is
  behind it, and one depth has to stand for both. The nearest is the conservative one: the grid and
  the debug lines drawn over the picture are hidden by any near edge that covers part of the pixel,
  so they never show through the softened edge of an object in front of them; the ambient
  occlusion treats the edge pixel as the near surface, which only widens the near side of the
  occlusion's edge by the pixel it already blurs across. The first sample (`eSampleZero`, which
  every device has) would pick one side at random along an edge. A device without `eMax` (rare)
  gets the first sample; `VulkanViewportTarget::ChooseDepthResolveMode()` says which.
- **Not read per sample.** Nothing samples the multisampled targets: no pass reads a sample of
  its own choosing, the ambient occlusion works on the resolved depth at half size anyway, and the
  multisampled targets are transient attachments, stored nowhere after the pass.
- **The sky** draws into the multisampled targets, tested against the multisampled depth: it fills
  every sample no object covers, so edges against the sky are smoothed like any other.
- **Decals** are quads drawn over what is there, tested against the depth (`DecalPipeline`, depth
  test without writing); they don't read the depth buffer. Multisampled, they are tested and
  blended per sample, so a decal's edge, and the edge of what is in front of it, are smoothed like
  the rest, and a decal over an object's edge covers exactly the samples of the object.
- **Fog** is worked out per pixel in the surface and sky shaders from the pixel's place, not from
  the depth buffer, so it needs nothing.
- **Picking and the selection masks** have targets of their own, single-sampled, drawn by the
  editor's passes before the scene pass: an id a pixel, exact, at every sample count
  (`NyxMultisamplingGpuTests` picks the same entity under every pixel it tries at 1, 2, 4 and 8).
  The outline drawn from them is not smoothed; it is a marker, and stays crisp.
- **Point lights** are added up in the surface shaders, per pixel, in the scene pass: their light
  is multisampled with the rest, and nothing about them changed. Their icons and ranges in the
  editor are overlay lines and picking, single-sampled as ever.
- **Particles** are drawn after the resolve, in a pass of their own over the resolved HDR scene,
  one sample a pixel, against the resolved depth, which they read for their soft fade
  (`VulkanParticles`, `Particle.slang`); their pipelines keep one sample. Why there, and not in
  the multisampled targets:
  - they are soft by nature: a billboard's edge is a texture's fade and its meeting with the scene
    the soft fade by depth, so samples smooth almost nothing of theirs;
  - they are overdrawn many times, blended: per sample, every layer's blending and the
    multisampled targets' bandwidth would be paid N times over for edges no one sees;
  - their depth read for the fade would need the multisampled depth sampled per sample
    (`Texture2DMS`, a sample chosen or each shaded), or the resolved one anyway;
  - and they keep their place in the frame, after the ambient occlusion, whose composite adds to
    the resolved HDR.
  The depth they read is the one the ambient occlusion and the overlays read: the nearest sample.
  Where a near object's anti-aliased edge partly covers a pixel, the particles behind it are hidden
  in all of that pixel, so a cloud behind an object never shows through its softened edge; the
  price is that a pixel of the edge lacks the cloud's share where the object covers only part of
  it, which the cloud's own softness hides. A mesh particle's hard edge isn't smoothed; a mesh
  shape that needs it would move into the scene pass, as decals are.
- **Shadows** are drawn into their own map, unchanged; a material that casts no shadows (its Cast
  Shadows option) is left out of the map as before. Instanced draws, two-sided materials and
  vertex functions draw as before: only their pipelines' sample count changed.

What the shaders pay: a fragment shader still runs once a pixel for each triangle (not per sample;
sample shading is off), so the cost is in the rasteriser, the depth test, the blending and the
memory of N samples, and in the resolve.

## Alpha to coverage: not yet

A material whose opacity cuts holes in it, a card of leaves, could have its opacity turned into
coverage (alpha to coverage): with 4 samples its cut edges would be as smooth as geometry's. It
isn't there, because Nyx's opaque materials have no cut-out mode yet: their opacity is ignored by
the opaque pipeline, and the shadow, picking and selection mask passes draw whole triangles. Alpha
to coverage in the scene pass alone would cut the leaves in the picture and leave square shadows
and square picking. It belongs with a masked material mode (an option like *TwoSided*) that cuts
in all those passes alike: alpha tested where there is one sample, alpha to coverage where there
are more. Then it is `alphaToCoverageEnable` on that mode's scene pipeline, one more pipeline for
the shaders that use it.

## TAA: what it would add, and why not now

Temporal anti-aliasing jitters the camera by a fraction of a pixel each frame and blends each
pixel with the history reprojected from the frame before, by motion vectors, rejecting history that
doesn't fit.

- **What it would add.** It smooths what MSAA can't: the inside of surfaces, specular highlights
  that flicker as a normal map tilts across a pixel, alpha-cut foliage without alpha to coverage,
  the noise of the ambient occlusion and of soft shadows, and edges in the passes after the
  resolve. It costs about one full-screen pass, much the same at any scene complexity, where
  MSAA's cost grows with the edges and the memory with the samples.
- **What it costs Nyx first.** Motion vectors: a target the scene pass writes with each pixel's
  movement since the last frame, which needs the last frame's transform of every object and
  instance (the instance stream twice, or the previous transforms kept), and the last frame's
  vertex function at the last frame's time, for wind. A history target per view, kept across
  frames and dropped on a cut or a resize. A jittered projection everywhere the projection is used,
  undone where exact pixels matter (picking, the editor's overlays, `ScreenPointToRay()` stays
  unjittered). Then the tuning that makes it look right: history clamping, ghosting behind moving
  things, the softening it brings that wants a sharpening pass after it.
- **Why it was left.** It is a larger change than MSAA across more of the renderer (the scene
  pass's outputs, the instance stream, the vertex functions, every view's history) for a picture
  that has to be tuned against ghosting and blur, while MSAA fixes what was asked for, thin
  geometry breaking up in the distance, with no change to anything after the scene pass. With a
  forward renderer, MSAA is cheap. TAA can come later on top of a lower MSAA count, or instead of
  it, when the vertex functions and the instance stream keep their last frame anyway (for motion
  blur, for instance).

## Numbers

`NyxInstancingBenchmark msaa`, 1280 × 720, vsync off, a hidden window, an RTX 3070 Ti and an
i7-12700K; the median of 300 frames in Release. *Frame* is `DrawFrame()` on the CPU, *scene CPU*
the renderer's extraction, culling and recording, *GPU* the scene views' passes, by timestamps,
*particles* their pass alone. *Field* is the instancing note's field (50 000 cubes in 200 cells and
300 spheres) on a ground, under a sun casting four cascades, with the environment's defaults (fog,
the sky following the sun, bloom, ambient occlusion); *atmosphere* the atmosphere test scene (about
24 000 cubes scattered over hills, fog, bloom, ambient occlusion) from its camera; with 64 point
lights in view, each reaching 6 m; with 10 emitters of 2 000 particles each, half additive, half
alpha blended and lit.

| Scene, shot | Samples | Frame | Scene CPU | Scene views GPU | Particles GPU |
| --- | --- | --- | --- | --- | --- |
| field, above | 1 | 2.88 ms | 0.28 ms | 1.76 ms | |
| field, above | 4 | 2.98 ms | 0.29 ms | 1.86 ms | |
| field, above | 8 | 3.08 ms | 0.29 ms | 2.14 ms | |
| field, inside | 1 | 2.06 ms | 0.26 ms | 1.27 ms | |
| field, inside | 4 | 2.39 ms | 0.26 ms | 1.48 ms | |
| field, inside | 8 | 2.56 ms | 0.28 ms | 1.66 ms | |
| atmosphere | 1 | 2.56 ms | 0.13 ms | 1.79 ms | |
| atmosphere | 4 | 2.69 ms | 0.13 ms | 1.90 ms | |
| atmosphere | 8 | 2.95 ms | 0.15 ms | 2.09 ms | |
| atmosphere, 64 point lights | 1 | 2.97 ms | 0.14 ms | 2.15 ms | |
| atmosphere, 64 point lights | 4 | 3.46 ms | 0.17 ms | 2.41 ms | |
| atmosphere, 64 point lights | 8 | 3.60 ms | 0.17 ms | 2.71 ms | |
| atmosphere, 20 000 particles | 1 | 3.76 ms | 1.20 ms | 1.80 ms | 0.15 ms |
| atmosphere, 20 000 particles | 4 | 3.93 ms | 1.26 ms | 1.94 ms | 0.16 ms |
| atmosphere, 20 000 particles | 8 | 4.20 ms | 1.27 ms | 2.16 ms | 0.16 ms |

What it says: 4 samples cost the scene views 0.1 to 0.25 ms of GPU time at 1280 × 720 (5 to 15 %),
8 samples 0.3 to 0.55 ms (15 to 30 %). The CPU's work doesn't change: the same draws are recorded,
only the targets and the pipelines' sample count differ. The passes after the resolve cost the same
at every count: bloom 0.05 ms, the ambient occlusion 0.1 ms, the particles 0.15 ms (drawn once a
pixel). Point lights are shaded once a pixel too; with 64 of them the scene pass's extra cost of the
samples is a little larger, from the larger lit area's bandwidth, not from shading. The spread from
one run to the next is about 0.1 ms. At 1920 × 1080 the GPU's share grows with the pixels, roughly
2.25 times.

In Debug the frame is 9.2 to 9.3 ms (field, above), 4.3 to 4.9 ms (atmosphere) and 15 to 20 ms with
the particles (their simulation on the CPU, 11 to 15 ms in Debug) at every count; the scene's CPU
time doesn't change with the samples. Its GPU times move by up to 1 ms from run to run while other
programs use the GPU, so they say nothing of the samples.

The memory of one view's targets at 1920 × 1080 (`VulkanRenderer::GetSceneViewTargetMemory()`):

| Samples | Targets | Of that, multisampled |
| --- | --- | --- |
| 1 | 50.6 MiB | none |
| 2 | 135 MiB | 84 MiB |
| 4 | 210.0 MiB | 159 MiB |
| 8 | 369.4 MiB | 319 MiB |

Each sample is 20 bytes: 8 of HDR colour, 8 of the ambient light, 4 of depth. That is the cost to
watch: four open editor views at 1080p and 8 samples hold about 1.5 GB of targets. The ambient
light target could be left out while a view has no ambient occlusion (its scene pass would then
have one colour target, a second set of pipelines), and on a tiled GPU the transient multisampled
targets would take lazily allocated memory, which desktop GPUs don't have.

## Later

- A masked material mode with alpha to coverage (above).
- The multisampled ambient light only while the view has ambient occlusion, if the memory matters.
- TAA (above), perhaps on top of 2 samples.
- Sample shading for materials whose shading aliases inside triangles (thin specular highlights),
  as an option of the material: it shades every sample, so it costs N times the fragment work.
