# Sparse fields

A number or a vector in every cell of a grid over the world, kept only where something happens,
at a cost with a bound, the same on every machine. Built: `SparseField<T>` (`Engine/Source/Runtime/Sim`,
`SparseField.h`), for `float` and `glm::vec3`, and `NyxSparseFieldTests`.

## What it is

- **Cells in bricks.** Cells of `CellSize` (0.5 m by default) in bricks of 8 x 8 x 8 (a 4 m cube
  at 0.5 m, 2 KB a brick for a number, 6 KB for a vector, twice that with the second buffer).
  Bricks exist only where they were asked for (`Allocate(box)`); outside every brick the field has
  its **ambient** value, and that is what `Sample()` and `GetCell()` give there.
- **Generic.** No idea what the values mean: a temperature, a concentration, a wind. The ambient,
  every rate and when to step are the caller's. A game keeps one field per channel, e.g. a number
  and a vector, allocated with the same boxes, so they have the same bricks.
- **Operations**, each over every brick:
  - `AddSource(box, amount, weight)`: spreads `amount` over the cells whose centres are in the box
    (a box smaller than a cell: the cell its centre is in), in proportion to a weight of the
    cell's centre (1 for each without), so the sum added is the same whatever the box's size. Cells
    in no brick lose their share; it returns the part that found a brick, so the caller can tell it
    was short.
  - `Diffuse(diffusivity, dt)`: explicit, the six neighbours, d/dt = D laplacian; the ambient flows
    in from outside the bricks. Stable at any step: a step past the explicit limit (h^2 / 6D) is
    taken in as many equal parts as it needs, so a long step costs more, never blows up.
  - `Decay(rate, dt)`: towards the ambient, an implicit step, `1 / (1 + rate dt)`: about
    `exp(-rate dt)` for short steps, stable at any, and only correctly rounded arithmetic.
  - `Advect(velocity, dt)`: semi-Lagrangian: each cell takes the value sampled where its centre was
    `dt` ago. Stable at any step; smooths a little (linear interpolation). What would come from
    outside the bricks is the ambient; what leaves them is lost, so the caller allocates downwind.
    A velocity field on the same cells is read cell by cell; any other is sampled. A field may
    carry itself.
  - `Sample(position)`: trilinear between the eight cell centres round the point, the ambient for
    cells in no brick, so values blend into the ambient at the bricks' sides.
  - `ResetToAmbient()`, `ForEachCell()`: for a field made again from scratch each step, such as a
    wind prescribed by its sources, and anything else a caller writes cell by cell.
  - `FreeSettled(tolerance)`: frees the bricks not asked for this round whose every value is within
    the tolerance of the ambient.

## A cost with a bound

- **A hard cap**, `MaxBricks` (64 by default: 32 768 cells). No more bricks ever exist, so a step
  of the field costs at most the cap's cells' work, whatever asks for more.
- **Rounds of requests.** A caller asks for bricks each step: `ClearRequests()`, then `Allocate()`
  for each box, in the order it gives priority. Bricks asked for this round are *requested*;
  requested bricks are never dropped or freed.
- **Who gets dropped.** At the cap, a new brick takes the place of the *least important* brick not
  requested this round: the one whose values are all nearest the ambient (the least largest
  deviation), ties going to the last in the bricks' order. Its values are lost: it holds the least,
  so losing it changes the field least. With every brick requested, a new one is refused: within a
  round, bricks go to the calls in the order they are made, nearest a box's centre first, so the
  caller's order is its priority. `Allocate()` returns how many of a box's bricks were refused, so
  the caller can say what was left short.
- **Measured** (`NyxSparseFieldTests`, a number field and a vector field at the cap of 64 bricks
  each, 32 768 cells; Core i7-12700K, other builds running meanwhile), one step's average:

  | | Release | Debug |
  | --- | --- | --- |
  | `AddSource()`, a 4 m box | 0.002 ms | 0.02 ms |
  | `Diffuse()`, one part | 0.10 to 0.15 ms | 0.7 to 1.3 ms |
  | `Advect()` by a vector field on the same bricks | 0.40 to 0.55 ms | 13 to 19 ms |
  | `Decay()` | 0.01 ms | 0.06 to 0.14 ms |
  | The vector field made again cell by cell (`ResetToAmbient()`, `ForEachCell()`, the caller's own lambda) | 0.25 to 0.28 ms | 5 to 7 ms |
  | **A step: sources, diffusion, advection, decay** | **0.5 to 0.7 ms** | 15 to 20 ms |

  About 15 to 22 ns a cell for the whole step in Release, so the cap's cost is linear in it: 32 bricks
  cost half. Run every fourth step of 60 a second, that is 0.15 ms a step on average. Diffusion
  past the explicit limit (`CellSize^2 / (6 D)`) costs one more part for each time it is passed.
- **Where the time goes:** `Advect()` and `Diffuse()` copy each brick with a cell of its neighbours'
  values round it (10 x 10 x 10, the ambient where there is no neighbour), so no cell asks where its
  neighbours are; a sample further than a cell outside the brick (a velocity of more than a cell a
  step) falls back to looking the cells up.

## Deterministic

- **Bricks sorted by coordinate** (z, then y, then x) and always walked in that order; a brick's
  slot in memory never decides an order. Lookups are binary searches in the sorted list, and each
  brick keeps the slots of its 26 neighbours, made again whenever bricks come or go; no hash map is
  ever walked.
- **Double buffered:** `Diffuse()` and `Advect()` read only the values from before the step and write
  the new ones into each brick's second buffer, then the buffers swap, so the result doesn't depend
  on the order cells or bricks are visited in, and spreading bricks over threads later changes
  nothing. `AddSource()` and `Decay()` touch one cell at a time.
- **Correctly rounded arithmetic only:** no `exp`, `sin` or `pow` of a C library, so the same steps
  give the same values to the bit on any machine running the same build.
- Under the cap, the order boxes are asked for in makes no difference: the bricks and every value
  come out the same. At the cap it does, by design (the order is the priority).

## Later

- Threads over bricks: the double buffers already allow it.
- A debug view of the bricks and their values in the game's debug drawing.
- Bricks of other sizes, or several channels in one brick set, if a game needs them.
