# Instancing and culling

How Nyx draws one mesh at many transforms, and leaves out what a camera can't see, and why. The
code is in `Engine/Source/Runtime/Render` (`MeshInstanceCache`, `SceneCulling.h`,
`VulkanRenderer::CullSceneView()`) and `Engine/Source/Runtime/Scene` (`MeshInstancesComponent`).

## The shape of it

- **The data** is a `MeshInstancesComponent` on an entity that has a *Mesh Renderer* (and maybe a
  mesh made at run time): shared `MeshInstances`, a list of transforms relative to the entity's
  transform, optionally in *cells* (each the bounds of its instances' origins, in the entity's
  space, and a run of the transforms), and a revision. It is derived data like a
  `RuntimeMeshComponent`: not reflected, never saved, made in a post-load step by whatever
  scatters it ([Architecture: Generated content](../Architecture.md#generated-content)).
- **On the GPU** the transforms are a vertex buffer of `glm::mat4`, one for each `MeshInstances`,
  kept by `MeshInstanceCache` exactly as `RuntimeMeshCache` keeps meshes: keyed by the data's
  address (the cache holds a reference, so the address can't be reused while it is a key),
  uploaded when first drawn and again only for a higher revision, into a new buffer, the old one
  destroyed after the next frame's fence; instances no shown world drew in a frame are let go of.
- **Every mesh pipeline reads a second vertex stream**: binding 1, one `mat4` per instance, its
  columns at locations 5 to 8 (`GetInstanceAttributeDescriptions()`). `transformSurfaceVertex()`
  in `nyx/surface.slang` (every surface material, the missing material) and `Picking.vert` and
  `SelectionMask.vert` place a vertex at `Model × instance`, the normal by the cofactor matrix of
  that product. A draw without instances binds the cache's identity buffer, a single `mat4`; the
  identity leaves `Model` exactly as it was, so what was drawn before draws to the same bits.
- **Culling** happens per scene view, after the worlds are extracted (`PrepareSceneViews()`): each
  view's camera (the editor's free camera, the game's primary camera, a frozen one) gives a
  frustum, and the view's draw list (`SceneViewInstance::Draws`) holds only what isn't wholly
  outside it. The scene pass draws that list; picking and the selection masks draw everything, as
  before, a click may be anywhere and the masks are drawn only when they change.
- **Batching**: entities drawn once that draw alike, the same mesh with the same material in each
  section, are one draw: their transforms go into this frame's instance stream, with `Model` the
  identity.

## Why a vertex stream and not a storage buffer

Both work; the vertex stream fits what Nyx has:

- **Pipelines already describe the mesh as vertex input.** `VulkanMaterials::MakePipeline()` makes
  every material's pipelines from the attributes its vertex shader reads (`InputLocations()`); the
  instance is four more attributes and one more binding in the same place, and in the editor's two
  pipelines. A storage buffer needs a descriptor: either a new binding in the bindless set with a
  sub-allocator for every `MeshInstances` (and a descriptor rewrite when it grows), or buffer device
  addresses, a device feature Nyx doesn't turn on yet, and 8 more bytes of push constants.
- **Cells are ranges of the stream.** A run of cells is one `drawIndexed()` with `firstInstance` at
  the run's start, and the GPU's instance-rate fetch adds it: no index arithmetic in the shader, no
  per-draw offset to push, no `SV_InstanceID` semantics to rely on.
- **Ownership stays simple.** One buffer per `MeshInstances`, made new when it changes and retired
  after the fence, as meshes made at run time are: no allocator, no fragmentation, nothing shared
  that one upload could overwrite under another's draw.
- **The cost**: every draw with a mesh pipeline binds the second stream, the identity buffer when it
  has no instances (decals, the overlays' single objects). One bind; the shader's extra matrix
  product per vertex is noise for the meshes drawn so far.

A storage buffer becomes the better fit with culling on the GPU: a compute pass that writes the
visible instances' indices reads the transforms by index anyway. The vertex stream can still be the
output of such a pass, so nothing here has to be undone for it.

## Culling

- **Bounds.** At extraction each object gets its bounds in the world where it is drawn this frame
  (`RenderObject::WorldBounds`: the mesh's bounds through `DrawTransform`, between steps for a world
  drawn so). An instanced object's cells are kept in the entity's space by the cache, each the
  cell's given bounds of its origins united with the mesh's box at every one of its instances
  (`MakeInstanceCullCells()`), so rotation and scale are covered and a cell given bounds too small
  is never culled while an instance of it is in view; they are made again only when the instances
  or the mesh's bounds change. Each frame they go to the world through `DrawTransform`
  (`RenderWorld::Cells`), 200 boxes for the benchmark's field.
- **The frustum** (`MakeFrustum()`) is the six planes of Vulkan's clip space taken from the
  view-projection matrix: x and y from −w to w, depth from 0 to w, whichever end is near, so it
  holds for Nyx's reversed depth. `ClassifyBox()` tests a box's furthest corner along each plane's
  normal (outside one plane: outside) and its nearest (inside all: inside; else straddling). Near
  the frustum's corners a box outside it can be inside every plane: it is drawn, never the other
  way round.
- **Per view**: an object outside is skipped; one drawn once is kept for batching; an instanced one
  appends the runs of its cells in view (`AppendInstanceRuns()`), a run being cells that follow on
  in the transforms. With cells, what is drawn is what the cells cover; transforms in no cell are
  not drawn, and cells shouldn't overlap.
- **Batching** sorts the visible single objects by mesh and section materials (stable, so the same
  world draws in the same order), and makes each group one draw of their transforms in the frame's
  instance stream, a host-visible buffer written after the last frame's fence (one frame in flight)
  and grown by half again when it doesn't fit. Trees sharing one cached mesh and its materials are
  one draw.
- **Seeing it**: `IRenderer::SetSceneViewCullingDebug()`: *Bounds* draws, over the picture, the
  boxes the view culls by, green drawn and red culled (cells for an instanced entity); *Frozen*
  culls by the camera as it was when frozen, its frustum in yellow, while the camera moves on. The
  editor's *Debug > Culling Bounds* and *Freeze Culling* set it for every view; a Debug build of the
  game cycles it with F4. The lines are debug drawing, so a Release build has none.
- **What was culled** is in `SceneViewCullingStats` (`VulkanRenderer::GetSceneViewCullingStats()`):
  objects and the culled ones, cells and the culled ones, instances drawn, and draws.

The shadows' cascades will cull the same way: a cascade's view-projection gives its frustum, and
`ClassifyBox()` and `AppendInstanceRuns()` its draw list.

## Faces

An instanced draw culls back faces as any other, its material's way (two-sided or not,
[Shaders and materials](shader-materials.md#materials)), and for the whole draw: the entity's
transform may mirror it, but its instances' transforms shouldn't, as one instance mirrored would be
drawn inside out. Entities batched into one draw are mirrored alike or not at all.

## Numbers

`NyxInstancingBenchmark`: 50 000 cubes of 0.15 to 0.3 m in 200 cells of 10 × 10 m (250 each) over
200 × 100 m, and 300 spheres among them, one Untextured material, in a 1280 × 720 view. *Above*: 120
m up looking down at all of it; *inside*: 2 m up in its middle looking along −z, about an eighth of
it in view. Median of 40 frames in Debug and 300 in Release, vsync off; a hidden window, an RTX 3070 Ti
and an i7-12700K. *Frame* is `DrawFrame()` on the CPU; *scene CPU* the renderer's work for the worlds and views
(extraction, culling, recording); *scene views GPU* the views' passes by timestamps.

Before (`071d6b9`, no instancing or culling: the field as 50 300 entities):

| Build | Shot | Frame | Scene CPU | Scene views GPU |
| --- | --- | --- | --- | --- |
| Debug | above | 978 ms | 957 ms | 12.4 ms |
| Debug | inside | 1094 ms | 1085 ms | 9.2 ms |
| Release | above | 30.4 ms | 24.6 ms | 4.6 ms |
| Release | inside | 29.9 ms | 23.4 ms | 4.9 ms |

After, the same field as 50 300 entities (culled and batched), and as one instanced entity and 300
spheres:

| Build | Field | Shot | Frame | Scene CPU | Scene views GPU |
| --- | --- | --- | --- | --- | --- |
| Debug | entities | above | 516 ms | 507 ms | 4.5 ms |
| Debug | entities | inside | 481 ms | 477 ms | 0.9 ms |
| Debug | instanced | above | 7.6 ms | 4.3 ms | 1.8 ms |
| Debug | instanced | inside | 5.6 ms | 4.1 ms | 0.5 ms |
| Release | entities | above | 21.7 ms | 17.4 ms | 2.4 ms |
| Release | entities | inside | 16.1 ms | 13.7 ms | 0.8 ms |
| Release | instanced | above | 1.4 ms | 0.15 ms | 0.95 ms |
| Release | instanced | inside | 0.6 ms | 0.12 ms | 0.27 ms |

What it says: the entities' cost was the CPU, one draw each, recorded every frame. Instanced, the
CPU's work no longer grows with the instances, only with the cells; culling takes the GPU's work
down with the share in view. Entities that draw alike gain from batching too, but each still costs
its extraction, bounds and sort every frame, so a field belongs in one instanced entity.

## Later

- The transforms are in host-visible memory, as the meshes' vertices are; device-local memory with
  a staging copy reads faster on discrete GPUs.
- Only a transform per instance: a colour or a wind phase would be another stream or a wider one.
- No culling by distance or level of detail, no occlusion culling, nothing on the GPU yet. The
  shadows' cascades cull with the same code, each by its own frustum ([Shadows](shadows.md)).
- Picking and the selection masks draw every instance, unculled.
- Entities that share one `MeshInstances` but have meshes of different bounds make the cells again
  each time the other is drawn.
- Batching sorts the visible entities drawn once, every frame and view.
