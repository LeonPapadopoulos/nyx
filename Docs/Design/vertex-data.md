# Vertex data

Numbers of a game's own that reach the materials: a mesh's for each vertex, an instance's or an
entity's for each draw, and the time; and a material's vertex function, which moves its meshes'
vertices by them. The code is in `Engine/Source/Runtime/Core` (`MeshData.h`),
`Engine/Source/Runtime/Render` (`Mesh.h`, `MeshInstanceCache`, `VulkanRenderer`,
`VulkanMaterials`, `ShaderCompiler`), `Engine/Source/Runtime/Scene` (`MeshInstancesComponent`,
`MeshRendererComponent`) and `Engine/Shaders/Library/nyx/surface.slang`.

What it is for, as examples: a sway in the wind (how far each vertex bends, a phase for each copy
of a plant), something that grows (the ages at which each vertex appears and is fully grown, and
how old each copy is, its unborn parts shrunk away), a tint or a wear for each copy. Nyx doesn't
know what the numbers mean; the material that reads them does.

## The shape of it

- **Each vertex** has `Vertex::Custom`, a `glm::vec4`, zero unless set: the meshes Nyx makes and
  imports (the built-in ones, a ground, glTF) leave it zero. A mesh made at run time
  (`RuntimeMeshComponent`) sets it as it sets the colour.
- **Each instance** of a `MeshInstancesComponent` has `MeshInstances::Custom[i]` beside
  `Transforms[i]`, zero for one past the end of `Custom`, so instances without numbers cost no
  memory on the CPU. A scatter fills `Custom.x` with a number of each copy's own from 0 up to 1 (a
  hash of where it stands), for a phase.
- **An entity drawn once** has `MeshRendererComponent::Custom` (*Custom Data* in the details
  panel), which the material gets as its instance's numbers, so one material serves both ways of
  drawing. An instanced entity's own value isn't used: its instances have theirs.
- **The time** is `View.Time`, in seconds: the view's world's. A game's world's is its fixed-step
  clock's at the moment its poses are drawn, so it stands still while the game is paused and moves
  on by a step with Step; the editor's worlds have the renderer's seconds
  ([Shaders and materials: Decals](shader-materials.md#decals)).
- **What the material gets**, in `SurfaceInput`: `Custom`, the vertex's, interpolated between the
  vertices as any attribute is; `InstanceCustom`, the instance's or the entity's, the same at every
  vertex of the instance.

## On the GPU

- **The vertex buffer** holds a `GpuVertex` a vertex, made from each `Vertex` on upload
  (`ToGpuVertex()`): the vertex as it was, with `Custom` as four half floats. 68 bytes, 8 more than
  before.
- **The instance stream** (binding 1) is a `MeshInstanceData` an instance: the transform's columns
  at locations 5 to 8 as before, then its custom numbers at 10, 80 bytes. The vertex's custom
  numbers are at location 9, after the instance's transform, so the engine's GLSL shaders
  (`Picking.vert`, `SelectionMask.vert`) find what they read where it was.
- **An entity drawn once** is drawn from this frame's instance stream, batched with the entities
  that draw alike (see [Instancing](instancing.md)): its transform goes into the stream every
  frame anyway, and its custom numbers go with it. Changing them every frame costs nothing more:
  no upload of the mesh, no new push constant, no draw split off. A draw that binds the identity
  buffer instead gets zero: decals, and the editor's passes, which a vertex function changes
  ([below](#how-it-is-compiled-the-least-new-machinery)).
- **Pipelines read what their vertex shader reads** (`GetMeshAttributesRead()`), as
  before; every surface material's vertex shader passes both on, so every mesh pipeline reads
  them. The fetch is a few bytes more for each vertex and each instance ([Numbers](#numbers)).

## Why these sizes

- **Four numbers, not two or eight.** The uses seen so far want one to three a vertex: a sway's
  weight; a growth's two ages; a weight and a pivot height. Four is what one attribute fetch
  carries, and leaves one spare. More can come as a second attribute when a use needs them.
- **Half precision in the vertex buffer, a float on the CPU.** A `glm::vec4` keeps meshes simple to
  make and test; the GPU gets 8 bytes, not 16, as every mesh pays for the channels whether it uses
  them or not: a vertex of 68 bytes, not 76 (13 % more than before, not 27 %). Half floats keep
  about three significant digits (11 bits), with 0, 1 and small whole numbers exact, and reach
  ±65 504: enough for weights, ages as a part of a lifetime, phases and indices up to 2048, not for
  positions. A growth over two hours in 1/2048ths moves on every 3.5 s, smoothly between the
  vertices. Bytes (four 8-bit numbers from 0 to 1) would have kept 4 bytes, but 1/255ths are too
  coarse for an age, and nothing below 0 or above 1 would fit.
- **One interleaved stream, not a stream of its own.** A second vertex buffer only for meshes with
  custom channels would let the others pay nothing, but a pipeline's vertex input is fixed when it
  is made: every mesh would need the buffer bound (a shared one of zeros, as long as the longest
  mesh) or every material two pipelines. 8 bytes a vertex is the cheaper of the three.
- **The instance's numbers in the instance stream, at full precision.** 16 bytes an instance beside
  the 64 of its transform; an instance has far fewer of them than its mesh has vertices.

## Numbers

`NyxInstancingBenchmark` (see [Instancing: Numbers](instancing.md#numbers) for the field, the
shots and the machine), Release and Debug, before (`84b767b`) and after the larger vertex and
instance:

Medians of 300 frames in Release and 60 in Debug; where two numbers are given, two runs taken in
turn, before and after, as a feel for the noise. *Scene CPU* is the renderer's work for the worlds
and views, *GPU* the views' passes.

| Build | Field | Shot | Scene CPU before | after | GPU before | after |
| --- | --- | --- | --- | --- | --- | --- |
| Release | 50 300 entities | above | 17.4, 16.6 ms | 17.3, 18.0 ms | 1.63, 1.54 ms | 1.63, 1.66 ms |
| Release | 50 300 entities | inside | 13.6, 13.8 ms | 13.8, 14.0 ms | 0.54, 0.53 ms | 0.56, 0.54 ms |
| Release | instanced | above | 0.14 ms | 0.15 ms | 0.80 ms | 0.81 ms |
| Release | instanced | inside | 0.13 ms | 0.13 ms | 0.25 ms | 0.25 ms |
| Release | instanced, 4 shadow cascades | above | 0.24 ms | 0.23 ms | 1.77 ms | 1.53 ms |
| Release | instanced, 4 shadow cascades | inside | 0.23 ms | 0.23 ms | 1.20 ms | 1.29 ms |
| Debug | 50 300 entities | above | 524 ms | 527 ms | 3.95 ms | 3.96 ms |
| Debug | instanced | above | 4.39, 4.37 ms | 4.62, 4.46 ms | 0.88, 0.81 ms | 0.89, 0.81 ms |

What it says: nothing that stands out of the noise. The field's 1.8 million vertices a pass fetch
8 more bytes each, and each instance 16 more; at these sizes the GPU's time goes to other things.
On the CPU, the batched entities' 16 more bytes in this frame's stream don't show either.

## The vertex function

A material moves its vertices by implementing `ISurfaceVertex` beside `ISurface`
(`nyx/surface.slang`): one function, `displace(inout SurfaceVertex vertex)`, a method of the
material's struct, so its fields are its parameters as `shade()`'s are.

- **What it gets**: the vertex's position and normal in the mesh's own space, its colour, UV and
  custom numbers, the instance's (or the entity's) custom numbers, the transform from the mesh's
  space to the world (`ObjectToWorld`, the draw's after the instance's) and the time.
- **What it moves**: `Position` and `Normal` in the mesh's own space, which then follow the
  entity's and the instance's turn and scale, e.g. a part shrunk towards a point of the mesh; and
  `OffsetWS`, added in the world after the transform, in metres, for moves that keep their size and
  direction however the entity lies, such as a wind's. Either or both.
- **Where it runs**: wherever the mesh is drawn, so everything moves together: the scene pass, the
  primary light's shadow maps, and the editor's picking and selection masks. A click picks the mesh
  where it is drawn, and the outline follows it. Decals drawn with such a material lie flat as
  ever: a decal's quad isn't moved.
- **Culling**: a material declares how far its function moves a vertex at most, its *Max Vertex
  Offset* (`MaxVertexOffset = 0.2` in the `.nyxmat`, a `MaterialOptions` option, inherited by
  instances). The renderer widens the bounds it culls an entity by, every cell of an instanced
  one, and the shadows' caster bounds, by the largest of its sections' materials'. Keeping within
  it is the material's part: a vertex moved farther may be culled while in view, as a too small
  bound would. Nothing measures it.

### How it is compiled: the least new machinery

Materials were already compiled as the surface domain's entry points specialised with the
material's struct (`ShaderCompiler::CompileMaterial()`); the vertex side adds to that, not beside
it:

- **The domain names an interface and the entry points that need it**
  (`ShaderDomain::VertexInterface` and `VertexEntryPoints`: `ISurfaceVertex`,
  `displacedSurfaceVertex` and `displacedShadowVertex`). The compiler asks Slang's reflection whether the material's struct
  implements the interface (`ProgramLayout::isSubType()`); if it does, those two entry points are
  compiled with the others, generic over the material as the fragment ones are. A material without
  a vertex function compiles to exactly what it did, four entry points; one with it to six.
- **No new kind of file, no flag.** The function is part of the material's shader, so a material a
  project ships is searched for and compiled as before, project assets before the engine's, and
  live reload makes it again when the file is saved.
- **Its pipelines.** `VulkanMaterials` makes the material's scene pipeline from
  `displacedSurfaceVertex` in place of `surfaceVertex`. The other passes that draw meshes have
  their own state (render pass, depth, what the fragment writes), which their owners know, so
  each owner makes the material's pipeline from its vertex shader with its own state and fragment
  shader (`VulkanMaterials::VertexFunctionPasses`: `VulkanShadows::MakeCasterPipeline()` from
  `displacedShadowVertex`, `VulkanEditorOverlays::MakePickingPipeline()` and
  `MakeSelectionMaskPipeline()` from `displacedSurfaceVertex`). The material's handle points at
  them (`Material::ShadowPipeline`, ...); null for any other material, which the passes draw with
  their shared pipelines as before. A pass switches pipelines per section only where a material
  has its own.
- **What the pipelines read** of the vertex and the instance is what their vertex shader reads
  (`GetMeshAttributesRead()`, the SPIR-V's input locations), for the shared pipelines as for the
  materials'.
- **The editor's passes** draw an entity drawn once from the identity buffer, not from this
  frame's instance stream, so its custom numbers go in the draw's push constants
  (`ObjectConstants::InstanceCustom`, 16 more bytes, 112 in all, within the 128 every device has),
  which the shader adds to the stream's: one of the two is always zero. The selection masks, drawn
  again only when what they show changes, are drawn every frame while a material with a vertex
  function is in the world, as it may move by the time.

### The wind, as the example

`Shaders/VertexColorWind.slang`, `Materials/VertexColorWind.nyxmat` and its two-sided instance
`Materials/VertexColorWindTwoSided.nyxmat`: the vertex colours, swaying.

- A vertex bends by its mesh's `Custom.x` (0 where it holds still, a blade's foot; 1 where it
  bends most, its tip), each instance with its own phase from its `InstanceCustom.x`, which a
  scatter gives every copy.
- The wind's settings are the material's: *Direction* along the ground, *Strength* (how far a tip
  leans at a gust's peak, in metres), *Gustiness* (how much of the lean comes and goes), *Gust
  Frequency* and *Gust Length* (gusts cross the ground along the wind, so neighbours lean together
  and a field ripples), *Flutter* and *Flutter Frequency* (a quick sway on top, each copy at its
  phase). Material values rather than a wind of the world's: simple, editable live, and one wind
  for each look; a world's wind can come later as view constants the materials read.
- A tip moves at most *Strength* × (1 + *Flutter*), 0.162 m with the defaults; the material's Max
  Vertex Offset is 0.17.
- The built-in *Crossed Quads* mesh (`Meshes/CrossedQuads.nyxmesh`, `MakeCrossedQuads()`) carries
  the weights: two upright quads crossed, the usual stand-in for small plants, its weight the
  square of the height's fraction so a lean curves. `Assets/Scenes/GroundTest.nyxscene` has a
  scatter of them round the flat place, in `Materials/GroundTest/Grass.nyxmat`, swaying, their
  shadows with them.

### What it costs

The same benchmark with the field in `Materials/VertexColorWind.nyxmat` (its weights are zero on
the cubes, but the function runs for every vertex all the same) against
`Materials/VertexColor.nyxmat`, which looks the same without the function:

| Build | Field | Shot | Scene CPU `VertexColor` | `VertexColorWind` | GPU `VertexColor` | `VertexColorWind` |
| --- | --- | --- | --- | --- | --- | --- |
| Release | instanced | above | 0.23 ms | 0.24 ms | 0.81 ms | 1.00 ms |
| Release | instanced | inside | 0.21 ms | 0.22 ms | 0.25 ms | 0.30 ms |
| Release | instanced, 4 shadow cascades | above | 0.35 ms | 0.36 ms | 1.60 ms | 1.92 ms |
| Release | instanced, 4 shadow cascades | inside | 0.35 ms | 0.37 ms | 1.13 ms | 1.46 ms |
| Debug | instanced | above | 4.45, 4.62 ms | 4.49, 4.44 ms | 0.88, 0.99 ms | 1.19, 1.09 ms |
| Debug | instanced | inside | 4.26, 4.25 ms | 4.20, 4.19 ms | 0.27, 0.31 ms | 0.36, 0.33 ms |
| Debug | instanced, 4 shadow cascades | above | 5.75, 5.65 ms | 5.63, 5.68 ms | 1.95, 1.95 ms | 2.29, 2.36 ms |
| Debug | instanced, 4 shadow cascades | inside | 5.86, 5.70 ms | 5.67, 5.67 ms | 1.40, 1.45 ms | 1.54, 1.44 ms |

(Medians of 300 frames in Release and 60 in Debug; two numbers: two runs taken in turn.)

What it says: the CPU pays nothing; the material's pipelines are made once, and the passes switch
pipelines only where a material has its own. The GPU pays for the function at every vertex of every
pass that draws the mesh: about 0.2 ms more for the field's 1.8 million vertices in the scene pass
with all of it in view, and as much again across the four shadow cascades, about a tenth of a
millisecond a million vertices a pass on this GPU. A field of grass, whose vertices are a few
dozen a copy, costs a few hundredths of that.

## Later

- More than four numbers a vertex, as a second attribute, when a use needs them.
- glTF's own custom attributes (`_CUSTOM`) read into the channels on import.
- Half precision for the colour, UV and normal too, which would take the vertex below its old size.
- A world's wind (direction, strength, gusts) as view constants every swaying material reads,
  beside the time.
- Bounds measured rather than declared, e.g. the function run on the CPU over a mesh's vertices,
  or a debug view that shows a vertex past its material's Max Vertex Offset.
- The previous frame's offset, for motion vectors, when Nyx gets temporal effects.
