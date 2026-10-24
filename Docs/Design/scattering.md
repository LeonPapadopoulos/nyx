# Scattering

How Nyx places many copies of one mesh over a surface, such as plants over a ground, and why. The
code is in `Engine/Source/Runtime/Scene` (`ScatterComponent.h`, `ScatterBlockerComponent.h`,
`ScatterInstances.h`, `MeshInstancesComponent.h`); the tests are `NyxScatterTests`.

## The shape of it

- **A scatter is an entity with a mesh and a `ScatterComponent`.** The entity's *Mesh Renderer*
  (or its `RuntimeMeshComponent`) says what is drawn and with which materials; the scatter says
  where: on which surface, over what area, how densely, within which limits and masks, scaled and
  turned how. One scatter, one mesh: a meadow of three kinds of plants is three scatters on the
  same ground.
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
- **Blockers are entities with a `ScatterBlockerComponent`**, such as a crate, a ledge or a tree's
  trunk: every scatter keeps its copies out of them ([Blockers](#blockers)).
- **Everything is in the scatter entity's space:** the area is around it, heights are along its Y,
  slopes against its up, the noise mask over its X and Z. The surface's triangles are brought into
  that space first (`inverse(scatter) * surface`), normals by the inverse transpose.

## Sampling, cell by cell

`ScatterOnMesh()` is a pure function of the values, the surface's `MeshData`, the matrix between
the two spaces and the blockers. The area is cut into square cells of *Cell Size* (the last ones cut
off at the area's edge), and each cell is sampled on its own:

1. The surface's triangles are cut into the cells: a triangle inside one cell goes in whole, one
   across a cell's edge is clipped to the cell's square (Sutherland and Hodgman, normals carried
   along) and fanned into pieces. Each cell keeps its pieces and their areas added up.
2. Density times the cell's area of surface points are thrown, the fraction rounded up or down by a
   hash of the cell's own. Each picks a piece by its area (a binary search in the running sums) and
   a point on it, evenly over its area (`sqrt` of one number for the barycentric weights), with its
   normal between the piece's vertex normals. Every number of a point is a hash of the seed, the
   cell's column and row, the point's number and which number it is.
3. A point is kept if the slope and height limits allow it, a hash of its own is below the density
   the masks leave there ([The noise mask](#the-noise-mask), [Blockers](#blockers)), and it is at
   least *Spacing* from every point kept before in the cell (a grid of squares the spacing a side
   over the cell, so each test looks at nine squares). This is dart throwing: a cheap Poisson disc.
   Once the darts fill the surface, more density gives no more instances: the spacing wins.
4. Then the spacing across the cells' edges: of two points too close in two cells, the one of the
   later cell (by rows, then columns) goes. A cell looks at the points the cells before it *threw*
   (step 3), not those they kept in the end, so it depends on its neighbours' throws, never on a
   chain of cells. Now and then a point near an edge goes for a neighbour that went itself: a
   little fewer along the edges, never two too close.
5. A kept point gets its copy: scaled within *Scale Range*, turned about its up by up to *Yaw
   Range*, its up tilted from the entity's Y towards the normal by *Align To Normal*, and sunk
   *Sink Depth* along its up.
6. The cells' copies go one cell after another, each non-empty cell with the bounds of its origins
   and its range of transforms.

Because a cell's pieces are its own share of the surface, the count is density times the surface's
area inside the area, before limits, masks and spacing, with no points wasted outside it. Each cell
keeps at most its share of `MaxScatterInstances` (about a million) and throws at most four times
that, so a mistyped density can't hang the editor, and no cell's count depends on another's.

So a cell's copies are a function of the values, the surface's pieces in it, the blockers that
reach it and the throws of the cells before it within the spacing: the same whether one cell is
made or all of them, which is what makes local edits possible ([Making only what changed](#making-only-what-changed)).
The cell size, which also sets the renderer's culling, therefore also changes where the copies are,
not only how they are grouped.

## The noise mask

*Noise Mask* on the scatter thins the copies in patches, so one scatter makes drifts with soft,
natural edges, or clearings when inverted, where three square scatters overlapping would show their
corners. At a point of the scatter's X and Z:

- the noise is three layers of gradient noise (`SampleGradientNoise()`, the same noise the ground's
  heights are made from), each twice as fine and half as strong, the first with *Noise Frequency*
  patches a metre, seeded by *Noise Seed*; scaled to about 0 to 1, as much above 0.5 as below
  (`SampleScatterNoise()`);
- the density it leaves is 0 below *Noise Threshold* minus half the *Noise Softness*, 1 above it
  plus half, smooth between (`GetNoiseMaskDensity()`); *Noise Invert* gives one minus that. At a
  threshold of 0.5 it keeps about half the area; with no softness the mask and its inverse share
  the copies out exactly between them.

The density multiplies: a point is kept when a hash of its own, from 0 to 1, is below it, so the
mask thins the density rather than moving copies. As every density, it only shows where the
spacing doesn't already fill the surface.

**A painted mask later** is one more factor in that product (`ScatterDensity` in
`ScatterInstances.cpp`): a texture over the area, sampled at the point's X and Z, saved as an asset
and named on the scatter. Its revision goes into the values' hash, and a stroke can make again only
the cells it painted, as a blocker does. None is built yet.

## Blockers

A *Scatter Blocker* keeps every scatter's copies away from its entity:

- **Its shape:** with *Shape* at *Collider*, the entity's box, sphere or capsule collider, scaled
  the way the physics scales it (a box's half extents by the scale, a sphere's radius by its largest
  axis, a capsule's radius by the larger of X and Z and its middle by Y), and turned with the
  entity; without one, or with *Shape* at *Circle*, a circle of *Radius* (scaled as a capsule's)
  round the entity, standing along the scatter's up, as round a post or a trunk. A mesh collider
  counts as none: its triangles would cost a scan a point.
- **Its margin and falloff:** no copy within *Margin* of the shape (by the distance in the world,
  in three dimensions for the colliders, across the scatter's plane for the circle). Past it, the
  density rises from 0 to 1 over *Falloff* metres, smoothly, instead of stopping hard.
- **Every scatter keeps to every blocker of the world**, unless it says not to (*Use Blockers* off),
  e.g. a moss that grows right up to the stones. Each blocker's densities multiply in the
  blockers' order (by guid), so it is the same wherever a cell is made.

`GatherScatterBlockers()` gives the blockers as one scatter sees them: each shape's matrix from the
scatter's space into the shape's, in metres; the square of the scatter's X and Z it reaches, margin
and falloff with it, outside which its density is exactly 1; and a hash of everything it is made
from. A cell is tested only against the blockers whose square reaches it. Each blocker is taken to
the centimetre, its rotation to about a tenth of a degree (each number of the quaternion to a
1024th) and its scale to a thousandth: a drag that moves it less than that makes nothing again, and
since the full scatter rounds it the same way, what is made never depends on how it got there.

## Made in the post-load step, followed each frame

As every generated content in Nyx ([Architecture: Generated content](../Architecture.md#generated-content)):

- The scatter's post-load step (`UpdateScatter()`) makes the instances on its entity, on load, on
  every edit, undo and redo in the editor and on every live edit in a game. Its remove step takes
  them away. They are never saved.
- It keeps a hash of what it made them from (`ScatterComponent::MadeFrom`): its values, whether it
  stood on a mesh, the plane or nothing, the surface mesh's revision, the matrix between the
  two entities, and each blocker's hash. A weak pointer to the mesh (`MadeOn`) tells it apart from
  another mesh that takes the same address. For the same, it makes nothing again.

What is new is that a scatter's content depends on *other* entities, which a post-load step may
read but never change, and whose own steps run in an order nobody fixes: a scene's entities are
loaded in file order, and an edit to the ground runs the ground's step alone. So the scatter
follows its surface and the blockers as the renderer and the mesh colliders follow a mesh, by its
revision: `UpdateScatters()` runs `UpdateScatter()` for every scatter once a frame, in the editor
(`EditorLayer::OnUpdate()`) and in the game (`GameLayer::OnLateUpdate()`, after the steps and the
live edits, also while paused). When nothing changed, that is a guid lookup, a look at each blocker
and a hash of about a hundred bytes a scatter and a blocker. When something changed, the scatter is
made again in the same frame:

- a scatter loaded before its surface gets no instances in its step, and the first frame finds the
  surface's mesh and scatters on it; blockers loaded after it are found the same way;
- an edit to the ground makes a new mesh in the ground's step, and the next frame scatters again;
- a blocker added in the editor takes its copies away the next frame; dragged, removed or undone,
  the same;
- a surface that loses its mesh, or is deleted, leaves the scatter with no instances; its entity is
  then drawn zero times, not once.

Instances are always set, empty or not, once the step ran, so a scatter's mesh is never drawn
alone at its origin while it waits for its surface.

## Making only what changed

The scatter keeps its cells as last made (`ScatterComponent::Cells`, a `ScatterCells`: each cell's
pieces and their hash, the points it threw, those it kept, and the blockers it was made with), and
`UpdateScatterCells()` makes again only what an edit reaches:

- **Other values** (the area, the density, the mask, anything on the scatter): every cell.
- **A blocker added, removed, edited, or moved past its rounding:** the blockers before and after
  are matched by their hashes, and only the cells reached by one that is in one list and not the
  other, before or after, are thrown again.
- **The surface changed** (a new revision, or it or the scatter moved): its triangles are cut into
  the cells again, which costs a pass over the triangles but no sampling, and only the cells whose
  pieces' hash changed are thrown again. A ground made again with the same values, or a mesh edited
  in one place, touches only those cells; moving the surface or the scatter changes every piece, so
  every cell.
- Then the cells after each one thrown, within the spacing, keep apart from it again (step 4 of
  the sampling), which looks at points only.

The cells that weren't thrown keep their transforms as they were, to the bit, and what the update
makes is exactly what a full scatter of the same values, surface and blockers makes: each cell is
the same function of the same inputs either way. The new instances are the cells' copies put
together again, a new `MeshInstances`, so the renderer still uploads every transform of the
scatter once for each change (a few megabytes for 40,000); uploading only the changed cells would
be a renderer change. A component that has no cells yet, just loaded or added, makes all of them; a
copy that shares another's copies them before they change.

## The same on every machine

The ground's rule (`SampleGroundHeight()`, [Architecture: Generated content](../Architecture.md#generated-content))
holds here too. Each point's numbers are integer hashes of the seed, its cell, its number and which
number it is (`HashCell()`, `HashPoint()`), turned into floats by their top bits; there is no `std::`
random engine or distribution, whose results differ between standard libraries. The rest is
additions, multiplications, divisions, `floor`, `round` and `sqrt`, which IEEE 754 rounds the same
everywhere, in a fixed order: the clipping, the noise (from the ground's integer lattice hashes),
the distances to the blockers, the rounding of their places. Sines and cosines, for the yaw and for
the slope limits' cosines, come from their series (`GetSineCosine()`), not the C library.
`NyxScatterTests` checks a hash of the transforms and one of the noise against those recorded when
they were written, in Debug and Release alike.

Each point's numbers depend on its cell and its number only, so the same values on the same mesh
give the same instances, and a change in one cell moves only that cell's points (and, through the
spacing, can take a point or two off the edges of the cells after it).

## Cost

Scattering about 50,000 instances on the default 128 m ground (32,768 triangles, density 4, spacing
0.25 m, slopes up to 35 degrees; 48,583 instances in 64 cells) takes about 210 ms in a Debug build
and 20 ms in Release (`NyxScatterTests` prints it). Making the ground's mesh itself takes about
21 ms in Debug.

Dragging a blocker (a circle of 1 m, margin 0.2 m, falloff 0.5 m, 5 cm a frame) over about 40,000
copies on the default ground (116 m, otherwise the same; 64 cells of 14.5 m): about 9 ms a frame in
Debug, making 2 of the 64 cells again, against about 150 ms a frame before the cells were kept, when
every change scattered all of it, and about 185 ms for a full scatter now. In Release, about 1 ms a
frame against 15 ms. Smaller cells make a drag cheaper still, at the cost of more cells for the
renderer to cull.

## Later

- **Painted masks**: a texture over the area, one more factor of the density
  ([The noise mask](#the-noise-mask)).
- **Mesh assets as surfaces**, once meshes are assets that keep their data.
- **Uploading only the changed cells** to the GPU, now that only they change.
- **Distance**: fewer copies far away (by cell, as the renderer culls) and impostors.
