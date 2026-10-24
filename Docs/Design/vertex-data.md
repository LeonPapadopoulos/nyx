# Vertex data

Numbers of a game's own that reach the materials: a mesh's for each vertex, an instance's or an
entity's for each draw, and the time. The code is in `Engine/Source/Runtime/Core` (`MeshData.h`),
`Engine/Source/Runtime/Render` (`Mesh.h`, `MeshInstanceCache`, `VulkanRenderer`),
`Engine/Source/Runtime/Scene` (`MeshInstancesComponent`, `MeshRendererComponent`) and
`Engine/Shaders/Library/nyx/surface.slang`.

What it is for, as examples: a sway in the wind (how far each vertex bends, a phase for each copy
of a plant), something that grows (the ages at which each vertex appears and is fully grown, and
how old each copy is), a tint or a wear for each copy. Nyx doesn't know what the numbers mean; the
material that reads them does.

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
  buffer instead (decals, the editor's passes) gets zero.
- **Pipelines read what their vertex shader reads** (`InputLocations()` in `VulkanMaterials`), as
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

## Later

- More than four numbers a vertex, as a second attribute, when a use needs them.
- glTF's own custom attributes (`_CUSTOM`) read into the channels on import.
- Half precision for the colour, UV and normal too, which would take the vertex below its old size.
