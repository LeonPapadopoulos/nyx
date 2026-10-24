# Scattering

How Nyx places many copies of one mesh over a surface, such as plants over a ground, and why. The
code is in `Engine/Source/Runtime/Scene` (`ScatterComponent.h`, `ScatterInstances.h`,
`MeshInstancesComponent.h`); the tests are `NyxScatterTests`.

## The shape of it

- **A scatter is an entity with a mesh and a `ScatterComponent`.** The entity's *Mesh Renderer*
  (or its `RuntimeMeshComponent`) says what is drawn and with which materials; the scatter says
  where: on which surface, over what area, how densely, within which limits, scaled and turned how.
  One scatter, one mesh: a meadow of three kinds of plants is three scatters on the same ground.
- **The copies are a `MeshInstancesComponent`:** a shared list of transforms, each relative to the
  entity's `TransformComponent`, grouped into cells with the bounds of their origins, and a
  revision. The renderer draws the entity's mesh once per transform instead of once, and culls a
  cell whole. The component knows nothing of scattering: whatever makes instances, a scatter, a
  tool or a game's system, fills it the same way (`SetMeshInstances()`).
- **The surface is another entity, named by guid** (`ScatterComponent::Surface`, picked in the
  details panel like any entity reference), and what is scattered on is its run-time mesh
  (`RuntimeMeshComponent`): a ground's, a generated mesh's, anything. There is no ground-specific
  code; a ground is only the main use. Without a surface, the scatter is on the flat plane through
  its entity, so one added in the editor shows something at once. A mesh loaded by its path is
  only on the GPU (`Nyx::Mesh` keeps no `MeshData`), so it can't be a surface yet; when meshes
  become assets that keep their data, they can be.
- **Everything is in the scatter entity's space:** the area is around it, heights are along its Y,
  slopes against its up. The surface's triangles are brought into that space first
  (`inverse(scatter) * surface`), normals by the inverse transpose.

## Sampling

`ScatterOnMesh()` is a pure function of the values, the surface's `MeshData` and the matrix between
the two spaces:

1. The triangles that reach into the area are listed with their areas added up.
2. Density times that area points are thrown. Each picks a triangle by its area (a binary search
   in the running sums) and a point on it, evenly over its area (`sqrt` of one number for the
   barycentric weights), with its normal between the triangle's vertex normals.
3. A point is kept if it lies in the area, the slope and height limits allow it, and it is at least
   *Spacing* from every point kept before (a grid of squares the spacing a side over the area, so
   each test looks at nine squares). This is dart throwing: a cheap Poisson disc. Once the darts
   fill the surface, more density gives no more instances: the spacing wins.
4. A kept point gets its copy: scaled within *Scale Range*, turned about its up by up to *Yaw
   Range*, its up tilted from the entity's Y towards the normal by *Align To Normal*, and sunk
   *Sink Depth* along its up.
5. The copies are sorted into square cells of *Cell Size* over the area (a counting sort, so each
   cell's copies stay in the order they were kept), and each non-empty cell gets the bounds of its
   origins and its range of transforms.

The number of points thrown is density times the area of the triangles that reach into the area,
and those partly outside lose the points that fall outside: so the count is about density times
the surface's area inside the area, before limits and spacing. At most `MaxScatterInstances`
(about a million) are kept and four times as many thrown, so a mistyped density can't hang the
editor.

## The same on every machine

The ground's rule (`SampleGroundHeight()`, [Architecture: Generated content](../Architecture.md#generated-content))
holds here too. Each point's numbers are integer hashes of the seed, its number and which number it
is (`HashPoint()`), turned into floats by their top bits; there is no `std::` random engine or
distribution, whose results differ between standard libraries. The rest is additions,
multiplications, divisions, `floor` and `sqrt`, which IEEE 754 rounds the same everywhere, in a
fixed order. Sines and cosines, for the yaw and for the slope limits' cosines, come from their
series (`GetSineCosine()`), not the C library. `NyxScatterTests` checks a hash of the transforms
against the one recorded when it was written, in Debug and Release alike.

Each point's numbers depend on its number only, so the same values on the same mesh give the same
instances. A change to the surface anywhere moves every point, though, since the triangle a number
picks depends on all the areas before it. Stable placement under local edits (seeding each cell on
its own) can come when painting or local edits need it.

## Made in the post-load step, followed each frame

As every generated content in Nyx ([Architecture: Generated content](../Architecture.md#generated-content)):

- The scatter's post-load step (`UpdateScatter()`) makes the instances on its entity, on load, on
  every edit, undo and redo in the editor and on every live edit in a game. Its remove step takes
  them away. They are never saved.
- It keeps a hash of what it made them from (`ScatterComponent::MadeFrom`): its values, whether it
  stood on a mesh, the plane or nothing, the surface mesh's revision and the matrix between the
  two entities. A weak pointer to the mesh (`MadeOn`) tells it apart from another mesh that takes
  the same address. For the same, it makes nothing again.

What is new is that a scatter's content depends on *another* entity, which a post-load step may
read but never change, and whose own step runs in an order nobody fixes: a scene's entities are
loaded in file order, and an edit to the ground runs the ground's step alone. So the scatter
follows its surface as the renderer and the mesh colliders follow a mesh, by its revision:
`UpdateScatters()` runs `UpdateScatter()` for every scatter once a frame, in the editor
(`EditorLayer::OnUpdate()`) and in the game (`GameLayer::OnLateUpdate()`, after the steps and the
live edits, also while paused). When nothing changed, that is a guid lookup and a hash of about a
hundred bytes a scatter. When the surface's mesh has a new revision, or either entity moved, the
scatter is made again in the same frame:

- a scatter loaded before its surface gets no instances in its step, and the first frame finds the
  surface's mesh and scatters on it;
- an edit to the ground makes a new mesh in the ground's step, and the next frame scatters again;
- a surface that loses its mesh, or is deleted, leaves the scatter with no instances; its entity is
  then drawn zero times, not once.

Instances are always set, empty or not, once the step ran, so a scatter's mesh is never drawn
alone at its origin while it waits for its surface.

Moving a scatter or its surface with the gizmo scatters again every frame of the drag: fine in
Release at the timings below, slow in a Debug build for a scatter of tens of thousands.

## Cost

Scattering about 50,000 instances on the default 128 m ground (32,768 triangles, density 4, spacing
0.25 m, slopes up to 35 degrees; 48,664 instances in 64 cells) takes about 200 ms in a Debug build
and 20 ms in Release (`NyxScatterTests` prints it). Making the ground's mesh itself takes about
21 ms in Debug.

## Later

- **Masks** beyond slope and height: a painted map over the area, room kept around other entities
  (paths, houses, trees), a noise that thins the scatter in patches. Each is one more test on a
  point, next to the limits (`ScatterLimits::Accepts()` in `ScatterInstances.cpp`), given the
  point's position and normal in the scatter's space; a mask that names other entities is found
  by guid and followed each frame as the surface is, its revision or transform in the hash.
- **Mesh assets as surfaces**, once meshes are assets that keep their data.
- **Stable placement** under local edits, by seeding each cell on its own.
- **Distance**: fewer copies far away (by cell, as the renderer culls) and impostors.
