# UI painter

Plan written 2026-10-12. The painter of `Design/editor-ui-architecture.md`, section 3.10.1; read that first. Reference: Wessel Mast, "UIPainter: Tile-Based UI Rendering in One Draw Call" (Guerrilla, GDC 2024).

The **painter** draws 2D shapes as signed distance fields. Every shape stays a shape until the pixel shader, so edges are anti-aliased exactly at any scale or zoom, and shadows and glows cost almost nothing.

- **Stage 1** (below, done): the painter in the engine, drawing into its own image, shown in the Painter Test window.
- **Stage 2**: painting inside ImGui windows, and the first `Nyx::UI` widgets drawn with it.
- **Stage 3**: the whole editor painted (decision: `Design/editor-ui-architecture.md`, 3.10.2), with ambilight and glass.

# Stage 1

## Done when

- A **Painter Test** window (Debug menu) shows a test sheet with every primitive: rects, rounded rects with different radii per corner, circles, arcs and circle sectors, lines of several widths and angles, triangles, text and images.
- The sheet is crisp at 100%, 150% and 400% zoom, and at fractional zooms in between. Next to it, the same shapes drawn with `ImDrawList` show the difference.
- Shadows and glows work per command and per primitive group: a group of shapes casts one shadow and glows as a whole.
- A heatmap view colours each 8×8 tile by the number of commands it holds.
- The window shows statistics: commands, tiles in use, commands per tile (average and maximum), CPU binning time.
- `NyxPainterTests` passes: command recording, tile binning, the line tile walk, clipping, limits.

## Where it lives

- `Engine/Source/Runtime/Renderer`:
  - `Painter.h/.cpp`: the API and the draw commands. Plain C++, no Vulkan, so it's testable without a GPU and usable by the game.
  - `PainterTiles.h/.cpp`: tile binning.
  - `VulkanPainter.h/.cpp`: buffers, the pipeline, and drawing a painter's commands into a target.
- `Engine/Shaders/Painter.vert` and `Painter.frag`, added to the list in `Engine/Shaders/CMakeLists.txt`.
- `Engine/Source/tests/PainterTests.cpp`: `NyxPainterTests`.
- `VulkanRenderer` owns one `VulkanPainter`, the way it owns the outline and grid passes. It doesn't know what is painted.
- Names don't collide with Windows or C runtime headers (handoff, learnings).

## Changes

### 1. Draw commands and the painter API

- `Nyx::Engine::Painter` records commands for one frame and one target:
  - `AddRect`, `AddRoundedRect` (radius per corner), `AddCircle`, `AddArc` and `AddCircleSector` (start and end angle, inner and outer radius), `AddLine` (width, round or flat caps), `AddTriangle`;
  - filled or outlined (outline width), where the shape allows both;
  - `AddText` and `AddImage` arrive in change 4; the command types are reserved here.
- **Header of every command**, as in the talk:
  - type and flags;
  - colour, stored as premultiplied RGBA8;
  - clip rect;
  - shadow (colour, offset, blur radius) and glow (colour, distance, strength);
  - primitive group;
  - a fill mode field, always "solid" in stage 1, kept for gradients later.
- **State:**
  - `PushClipRect`/`PopClipRect`, intersected with the current one;
  - `BeginGroup(shadow, glow)`/`EndGroup()`: the commands in between share one shadow and glow;
  - `PushTransform(scale, offset)`/`PopTransform()`: scale and translation only, applied on the CPU when a command is added. Canvases such as a node graph or the test sheet zoom with it.
- **Units:** pixels of the target, origin top left. Callers scale for DPI; the painter doesn't know about monitors.
- **Layout on the GPU:** every command is one fixed-size record (a few `vec4`s), with its payload per type in the last ones. A `static_assert` on the C++ side and a comment on the GLSL side keep the two in step. Fixed size keeps the shader simple; variable-length commands are left for later.
- Tests: recording, the header each command gets, clip and transform stacks, groups (nesting a group is an error, reported once).

### 2. Tile binning

- The target is split into 8×8 pixel tiles. Eight by eight matches GPU wave sizes (talk: 32 or 64 threads per wave), so all pixels of a wave walk the same command list.
- Each command goes into every tile its **coverage bounds** touch: the shape's bounds, grown by one pixel for anti-aliasing and by its shadow and glow reach, then cut by its clip rect. A command that ends up with no tiles is dropped.
- **Lines** walk their tiles along the line (a Xiaolin Wu–style walk widened by the line's width, as in the talk) instead of filling their bounding box. A diagonal line touches a thin band of tiles, not a square.
- **Tile buffer:** per tile an offset and a count into one list of command indices, built with two passes (count, then fill). Commands keep their order within a tile, since the shader blends in order.
- **Groups:** a group's shadow and glow reach is the group's bounds, so every tile near any shape of the group knows the whole group. Otherwise a shadow would be cut off at the edge of the shape that happens to be in the tile.
- **Limits:** a frame's commands and tile entries have a budget. Past it, further commands are dropped and a warning is logged once per frame, never a crash or a huge allocation.
- Tests: rects and circles land in exactly the expected tiles; clipped and off-target commands are dropped; a 45° line touches far fewer tiles than its bounding box; order within a tile is kept; budgets hold.

### 3. Shader and Vulkan pass

- `VulkanPainter::Draw(commandBuffer, painter, target)`:
  - uploads commands and tiles into storage buffers, one set per frame in flight, grown when needed;
  - draws one full-screen triangle (like `OutlineComposite.vert`) with dynamic rendering into the target;
  - the target's size and the tile grid size come in a push constant.
- **Fragment shader**, per pixel:
  1. Find the pixel's tile and its command list.
  2. Start from transparent black and blend the commands in order, in registers.
  3. Per command, compute the signed distance in pixels, and coverage as `clamp(0.5 - distance, 0, 1)`, which gives one pixel of anti-aliasing.
  4. A group first gets its shadow and glow from the union of its shapes (the minimum distance), under its fills; then its fills blend in order.
  5. Write premultiplied colour once. Fixed-function blending (`One`, `OneMinusSrcAlpha`) puts the result over what the target already holds.
- **Distance functions:** rounded box (radius per corner), circle, arc and circle sector, line segment with width, triangle. Outlines are `abs(distance) - width / 2`.
- **Colour space:** the swapchain is `B8G8R8A8Unorm` with sRGB presentation (`VulkanSwapchain.cpp`), so ImGui blends sRGB-encoded values directly. The painter does the same, so painted and ImGui-drawn colours match side by side. Its offscreen target in the test window uses a UNORM format too.
- The pass skips pixels whose tile is empty early. This matters for stage 2, where most of a window is not painted.

### 4. Text and images

- **Text:** `AddText(font, size, position, colour, text)` makes one glyph command per character, using ImGui 1.92's baked glyphs for that size (`ImFont::GetFontBaked(size)`, `FindGlyph`). Glyphs sample ImGui's font atlas texture, so text looks the same as ImGui's. One text system, no copy of the atlas.
  - The painter's pipeline layout takes the atlas through the same descriptor set layout the ImGui Vulkan backend uses for its textures.
  - In stage 1 the test sheet is drawn before ImGui renders the frame, so a glyph baked for the first time this frame appears one frame later. Stage 2 draws during ImGui's rendering, after texture updates, where this goes away.
- **Images:** `AddImage(texture, rect, uv rect, tint)`. A draw binds a small fixed array of textures (e.g. 8); a painter that uses more is split into several draws. A bindless array, or an icon atlas, is left for when icons arrive (overhaul change 9).
- Tests: glyph commands for a string (count, order, advance), and splitting by texture count.

### 5. Painter Test window and debug views

- Debug menu > **Painter Test**, an editor window:
  - draws the test sheet with a `Painter` into an offscreen target and shows it with `ImGui::Image`;
  - zoom (25% to 800%) and pan, through `PushTransform`;
  - a toggle to show the same sheet drawn with `ImDrawList` beside it;
  - the heatmap view (a flag in the push constants: the shader outputs a colour by command count instead of the image);
  - statistics: commands, tiles in use, commands per tile (average, maximum), binning time.
- The test sheet is built by a function in the engine (`DrawPainterTestSheet(Painter&)`), so stage 2 and NyxGame can draw it too.
- Wrap the window's UI calls in `NYX_UI` and `Nyx::UI::Begin` like every other panel.

## Left for later

- **Stage 2:** painting inside ImGui windows through draw callbacks, in order with overlapping windows, popups and multi-viewports; `Nyx::UI` widgets drawn with the painter.
- Gradient fills, images masked by a shape, convex polygons, true bezier curves, the custom command, glass (architecture doc, 3.10.1).
- Binning on the GPU, if CPU binning becomes the bottleneck (the talk's next step).
- A source id per command, so F8 can name the code behind a painted pixel.
- Using the painter in NyxGame (debug overlays, the frame-time graph).
- Comparing painter output with reference images in an automated GPU test.

## As built

### 1. Draw commands and the painter API

Built and tested on Windows 2026-10-12 (Visual Studio 2022, Debug): `NyxPainterTests` passes, and so do the other 14 tests in `ctest -C Debug`.

- **Two headers instead of one**, so the GPU format can be read on its own:
  - `PainterCommands.h`: what the shader reads. `PainterCommand` (80 bytes), `PainterGroup` (32 bytes), one payload struct per command type, colour packing and `GetShapeBounds()`. A byte table in the comments is the whole format; `static_assert`s keep the struct to it.
  - `Painter.h`: the API that records them (`PainterStyle`, `PainterEffects`, `PainterTransform`, `Painter`).
  - `PainterCommands.h` starts with a **checklist for adding a primitive**: type, payload, `Add...()` function, bounds, shader distance function, test.
- **Shadow and glow belong to groups only.** The command header holds a group index instead of shadow and glow values. A shape with effects of its own becomes a group of one, so the shader handles effects in one place, and the command stays 80 bytes.
- **Header layout:** Type, Flags, Color, Group | ClipRect | OutlineWidth, FillMode, Texture, Unused | 32 bytes of payload. Colours are premultiplied RGBA8, red in the lowest byte, as GLSL's `unpackUnorm4x8()` reads them.
- **Fewer types than primitives:** `AddRect` and `AddRoundedRect` both record a `Rect` (radius 0 is a sharp corner); `AddArc` records a `CircleSector` as wide as the arc. Types: Rect, Circle, CircleSector, Line, Triangle; Glyph and Image are reserved for change 4.
- **Payloads** are plain structs copied into the command with `SetPayload()`/`GetPayload()` (`memcpy`), so they can use `glm::vec2` and are read back the same way in tests.
- **`GetShapeBounds()` came forward from change 2:** it is knowledge about each primitive, so it sits with the primitives. Change 2 grows it by anti-aliasing, shadow and glow.
- **Frames:** `BeginFrame(width, height)` and `EndFrame()`. `EndFrame()` closes groups, clip rects and transforms left open.
- **The painter never logs.** Misuse is collected in `GetProblems()`, once per kind and frame, for whoever draws the frame (change 5 shows them). Nested `BeginGroup()` is ignored with its matching `EndGroup()`; a transform with a scale of 0 or less is ignored but still pushed, so the matching `PopTransform()` works.
- **Values are made safe when recorded:** rects, radii and angles given the other way round are swapped; negative sizes become 0; corner radii shrink to half the rect's shorter side; lines drop the outline width.
- **Namespace** `Nyx::Engine`, as the handoff names it; most older renderer files use plain `Nyx`.
- Tests (`NyxPainterTests`): colour packing; recording order and payloads; radius fitting and normalized rects; arcs and sectors; lines without outlines; nested clip rects and an extra pop; transforms (rects, radii, outlines, lines, shadows, nesting, clip rects, a scale of 0); groups, groups of one, glows that reach nowhere; group misuse; `EndFrame()` and the next frame; shape bounds.

### 2. Tile binning

Built and tested on Windows 2026-10-12 (Visual Studio 2022, Debug): `NyxPainterTests` passes with the new binning tests, and so do the other 14 tests in `ctest -C Debug`.

- **`PainterTiles.h/.cpp`:** one function, `BinPainterCommands(painter, budget, tiles)`. Its result, `PainterTiles`, is what the GPU reads: `Tiles` (an offset and a count per tile, 8 bytes, row by row) and `Entries` (command indices). It also holds statistics (`PainterBinningStats`) and problems. Its vectors are reused from frame to frame.
- **Coverage of a command**, worked out once per command (`MeasureCoverage`):
  - its shape bounds, or for a command in a group the group's whole reach;
  - grown by `PainterAntiAliasingReach` (1 pixel);
  - cut by its clip rect and the target. A command with nothing left is counted as culled, which is not a problem.
- **Group reach** (`MeasureGroupReach`): the union of the group's shape bounds, plus the same rect moved by the shadow offset and grown by the blur, plus the rect grown by the glow distance. `PainterGroup::ShadowBlur` now says that the shadow reaches no further than the blur, since binning relies on it; the shader in change 3 must keep to that.
- **Lines walk row by row, not with Xiaolin Wu's algorithm.** For each row of tiles, the part of the line between the row's top and bottom (moved out by half the width plus anti-aliasing) gives the row's x range. This is simpler to read than a Wu-style walk, provably never misses a pixel, and gives about three tiles per row for a 45° line: under 400 tiles instead of 10,000 for an 800-pixel diagonal. Lines in a group cover the group's reach instead, since its shadow and glow spread around the whole group.
- **Two passes**, as planned: count per tile (with the budget check), prefix offsets, then fill in command order. A command is walked three times (count for the budget, count per tile, fill). That keeps the code plain; if binning shows up in the Painter Test statistics, counting rects by their area is the first saving.
- **Budget:** `PainterBudget` (default 65,536 commands and about 4 million tile entries, 16 MB). The first command that doesn't fit is dropped together with every command after it, so what is drawn is always a prefix of the frame, never a frame with holes. One problem line reports how many were dropped.
- **Rect helpers** `Grow()` and `Move()` moved into `PainterCommands.h`, next to `Intersect()` and `Union()`.
- Tests: grid size; exact tiles for a rect and a circle; culling (off target, clipped away, a 0x0 target); clip rects cutting a shadowed shape; drawing order and statistics; lines at 45°, shallow, steep and horizontal, checked by stepping along and across them; groups (a tile only in the shadow holds every shape of the group, glow reach, a line in a group); both budgets; reusing the result for a smaller next frame.

### 3. Shader and Vulkan pass

Built and tested on Windows 2026-10-12 (Visual Studio 2022, Debug): `NyxPainterGpuTests` passes on the GPU with the Vulkan validation layers on and no validation messages, and so do the other 15 tests in `ctest -C Debug`. The editor starts and runs with the painter pass created.

- **`VulkanPainter.h/.cpp`**, two classes:
  - `VulkanPainter`: the render pass, descriptor set layout, descriptor pool (32 targets), pipeline and shaders. `Draw(commandBuffer, painter, target, options)` bins the commands (change 2), uploads them, and draws one triangle.
  - `VulkanPainterTarget`: the painted image (`R8G8B8A8Unorm`, left in `ShaderReadOnlyOptimal`), its framebuffer, four storage buffers (commands, groups, tiles, entries) and a descriptor set. It is empty until first drawn, takes the painter's size, and keeps the last frame's `PainterTiles` for the statistics. Its buffers are host-visible, mapped once, and grow by doubling.
  - `PainterDrawOptions`: clear colour, debug view (`EPainterDebugView::Heatmap`), budget.
- **A render pass, not dynamic rendering.** Stage 2 draws inside ImGui's render pass, which needs pipelines made for a render pass; using one here too keeps one way of doing it. The painter has its own render pass instead of `VulkanViewportTarget`'s, which carries a depth buffer and registers itself with ImGui.
- **One set of upload buffers per target, not per frame in flight.** The renderer has one frame in flight and waits for it before recording the next, so a target's buffers are free again by then. A target is drawn at most once per frame.
- **`Painter.frag`**, in sections: the data layout (structs mirroring `PainterCommands.h` and `PainterTiles.h`), distance functions, blending, `main()`. Its header repeats where a new primitive goes.
  - Distance functions: rounded rect (radius per corner), circle, circle sector (an exact distance for the straight ends, so nearly full rings have no seam), line with round or flat caps, triangle (after Inigo Quilez's exact triangle).
  - Coverage is `clamp(0.5 - distance, 0, 1)`: one pixel of anti-aliasing centred on the edge.
  - A group's shadow and glow are drawn when the first of its shapes appears in the tile, from the union (nearest distance) of all its shapes, under the fills. The shadow fades from the moved shapes' edge to nothing `ShadowBlur` pixels out; the glow is strongest at the edge and fades linearly over `GlowDistance`.
  - Empty tiles `discard`, so unpainted pixels keep what's beneath.
  - The heatmap tints each tile over the painted result (blue for one command, green at 8, red at 16 or more), so shapes stay recognisable under it.
- **Output is premultiplied** and blended with (`One`, `OneMinusSrcAlpha`). The clear colour is given straight, like every painter colour, and premultiplied when clearing. ImGui draws images with straight alpha, so the Painter Test window (change 5) clears to an opaque colour; transparent edges over ImGui would otherwise be darkened.
- **`VulkanRenderer` owns `PainterPass`** (`GetPainterPass()`), created after the other pipelines and shut down before the ImGui backend. Targets belong to their users and are released before that.
- **A GPU test came forward from "Left for later":** `NyxPainterGpuTests` makes a hidden GLFW window and a `VulkanContext`, paints small frames, copies the image to a host buffer and checks pixels. It covers fills and the half-covered pixel at an edge, circles, order and transparency, clip rects, outlines, rounded corners, round and flat lines, triangles, a quarter pie, a ring and a nearly full sector, a hard shadow, one shadow for a group of overlapping shapes, a glow at half its distance, the clear colour, the heatmap, and a target following the painter's size. It needs a Vulkan device, which every machine that runs the editor has.
- **Build:** `Engine/Shaders` is now added before `Engine/Source` in the root `CMakeLists.txt`, since the engine's tests call `nyx_copy_shaders_next_to()` from it.

### 4. Text and images

Built and tested on Windows 2026-10-12 (Visual Studio 2022, Debug): `NyxPainterTests` and `NyxPainterGpuTests` pass with the new tests, with the validation layers on and no validation messages, and so do the other 14 tests in `ctest -C Debug`. The editor starts and runs.

- **`Painter::AddText(font, size, position, colour, text)`**: UTF-8, lines broken at `\n` (`\r` is skipped), each line `size` high. One `Glyph` command per visible character; spaces only move the pen.
  - Glyphs come from `font->GetFontBaked(size * transform scale)`, so zoomed text is baked at the zoomed size instead of being stretched (tested: at scale 2 the glyph is the 26-pixel one).
  - The text starts on a whole pixel (`floor`, as ImGui does), so each glyph texel lands on one pixel.
  - Text casts no shadow or glow: in a group, the shader leaves glyphs out of the group's shape, since a glyph's shape is in the atlas and its rect would cast a box.
- **`Painter::AddImage(texture, rect, style, uvs)`** takes a `PainterStyle` instead of a tint, so images get the same colour (as tint) and shadow or glow as shapes; the outline width is ignored.
- **Payload:** `PainterTexturedRectPayload` (rect, then UV rect) for both types. `GetShapeBounds()` returns the rect.
- **Textures are ImGui texture references** (`ImTextureRef`), kept per frame in `Painter::GetTextures()`; `PainterCommand::Texture` indexes into them. They are resolved to the `ImTextureID` only in `VulkanPainter::Draw()`, since ImGui may still change its font atlas after the text is recorded.
- **Each texture is bound as a descriptor set of its own, not as an array in one set.** With ImGui's Vulkan backend a texture's `ImTextureID` is its descriptor set, and its image view can't be got from it, so ImGui's sets are bound as they are. The painter's texture set layout is defined exactly like ImGui's, which makes them compatible.
  - **`PainterMaxTextures` is 7**, not 8: the commands' set plus 7 texture sets is 8, the most every desktop GPU binds. `VulkanPainter::Initialize()` checks `maxBoundDescriptorSets` and says what to change if a GPU has fewer.
  - **No splitting into several draws.** An image whose texture would be the 8th of a frame is dropped and reported once (`GetProblems()`), like the other budgets. Text needs one texture (the atlas), so 6 are left for images; an icon atlas (overhaul change 9) keeps it that way. Splitting is left for when a frame really needs more.
  - Unused slots, and a texture without an `ImTextureID` yet (the font atlas before ImGui first uploads it), get a transparent 1x1 texture, so those glyphs and images are invisible rather than wrong.
- **Shader:** `TextureColor()` maps the pixel into the rect's UVs, clamped to the rect so edge pixels don't pick up a neighbour in an atlas, samples mip 0 explicitly (`textureLod`, since the command loop isn't uniform control flow), and premultiplies the straight-alpha texel before tinting. The rect's edge gets the usual one pixel of anti-aliasing.
- **The atlas timing is better than the plan expected.** ImGui's Vulkan backend uploads atlas changes synchronously while rendering ImGui, which happens before the frame's command buffer is submitted. A glyph baked for the first time is therefore visible in the same frame. Only a frame in which the atlas grows into a new texture can show newly baked glyphs one frame late, since that texture has no `ImTextureID` until ImGui's render. Old atlas textures stay alive for a few frames (the backend waits `ImageCount` frames), so a frame that recorded the old one still draws correctly.
- Tests:
  - `NyxPainterTests` (with an `ImGuiFonts` fixture: an ImGui context with `RendererHasTextures` after `NewFrame()`): glyph per visible character, colour, the atlas as texture, whole-pixel start, atlas UVs, left-to-right order; line breaks; zoomed text baked at its size; text without a font; images (transform, default UVs, tint, no outline, texture indices, UV part, shadow group, bounds); more than 7 textures.
  - `NyxPainterGpuTests` (it uploads the atlas and gives it its `ImTextureID` as the backend does): every pixel of a painted 'H' equals its atlas texel (a pixel-snapped font without oversampling, so texels map one to one); a group's shadow from a rect but none from glyphs; image texels, tint, part of a texture; an image whose texture has no ID yet is invisible.
- A full build once showed `LNK4020` ("a type entry in NyxEditor.pdb is corrupt") for `NyxEditorPanelTests`, which shares the editor's PDB. Rebuilding that target alone cleared it; it comes from parallel builds writing the shared PDB, not from this change.

### 5. Painter Test window and debug views

Built and tested on Windows 2026-10-12 (Visual Studio 2022, Debug): all 16 tests in `ctest -C Debug` pass, the GPU tests with the validation layers on and no validation messages. The window was tried in the editor: it shows the sheet as in the plan, and its first appearance gives no validation messages (see the bug below).

- **The renderer has painter canvases**, on `IRenderer`: `CreatePainterCanvas()`, `DestroyPainterCanvas()`, `SubmitPainterCanvas(id, painter, options)`, `GetPainterCanvasTextureId()` and `GetPainterCanvasTiles()`. The editor never touches Vulkan. `VulkanRenderer` keeps a `PainterCanvas` per id: a `VulkanPainterTarget`, a copy of the submitted painter, and an ImGui texture.
- **The UI is built before the swapchain render pass starts.** `DrawFrame()` now calls `ImGuiBackend->BeginFrame()` and `buildUI()` first, then `DrawPainterCanvases()` outside any render pass, then begins the render pass for ImGui. A canvas submitted while building the UI therefore shows in the same frame, without a frame of lag. Nothing in the UI records GPU commands, so the order is free to change.
- **A canvas' ImTextureID never changes.** The ImGui descriptor set is made once with `ImGui_ImplVulkan_AddTexture()` and pointed at the new image when the canvas changes size. The previous frame has finished by then, and ImGui records its use of the set only afterwards. The canvas uses a nearest-filtering sampler, since it is shown pixel for pixel.
- **`PainterDrawOptions` and `EPainterDebugView` moved to `PainterDrawOptions.h`**, so `Renderer.h` and the editor need no Vulkan header. **Binning times itself:** `PainterBinningStats::BinningMilliseconds`.
- **`PainterTestSheet.h/.cpp`** (engine): `DrawPainterTestSheet(painter, font, image)` and `PainterTestSheetSize` (760x560). Six labelled rows:
  1. rects: sharp, rounded, per-corner radii, a pill, outlines, half-transparent overlap;
  2. circles and sectors: filled, outlined, tiny circles, a pie, a ring, a gauge arc, a nearly full sector, an outlined sector;
  3. lines: a fan of angles, widths from half a pixel, round against flat caps, a sine graph;
  4. triangles: filled, outlined, a sliver, a play button, chevrons;
  5. shadows and glows: a card with a soft shadow, a hard shadow, a glow, a switch as one group, a focused button;
  6. text and images: 10 to 24 px, two lines with UTF-8, text cut by a clip rect, the font atlas' used part as an image, plain and tinted, on cards.
- **`PainterTestWindow`** (editor), *Debug > Painter Test*:
  - the canvas fills the window, or its left half when comparing, at whole pixels;
  - zoom from 25% to 800% with the slider or the wheel around the mouse; dragging with any button pans; *100%* resets;
  - *Tile heatmap*;
  - two lines of statistics (commands, culled, dropped, groups, textures; tiles in use, average and most commands per tile, binning time) and the frame's problems in red;
  - the canvas is cleared to the window's opaque background, since ImGui shows images with straight alpha and the painter writes premultiplied colour;
  - its canvas is given back when the window closes and when the editor shuts down.
- **The ImDrawList comparison is generated from the painter's own commands** (`DrawPainterFrameWithImDrawList()`): rects, circles, sectors (`PathArcTo` and `PathFillConcave`), lines, triangles, glyphs and images become ImDrawList calls, clip rects included. So the comparison can't drift from the sheet, and any future sheet shows it too. Shadows and glows are left out (ImDrawList has none), and rects get their largest corner radius.
- **Bug found and fixed: `ImTextureID_Invalid` is -1 in ImGui 1.92.7, not 0.** The window drew `ImGui::Image(0)` in its very first frame, before the canvas had a texture, which bound a null descriptor set (a Vulkan validation error, once, the first time the window ever opened). It only showed before `imgui.ini` remembered the window; later starts drew the same `Image(0)` without an error, presumably because ImGui doesn't render that first frame then. Tracing the window's draw list found the quad with texture 0.
  - `GetPainterCanvasTextureId()` now returns `ImTextureID_Invalid` when there is no texture.
  - `VulkanPainter::BindTextures()` treats both `ImTextureID_Invalid` and 0 (a null descriptor set) as missing; `NyxPainterGpuTests` covers both.
  - The scene views still return `ImTextureID{}` (0) for "no texture"; they were left alone.
- **Tests:** `NyxPainterTests` checks the sheet: no problems, more than 100 commands, every command type, everything inside `PainterTestSheetSize`, only the font atlas as texture, and only the glyphs past the clip rect culled. `NyxPainterGpuTests` paints the whole sheet on an opaque background and checks it stays opaque and covers a good part of its area.
- **Looking at what the GPU painted:** with the environment variable `NYX_PAINTER_TEST_OUTPUT` set to a folder, `NyxPainterGpuTests` saves `PainterTestSheet.png` (100%) and `PainterTestSheet400.png` (the top left at 400%). It uses `stb_image_write.h` from GLFW's `deps` folder, for this test only.
- **README:** a paragraph on the painter, the Painter Test window and the two test programs.
- **The PDB warnings came back** (`LNK4020`/`LNK4204` for `NyxEditorSceneTests` and `NyxEditorPanelTests`) on the full parallel build after editor headers changed. Rebuilding those targets alone clears them. Their cause is the editor test targets reusing `NyxEditor`'s precompiled header and its PDB while `NyxEditor` links; fixed separately in "#FIX Editor tests - no corrupt PDB copies in parallel builds".

#### Fixed after change 5: windows outside the main window

Opening the Painter Test window logged a validation error: an image "expected in `SHADER_READ_ONLY_OPTIMAL`, current layout `UNDEFINED`". The image was the canvas right after it changed size. The window sat in its own OS window (ImGui multi-viewports; `imgui.ini` placed it outside the editor), and ImGui renders such windows from inside `VulkanImGuiBackend::DrawFrame()`, in command buffers it submits at once. Those ran before the frame's own command buffer, which paints the canvas, so they sampled the new image before anything was drawn into it, and otherwise showed last frame's canvas.

- `VulkanImGuiBackend::DrawFrame()` now records only the main window; `RenderPlatformWindows()` renders the other windows, and `VulkanRenderer::DrawFrame()` calls it right after submitting the frame. This is the order ImGui's own Vulkan example uses.
- Scene views moved into their own OS windows had the same ordering: they showed the previous frame. They now show the current one.
- Left for stage 2, change 1: the frame's fence covers only the frame's own submission, so the next frame can start writing painter buffers and canvases while those other windows' command buffers may still read them. Done in stage 2, change 1.

#### Fixed after change 5: the comparison when the font atlas grows

Zooming with *Compare with ImDrawList* on could crash the editor on ImGui's assertion `tex_ref._TexData->WantDestroyNextFrame == false` (`ImDrawList::PushTexture()`).

- **Cause:** zooming bakes glyphs at new sizes, and the font atlas can grow in the middle of a frame. ImGui then moves to a bigger texture and destroys the old one next frame. Glyphs the painter recorded before the growth still name the old texture, with UVs into it. The converter handed that texture to the draw list, which ImGui forbids once it is replaced.
- **Fix:** `DrawPainterFrameWithImDrawList()` leaves out glyphs and images on a texture replaced this frame, so the comparison misses them for one frame.
- **The painter itself is fine:** it draws them from the old texture, which lives until the next frame. The same holds for stage 2's segments, whose textures are resolved when ImGui renders the same frame.
- **Test:** `NyxEditorPanelTests` grows the atlas between two texts and checks that the comparison leaves the old texture out and still draws the new one. Without the fix it fails on the same assertion.

---

# Stage 2: painting inside ImGui windows

Plan written 2026-10-12, after stage 1. Stage 1 paints into an image of its own, shown with `ImGui::Image()`. Stage 2 paints straight into ImGui's windows, in order with everything ImGui draws there, and builds the first `Nyx::UI` widgets on it. Painted window chrome (tab bars, docking strips) and glass come after stage 2 (`Design/editor-ui-architecture.md`, section 5, step 9).

## Done when

- A window can paint between its ImGui widgets: a painted card behind ImGui text, an ImGui button over a painted shape, then painted shapes over both. The order on screen is the order in the code.
- Painting stays right with overlapping windows, popups and tooltips, docked tabs, child windows and scrolling regions (painting is clipped like ImGui's drawing), and windows dragged out of the editor into their own OS windows.
- Glyphs baked for the first time show in the same frame, even when the font atlas grows.
- `Nyx::UI::Button`, `Toggle`, `SliderFloat` and `SectionHeader` are painted, react like their ImGui counterparts (mouse, keyboard navigation, `NYX_UI` and F8 source inspection), and animate their hover and press states.
- The Painter Test window gets a *Widgets* tab: the painted widgets beside ImGui's own, at 100% and 150% font scale, with the per-window statistics.
- No validation messages, also with windows in their own OS windows, while they move, resize and close.

## How it works

- **Recording:** inside `Begin()`/`End()`, a window asks for its painter: `Nyx::UI::GetWindowPainter()`. Positions are ImGui screen coordinates, as for `ImDrawList`, and every command takes ImGui's current clip rect, so painting is clipped like ImGui's own drawing in child windows, tables and scrolling regions.
- **Segments:** the painted commands of a window are cut into **segments** wherever ImGui draws in between. A segment starts with an `ImDrawList` callback at its place in the window's draw list (as built: starts, since a callback can only be appended). ImGui renders windows back to front and each draw list in order, so the callback, and with it the segment, lands exactly where it was recorded: in front of what came before, behind what comes after.
- **Binning and upload, once per frame:** after the UI is built (where painter canvases are drawn now), the renderer bins every segment over its own small tile grid (the segment's bounds, aligned to 8 pixels) and uploads all segments of the frame into one set of buffers, each at its own offset.
- **Drawing:** a segment's callback runs inside ImGui's render pass, in whichever command buffer ImGui renders that window with (the main window's, or an OS window's, through `ImGui_ImplVulkan_RenderState`). It binds the painter's pipeline, draws one quad over the segment's tiles with the segment's offsets in push constants, and adds `ImDrawCallback_ResetRenderState` so ImGui continues with its own state.
- **Textures** are resolved to their `ImTextureID` in the callback, after ImGui has created and updated its textures for the frame. That closes stage 1's last timing gap (glyphs of a grown atlas).

## Changes

### 1. One fence for the whole frame

- After `RenderPlatformWindows()`, submit an empty batch with the frame's fence. A fence signalled by `vkQueueSubmit` also waits for everything submitted before it on the queue, so the fence then covers the frame's own work and the OS windows' command buffers.
- The next frame waits for it before it rewrites painter buffers, canvases, scene views and ImGui's own buffers.
- *Done when* moving a scene view or the Painter Test window into its own OS window and resizing it logs no validation messages, and frame times don't change measurably.

### 2. Segments: recording (CPU)

- `PainterSegment`: a range of a window painter's commands and groups, its bounds, its clip rect and its viewport.
- A **window painter** (in `Nyx::UI`, editor side) owns one `Painter` per ImGui window per frame, and ends a segment, with an `ImDrawList` callback, whenever ImGui has drawn since the last painted command (the draw list's index count changed).
- Coordinates: ImGui positions minus the viewport's position, times its framebuffer scale, so a segment is binned in its viewport's framebuffer pixels.
- `BinPainterSegment()`: binning over a grid that covers only the segment, so a small painted button costs a few tiles, not the whole viewport's.
- Tests (`NyxPainterTests`): segments split where expected, bounds and grids, offsets, viewport coordinates, a segment wholly clipped away.

### 3. Segments: drawing (GPU)

- One upload per frame of all segments, into one set of buffers with per-segment offsets. The shader gets the offsets, the tile grid's origin and size, and the viewport's size as push constants; `Painter.vert` makes the segment's quad from them instead of the full-screen triangle.
- A pipeline for ImGui's render passes. A pipeline only needs a *compatible* render pass (same colour format, one sample, no depth), so one pipeline per colour format covers the main window and every OS window. The OS windows' format is chosen by ImGui's backend from a short list; it is checked when the pipeline is made.
- `VulkanPainter::DrawSegment(commandBuffer, segment, viewport)`, usable in any compatible render pass.
- Tests (`NyxPainterGpuTests`): segments drawn into a test render pass that already holds something: order, offsets, clipping, two segments with different textures.

### 4. Painting inside ImGui windows

- `Nyx::UI::GetWindowPainter()` and the draw callback; the renderer gathers the window painters after the UI is built, bins and uploads (changes 2 and 3), and the callbacks draw.
- Works in the main window and in OS windows (their callbacks run in their own command buffers, after change 1's ordering), docked, overlapping, in popups and tooltips, in child windows and while scrolling.
- The Painter Test window gets a second tab that paints the test sheet straight into the window (no canvas), with ImGui widgets between its rows, to show the order.
- Statistics per frame: windows that painted, segments, tiles, binning time.

### 5. Painted widgets

- `Nyx::UI` widgets on ImGui's own behaviour functions (`ItemSize`, `ItemAdd`, `ButtonBehavior`, `SliderBehavior` from `imgui_internal.h`), so IDs, input, keyboard navigation, the disabled state and the last-item queries work as for ImGui's widgets, and `NYX_UI` wraps them like any widget.
- First widgets: `Button`, `Toggle` (a switch), `SliderFloat`, and `SectionHeader` (the accent header of the editor concepts).
- **Animation store:** `Nyx::UI::Animate(id, target, speed)` eases a value per widget ID over frames (hover, press, the toggle's knob) and forgets IDs not used for a while.
- **Colours** from a small `Nyx::UI::Theme` struct for now. Widgets read every colour, radius, spacing and effect from it, never from a literal, so overhaul change 9 can fill it from the theme file and the theme editor without touching the widgets.
- Text in painted widgets is painted too (`AddText()` with ImGui's current font and size), so a widget is one segment.
- **The UI gallery** (`Design/editor-ui-architecture.md`, 3.10.3): the *Widgets* tab in the Painter Test window shows each painted widget in every state (normal, hovered, pressed, focused, disabled; states forced, not only reached by the mouse), beside its ImGui counterpart, at 100% and 150% font scale. It is where widgets are designed. A *Save PNG* button saves the gallery as it looks, to compare a widget before and after a change.
- **The mockup overlay:** a debug window that lays a mockup image (a PNG chosen from disk), half transparent, over the editor or the gallery, with its opacity, offset and scale, to match sizes and positions. Painted as one image over everything.
- Tests (`NyxEditorPanelTests`, headless): clicks, keyboard activation, `IsItemHovered()`/`IsItemActive()` after the widgets, the animation store's easing and forgetting.

Split while building it (2026-10-12), so each part stays reviewable:

- **5a. Widgets and the gallery:** the theme, the animation store, the four widgets, the *Widgets* tab, the tests.
- **5b. Images in and out:** *Save PNG* (a readback of the swapchain image in the renderer, written as PNG) and the mockup overlay (a PNG loaded from disk into an ImGui texture, painted with `AddImage()`).

## Risks

- **ImGui internals:** the widgets use `imgui_internal.h`, whose API changes between ImGui versions more than the public one. Only the widget files use it.
- **One draw per segment:** a window that alternates painted and ImGui drawing a hundred times makes a hundred small draws. The statistics show it; widgets that paint their own text keep segments few.
- **OS windows with another colour format:** a pipeline per format; checked in change 3. (As built: compatibility also needs the same subpass dependencies, which ImGui's OS windows and the main window share.)

## Left for later

- Painted window chrome, ambilight and glass: stage 3.
- Gradients, masked images, convex polygons, bezier curves, the custom command.
- Splitting a frame that needs more than `PainterMaxTextures` textures.
- Binning on the GPU.
- A source id per command, so F8 can name the code behind a painted pixel.
- Painting in NyxGame (debug overlays, the frame-time graph).

## As built

### 1. One fence for the whole frame

- `VulkanRenderer::DrawFrame()` submits the frame's command buffer without a fence. After `RenderPlatformWindows()`, it submits an empty batch carrying `InFlightFence`.
- This works because a fence signalled by `vkQueueSubmit` waits for all earlier work on the queue, not only its own batch. ImGui submits the OS windows on the same graphics queue.
- The comments that already assumed this are now true. They are in `DrawPainterCanvases()` (the canvas descriptor set updated in place) and `VulkanPainter` (one set of upload buffers per target, and a resized target's old image).
- Nothing else changed: the next frame waits on the same fence at the same place, and the empty submit costs nothing measurable.
- Checked: the build is clean, all 16 tests pass, and a 12-second editor run logs no validation messages. Moving a scene view or the Painter Test window into its own OS window and resizing it is a hand test.

### 2. Segments: recording (CPU)

- **`PainterSegment`** (`Painter.h`) is only `FirstCommand` and `CommandCount`.
  - Groups and textures stay the window painter's; a segment's commands index them as in stage 1.
  - Bounds and grid come from binning, not recording (`PainterTiles::OriginX`/`OriginY`).
  - The viewport belongs to the window painter, since a window is in one viewport per frame.
- **`Nyx::UI::WindowPainter`** (`WindowPainter.h`) lives in the engine's renderer, next to `ImGuiSource.h`, not on the editor side as planned. The renderer gathers and draws the window painters in change 4, and NyxGame can paint later.
  - It has Painter's shapes, text, images and groups, in ImGui's screen coordinates. `Begin(drawList, viewport)` starts a frame and `End()` ends it.
  - It has no clip rects or transforms of its own. Clipping is ImGui's: `ImGui::PushClipRect()`, child windows, tables and scrolling regions. Transforms are left for later.
- **A segment starts with its callback, not ends with one.** A draw list callback can only be appended, so by the time ImGui has drawn, the place after the previous segment is gone.
  - Before each shape, `PrepareSegment()` starts a new segment if this is the first shape, if the draw list's command or index count changed since the last segment started (ImGui drew), or if ImGui's clip rect changed.
  - Starting a segment appends two callbacks: `DrawSegmentCallback` with `{ WindowPainter*, segment index }` (ImGui keeps a copy), then `ImDrawCallback_ResetRenderState`, both at record time.
  - All shapes of a segment share one clip rect.
- **Coordinates:** the painter's target is the viewport's framebuffer, and a pushed transform maps ImGui positions to `(position - viewport position) * framebuffer scale`. ImGui's clip rect is pushed through the same transform, so it is cut by the viewport too.
- **`DrawSegmentCallback`** hands the segment to a drawer set with `WindowPainter::SetSegmentDrawer()`. Nothing sets one yet (change 4), so the callbacks draw nothing.
- **`BinPainterSegment()`** bins one segment over a grid fitted to what its commands can touch, aligned to 8 pixels.
  - Entries count from the segment's first command.
  - A group's reach only counts its shapes in that segment.
  - A wholly clipped segment gets a 0×0 grid.
  - A segment past the painter's commands is cut and reported.
  - `BinPainterCommands()` and `BinPainterSegment()` share one function: whole-target grid or fitted grid.
- **Reported problems:**
  - ImGui drew between the shapes of one group. The group splits over two segments, and each part casts its own shadow and glow.
  - A shape outside `Begin()`/`End()`.
  - `End()` without `Begin()`, and `Begin()` without `End()`.
  - After `End()`, the painter's own problems are included too.
- **For change 3:**
  - a segment's command base is the window painter's upload offset plus `FirstCommand`;
  - its group base is the window painter's group offset;
  - tile entries are relative to the command base.
  - `PainterMaxTextures` applies per window painter, not per segment.
- **Tests** (`NyxPainterTests`, 8 new):
  - segment grids, origins and relative entries;
  - a group's reach per segment;
  - a segment clipped away, and one past the commands;
  - in a real ImGui window: splits where ImGui draws and where the clip rect changes, one draw callback and one reset per segment, in order, with the window's and the pushed clip rects;
  - an OS window at (100, 50) with framebuffer scale 2: positions, sizes and the clip rect in framebuffer pixels;
  - ImGui drawing inside a group;
  - misuse.
- **README:** `NyxPainterTests` now also tests segments.

### 3. Segments: drawing (GPU)

- **One shader for canvases and segments.**
  - The push constants (48 bytes, vertex and fragment) carry:
    - where the drawn part starts in each buffer (`CommandBase`, `GroupBase`, `TileBase`, `EntryBase`);
    - the tile grid (`OriginX`/`OriginY`, `TilesX`/`TilesY`);
    - the target size and the debug view.
  - `Painter.vert` draws a quad over the grid (6 vertices) instead of the full-screen triangle. A canvas is the case where the grid covers the whole target and every base is 0.
  - `Painter.frag` reads commands through `CommandAt(entry)` and groups at `GroupBase + Group`. It discards pixels left of or above the grid.
- **`VulkanPainterBuffers`:** the four storage buffers and their descriptor set, taken out of `VulkanPainterTarget`. Canvases and segment sets share one `Upload()`.
- **`VulkanPainterSegments`:** one frame's segments from any number of painters, used as `Clear()`, then `Add(painter, segment, budget)` for each segment, then `VulkanPainter::Upload(segments)` once.
  - The first `Add()` for a painter copies its commands, groups and texture references. Every `Add()` bins its segment (`BinPainterSegment()`) and appends the tiles and entries.
  - So the painters may change or go after `Add()`. Textures stay `ImTextureRef`s until the segment is drawn.
  - `GetStats(segment)` and `GetProblems()` are there for change 4's statistics.
- **`VulkanPainter::DrawSegment(commandBuffer, segments, segment, colorFormat, framebufferExtent)`** runs inside a render pass that is already going on.
  - It sets its own viewport and scissor: the grid cut by the framebuffer. ImGui leaves its last clip rect as the scissor before a callback.
  - It binds the pipeline, set 0 and the painter's textures, pushes the offsets and draws the quad.
  - A segment without tiles draws nothing.
  - It takes a plain `vk::CommandBuffer`, since ImGui's callbacks hand over a raw one (`ImGui_ImplVulkan_RenderState`).
- **The plan was wrong about compatibility.** A render pass is compatible with a pipeline's only if the subpass dependencies match too, not just the formats. The validation layer caught it in the first test run (`VUID-vkCmdDraw-renderPass-02684`, "dependencyCount is incompatible").
  - ImGui's OS windows and `VulkanSwapchain` have the same single dependency (external to subpass 0, colour attachment output, colour attachment write). So `GetSegmentPipeline(format)` makes its compatible render pass with exactly that dependency, still one pipeline per format.
  - `VulkanSwapchain::CreateRenderPass()` says to keep its dependency so.
  - The canvases keep their own pipeline (`CanvasPipeline`), made with their render pass (clear, two dependencies, ShaderReadOnly at the end).
- **The empty texture** is cleared once in `Initialize()`, in a short submission of its own. Segments are drawn inside render passes, where an image can't be cleared, so `PrepareEmptyTexture()` is gone.
- **Tests** (`NyxPainterGpuTests`, 2 new). `RenderInto(format, extent, background, record)` renders into a fresh image through a render pass made like a window's, then reads it back as RGBA.
  - **Segments drawn in order with other drawing**, in `B8G8R8A8Unorm`, the swapchain's usual format, so a second pipeline is made. Four segments from two painters, with a `vkCmdClearAttachments` square between the first two standing in for ImGui. It checks:
    - the order;
    - a clip rect leaving what was beneath;
    - each painter's own texture and shadow (its own group, through `GroupBase`);
    - a wholly clipped segment drawing nothing;
    - pixels outside every grid left alone.
  - **The test sheet as two segments matches the canvas within the tolerance**, after another painter's commands and group so every base is non-zero. The split falls where no group goes across.
  - No validation messages.
  - With `NYX_PAINTER_TEST_OUTPUT` set, it saves `PainterSegments.png` and `PainterTestSheetSegments.png`.
- **README:** `NyxPainterGpuTests` also checks segments.

### 4. Painting inside ImGui windows

- **`Nyx::UI::GetWindowPainter()`** (`WindowPainter.h`) returns the window painter of the window being built, child windows included. It starts that painter on the first call in a frame.
  - The painters come from a pool, so they never move while draw lists point at them, and they are reused frame to frame. Each is found by its window's draw list.
  - A new frame, or another ImGui context, forgets the last frame's list. Painters nobody ended get ended quietly, for headless use without a renderer.
- **`Nyx::UI::EndWindowPainters()`** ends this frame's painters and returns them in the order they started. The renderer calls it after the UI is built.
- **`WindowPainter` now has the whole `Painter` API**, so the test sheet can be painted with either:
  - `PushClipRect()`/`PopClipRect()` push onto ImGui's own clip rect stack (`ImDrawList::PushClipRect()`, cut by the current one), through the transform. ImGui's drawing until the pop is clipped too, and the next shape starts a segment.
  - `PushTransform()`/`PopTransform()` apply to shapes and to clip rects pushed meanwhile, not to ImGui's drawing. Popping the viewport's own transform is refused and reported.
  - `Begin()` takes the window's name, for problems and statistics.
- **`Painter::PushTargetClipRect()`:** a clip rect already in target pixels, whatever transform is pushed. Segments push ImGui's clip rect with it, through the viewport transform only, so a user transform doesn't move ImGui's clip rect.
- **`SetSegmentDrawer(drawer, userData)`** now takes user data (the renderer).
- **Renderer** (`VulkanRenderer`):
  - **`PrepareWindowPainters()`** runs in `DrawFrame()` right after `DrawPainterCanvases()`. It gathers the window painters' segments into `WindowSegments` (`VulkanPainterSegments`), remembers where each painter's segments start, uploads, and sums the statistics.
  - **`DrawWindowSegment()`** is the drawer, called from ImGui's callbacks:
    - the command buffer comes from `ImGui::GetPlatformIO().Renderer_RenderState`;
    - the framebuffer size comes from the viewport's draw data, worked out as ImGui's backend does;
    - the format is the swapchain's for the main viewport, and for an OS window the one ImGui chose (`ImGui_ImplVulkanH_GetWindowDataFromViewport(viewport)->SurfaceFormat`).
  - The drawer is set in `Initialize()` and unset in `Shutdown()`, and `WindowSegments` is released there.
- **Statistics:** `PainterWindowStats` (`PainterTiles.h`) holds windows that painted, segments, commands, tiles in use, tile entries and binning time. Its problems are the window painters' own (prefixed with the window's name) and binning's. They come from `IRenderer::GetWindowPainterStats()`, as of the last drawn frame.
- **Test sheet:** the rows are templates (`DrawPainterTestSheetRow<TPainter>()`), explicitly instantiated for `Painter` and `Nyx::UI::WindowPainter`. `PainterTestSheetRowCount`, `PainterTestSheetRowHeight` and `GetPainterTestSheetRowTop()` give the layout, and `DrawPainterTestSheet()` draws all rows.
- **Painter Test window** has two tabs:
  - *Canvas*: everything from stage 1.
  - *In a window*:
    - the statistics, then the sheet painted into a scrolling child window, row by row, each row moved to the cursor with `PushTransform()`;
    - an ImGui button over each row's right end, and a painted badge over each button, to show the order.
- **Tests** (`NyxPainterTests`, 2 new):
  - clip rects and transforms: ImGui's stack, the transform applied to shapes and pushed clip rects, a segment per clip rect, popping the viewport transform refused;
  - one painter per window and per child window, `EndWindowPainters()` order and ending, and a new frame without painters.
- **Checked in the editor:** two runs, with the Painter Test window forced open on *In a window*; the hacks were reverted and `imgui.ini` restored afterwards.
  - In its own OS window (as `imgui.ini` had it): ImGui's backend chose the format, and the drawer found it.
  - Forced inside the main window, over a scene view: the order is right, and the sheet is clipped by the scrolling child.
  - Both show the right order and no validation messages.
- **Not checked by hand yet:**
  - docked tabs, popups and tooltips;
  - dragging the window between viewports while it paints. The painter records for the window's viewport at `Begin()`; if a window changed viewport between building and rendering, that frame's segments would be drawn for the old one.
- **README:** a sentence on the *In a window* tab.

### 5a. Painted widgets and the UI gallery

- **Where:** in the engine's renderer module, next to `WindowPainter.h` and `ImGuiSource.h`, not on the editor side. Widgets paint with the window painters the renderer gathers, and NyxGame can use them later. Only `UIWidgets.cpp` (and, for a context hook, `UIAnimation.cpp` and `WindowPainter.cpp`) include `imgui_internal.h`.
- **`Nyx::UI::Theme`** (`UITheme.h`, `GetTheme()`): every colour, radius, size and effect of the widgets, as tokens:
  - colours: text, frame (normal, hovered, pressed, border), accent, track, knob, separator;
  - radius and border width;
  - control shadow and how far it shrinks when pressed, the pressed text offset, and the focus glow;
  - toggle, slider and section header sizes, as fractions of the frame height so they grow with the font;
  - the disabled alpha and the animation speed.

  **Layout follows ImGui's style instead** (frame padding, item spacing, item width), so painted widgets line up with ImGui's in the same window. This is a change from the plan, which had spacing in the theme too.
- **`Nyx::UI::Animate(id, target, speed)`** (`UIAnimation.h`): exponential easing, frame-rate independent (`target + (value - target) * e^(-speed * dt)`). Each value moves once per frame and starts at its target. Values not asked for in `AnimationForgetFrames` (60) are forgotten. Widgets key their values with `ImHashStr("Hovered", 0, id)` and the like.
- **The widgets** (`UIWidgets.h`): `Button`, `Toggle`, `SliderFloat` and `SectionHeader`, each mirroring its ImGui counterpart's code:
  - `Button` follows `ButtonEx()`, `Toggle` follows `Checkbox()`, and `SectionHeader` is a `NoNav` item.
  - `SliderFloat` follows `SliderScalar()`, without Ctrl+click to type a value.
  - Each works out a `WidgetLook` (hovered, pressed and focused eased 0 to 1; disabled from `ItemFlags`). Focused means `NavId == id` with the nav cursor visible, as ImGui draws its focus rect.
  - Each then calls a pure `Paint...()` function with the window's painter. Text is painted too, so a widget is one segment.
- **The slider's track** is the frame without room for the value's text, which sits right-aligned in the frame. That room is the wider of min and max formatted, so the track keeps its length.
  - `SliderBehavior()` gets the track rect, so the mouse maps onto the track.
  - The knob's centre follows `SliderBehavior()`'s grab layout (2 pixels of padding, a grab of `GrabMinSize`), so it sits under the mouse.
- **The UI gallery:** the Painter Test window's *Widgets* tab.
  - At 100% and 150% font size (`PushFont(nullptr, FontSizeBase * scale)`), each under a painted section header.
  - A table of Button, Toggle off and on, Slider and Section header: each forced state painted with `Paint...()`, then the live painted widget and ImGui's own (sharing their values).
  - Sliders get their width per cell (`SetNextItemWidth()`), since a table sets the item width per column. The first look had the knob stuck at the left of a too-short track.
- **`WindowPainter` also starts a segment when the draw list's channel changes.** Tables draw each column into a channel of their own, and two channels could happen to have the same sizes.
- **Contexts:** `WindowPainter`'s pool and the animation store forget everything when their ImGui context shuts down (`AddContextHook(ImGuiContextHookType_Shutdown)`). Tests found that a new context can get a destroyed one's address, so comparing pointers isn't enough.
- **Tests** (`NyxEditorPanelTests`):
  - painted widgets behave like ImGui's: hover and `IsItemHovered()`, held and `IsItemActive()`, a click on release, the toggle switching, the slider's ends giving min and max, Space clicking a focused button, a disabled button ignoring clicks;
  - the animation store: starting at the target, one frame's easing, once per frame, arriving, and being forgotten.
  - ImGui only activates by keyboard while the nav cursor is visible; the test turns it on as Tab or an arrow key would.
- **Checked in the editor:** the gallery, forced open inside the main window, looks right at both sizes, with every state, and logs no validation messages. The temporary changes were undone and `imgui.ini` restored afterwards.
- **README:** a sentence on the *Widgets* tab.

### 5b. Images in and out: Save PNG and the mockup overlay

- **`ImageFile.h`** (engine): `RgbaImage`, `LoadRgbaImage()` (stb_image: PNG, JPEG, BMP, TGA) and `SavePng()` (stb_image_write, which GLFW ships in `deps`, compiled with `STB_IMAGE_WRITE_STATIC` so it can't clash with the GPU tests' copy).
  - Files go through streams, so any path works.
  - `SavePng()` makes the folder.
- **UI images** (`IRenderer::CreateUIImage()`, `DestroyUIImage()`, `GetUIImageTextureId()`):
  - `R8G8B8A8Unorm`, not sRGB: `Texture` loads as sRGB and would show a mockup darker than ImGui's colours.
  - Linear filtering, uploaded in a short submission of its own, with an ImGui descriptor set as the texture ID.
  - Destroying waits for the next frame's fence (`UIImagesToRelease`), so it is safe while the UI is built. `Shutdown()` releases them all.
- **Screenshots** (`IRenderer::SaveScreenshot(path, min, max)`, `GetScreenshotMessage()`):
  - The swapchain images get `TransferSrc` usage where the surface allows (`VulkanSwapchain::CanCopyImages()`).
  - The rect is turned from ImGui's screen coordinates into the main window's pixels.
  - Right after ImGui's render pass, `RecordScreenshot()` copies it out of the swapchain image (PresentSrc to TransferSrc and back) into a host buffer.
  - After the next frame's fence, `FinishScreenshot()` writes the PNG: blue and red swapped for `B8G8R8A8`, alpha made opaque.
- **Main window only.** ImGui's backend makes its OS windows' swapchains with colour attachment usage alone, so they can't be copied from without changing ImGui. *Save PNG* says so when the Painter Test window is in an OS window.
- **`Nyx::UI::GetForegroundPainter(viewport)`:** a window painter on a viewport's foreground draw list, which ImGui renders after all windows. It comes from the same pool as the window painters, which are now found by draw list.
- ***Save PNG*** in the gallery saves the gallery's child window, as the screen shows it, to `Saved/Gallery/Gallery <date> <time>.png` under the project root. `Saved/` is in `.gitignore`.
- **The mockup overlay** (`MockupOverlay`, *Debug > Mockup Overlay*):
  - a path typed in or chosen with Windows' Open dialog (`FileDialogs.h`, `ChooseImageFile()`, `IFileOpenDialog`);
  - *Show*, *Opacity*, *Offset* (from the main window's top left), *Scale*, and *50%*/*100%*/*Reset offset*;
  - painted with `AddImage()` on the main viewport's foreground painter, so it lies over everything and takes no input;
  - closing the window hides the image; the editor gives it back on detach.
  - Its controls use the painted widgets.
- **Tests** (`NyxPainterTests`): a PNG saved into a new folder loads back as it was, a missing file and an empty image fail with a reason, and the foreground painter is gathered with the windows' ones.
- **Checked in the editor**, with temporary code (undone; `imgui.ini` restored and the test PNG deleted):
  - the gallery inside the main window, *Save PNG* triggered once, and a mockup loaded at 50% at an offset;
  - the PNG held exactly the gallery's rect with the right colours, the mockup lay over all windows, and no validation messages were logged;
  - the overlay window then got a fixed first height, since it had sized itself to its contents before an image was loaded.
- **README:** *Save PNG* and the mockup overlay.
- **Stage 2 is complete.**

---

# Stage 3: the whole editor painted

Plan written 2026-10-12, before stage 2 is built; it will be refined once stage 2 is done. It follows the decision to paint the whole editor (`Design/editor-ui-architecture.md`, 3.10.2): ImGui stays as the logic (IDs, input, layout, docking, text editing), and everything visible in the editor's own panels is painted. The target look is the editor concepts in `Design/EditorConcepts` and the light concept with ambilight: panels as floating rounded cards with gaps and soft shadows, pill tabs with accent underlines, painted trees and property rows, translucent overlays on the viewport, and the viewport's colours bleeding into the UI around it.

## Done when

- The editor matches the concepts in a light and a dark theme: backdrop, panel cards, tab bars, toolbars, outliner, details panel, asset browser, status bar.
- **Ambilight:** the scene view's colours glow onto the backdrop around it and faintly through the panels, following the picture as it changes.
- **Glass** on floating panels and overlays.
- No shape in the editor's own panels is drawn by ImGui; ImGui still draws text in the hybrid widgets (see change 7), which looks the same.
- A fully painted OS window is one painter draw; the statistics show draws per OS window.
- The look holds at 100% and 150% scale and in windows dragged out of the editor.

## Not painter work, but needed for the look

- The UI font and a merged icon font, theme tokens with a light and a dark theme (overhaul change 9).
- Asset thumbnails and material spheres: a thumbnail renderer that draws each asset into a small image; the painter only shows them.
- Workspaces (the Scene / Animation / Rendering tabs) and the command search in the titlebar (`Design/editor-ui-architecture.md`, 3.1 and 3.3).
- Rounded corners of the editor's own OS window: Windows 11 does that when asked (`DwmSetWindowAttribute`, corner preference), a small change in `WindowsWindow`.

## Changes

### 1. Blur chain

- After the scene views render, each one's image is downsampled a few times (to about 1/16) and blurred, into a small image of its own. Cheap: a few small passes per view and frame.
- Generic: any image can be blurred this way; glass (change 6) blurs what lies behind a panel with it.
- The blurred image is an ImGui texture, so the painter samples it like any image.
- The Painter Test window shows a scene view's blurred image, with the time the passes take.

### 2. Image glow primitive

- A new command type: for a pixel outside a rect, the colour of a texture at the nearest point of the rect's edge, faded by the distance to the rect (out to a reach, with a strength). The signed distance to the rect gives both the nearest point and the fade.
- Bounds: the rect grown by the reach. Payload: the rect, the reach, the strength, the texture's UV rect.
- Added through the checklist in `PainterCommands.h`; a row on the test sheet (a glow from the font atlas, and from a gradient image); CPU and GPU tests.

### 3. Painted backdrop and panel cards

- The editor's backdrop (behind the dockspace) is painted, from the theme.
- Every editor window is painted as a card: rounded, with a soft shadow, inset from its dock node so cards have gaps between them. ImGui's window background and border are made transparent for these windows; ImGui's layout inside is unchanged.
- Floating windows, popups, menus and tooltips get cards too, with stronger shadows.
- Splitters between docked cards are painted (a thin line or nothing until hovered), over ImGui's own splitter behaviour.

### 4. Painted tab bars

- **Hybrid first:** ImGui's dock tab bars keep their behaviour (select, drag to undock, close, context menu) with transparent colours; the painter draws pills or accent underlines behind them in the tab bar's rect. ImGui's tab text stays, and looks the same as painted text.
- Only if the hybrid can't reach the look: ImGui's tab bars hidden (`ImGuiDockNodeFlags_NoTabBar`) and tabs painted fully from the dock nodes, with the behaviour rebuilt on `imgui_internal.h`. Decided after the hybrid is tried.

### 5. Ambilight

- The scene view's card paints an image glow (change 2) of its blurred image (change 1) onto the backdrop, behind all cards.
- Panel cards are slightly translucent (theme tokens), so the glow shows faintly through them; optionally a much weaker glow sampled under each card.
- Strength and reach in the editor preferences; off for people who find it distracting.

### 6. Glass

- For floating panels and the viewport's overlays: what lies behind them, blurred (change 1), sampled inside the panel's shape, with refraction near the edge and a light rim from the signed distance's gradient.
- What lies behind a floating panel is the docked layer: the backdrop, the docked cards and the viewport. That layer is painted into an image first, blurred, and the floating panels are painted over it. This is the one place where the editor is painted in two layers.
- Windows dragged out of the editor can't see the desktop behind them; they use Windows' Mica or Acrylic, or no glass.

### 7. Widget coverage

In order of how much the editor shows them; each first as a hybrid where that is enough:

1. Tree rows (outliner, asset folders): selection pill, hover, indentation guides, icons, visibility eye toggles; ImGui's tree logic and text.
2. Property rows in the details panel: label column, value frames, coloured X/Y/Z labels on vectors; ImGui's drag and text editing behind painted frames.
3. Text fields and search fields: painted frame, focus glow, placeholder; ImGui's editing and text.
4. Combos, dropdowns and menus: painted frames and popups; ImGui's behaviour and text.
5. Checkboxes, colour swatches, scrollbars (thin painted pills).
6. Asset cards in the asset browser: painted card, thumbnail as an image (thumbnail renderer), selection.
7. Tables (Game Link messages): painted rows and headers; ImGui's table logic.
8. The status bar, tooltips and notifications.

Split while building it (2026-10-12), so each part stays reviewable:

- **7a. Tree rows:** the outliner and the asset browser's folders (1).
- **7b. Property rows and text fields:** the details panel and search fields (2, 3).
- **7c. Combos, menus, checkboxes, colour swatches and scrollbars** (4, 5).
- **7d. Asset cards, tables, the status bar, tooltips and notifications** (6, 7, 8).

Unlike tab bars (change 4), ImGui keeps no record of where it drew these widgets, so painting behind them after the fact (the hybrid) isn't possible. They are painted widgets on ImGui's behaviour functions, like stage 2's.

### 8. One draw per OS window

- Segments that follow each other in render order with no ImGui drawing between them merge into one; a fully painted OS window is then one painter draw.
- Statistics: draws per OS window, and a debug view that marks what ImGui still draws itself in the editor's panels.

Split while building it (2026-10-12), so each part stays reviewable:

- **8a. One draw per run of segments:** merging, the draws per OS window, and the marks of what ImGui still draws.
- **8b. The fallback:** dropped, see below.

#### Dropped: the ImDrawList fallback (was 8b)

The plan had a fallback switch in the Debug menu: all painting drawn through the converter to ImDrawList (the Painter Test window's *Compare with ImDrawList*), so the editor would stay usable with a broken painter. It was dropped on 2026-10-12, before it was built:

- **Cost:** converted geometry would have to be spliced into ImGui's draw lists at each segment's place: commands inserted, vertex and index offsets rewritten, and painting behind everything (cards, tab bars) handled apart. That leans on ImGui's draw list internals more than anything else in the painter, and each ImGui update could break it.
- **Lossy anyway:** the converter has no shadows, glows, per-corner radii, image glows or glass, so the fallback editor would be a degraded copy to keep checking.
- **Little to gain:** a painter that can't start says so at startup (the descriptor set check); a shader broken while working on it is fixed in the shader, and the GPU tests catch it first. A fallback pays off once others run the editor on hardware no one controls, which isn't the case.
- **What stays:** the converter, for *Compare with ImDrawList* in the Painter Test window.
- **If it is ever needed:** a switch that makes each `Nyx::UI` widget call its ImGui counterpart (the UI gallery already pairs them) is cheaper than converting segments, and stays in ordinary code.

### 9. The last of ImGui's shapes

Added 2026-10-13 to finish the stage: what *Mark ImGui Shapes* still finds. With the Painter Test window and Play Setups floating, it found:

- **The titlebar** (`WindowsWindow`): its background, the window controls (minimise, maximise or restore, close) with their icons, and the focus border around the main window (in the foreground draw list, which the marks didn't look at).
- **The titlebar's buttons** (`EditorLayer`, `PlayControls`): File, Window, Debug, Play / Stop and Play All.
- **Buttons, fields and tree nodes in panels:**
  - the outliner (Add Entity, Delete Selected, Duplicate);
  - Game Link (Clear, Copy, Pause, Types, and the message's details);
  - the crash cards (their buttons, call stack and last log lines);
  - Play Setups (Add Setup, Reset to Defaults, and the preview of where the windows go);
  - the mockup overlay's path and offset;
  - the Painter Test window's toolbar.
- **ImGui's own buttons in title bars and tab bars:** a floating window's collapse arrow and close button, a dock node's window menu button and close button, and a tab's close button.
- **False marks:** ImGui's background in an OS window of its own, which the chrome makes transparent, was still marked.

Left out, as not the editor's panels: the transform gizmo (a scene view tool, overhaul change 6), the UI source inspector's outlines (F8), ImGui's own debug windows, and the Painter Test window's comparisons with ImGui (the *In a window* tab's ImGui buttons, ImGui's widgets in the gallery). The editor's own OS window is rounded already (`WindowsWindow::ApplyRoundedCorners()`).

Split so each part stays reviewable:

- **9a. The editor's own:** the titlebar, its buttons and the panels' widgets above, on painted widgets. New:
  - a button with a colour of its own (Play green, Stop red);
  - a flat menu bar button, with a pill while its menu is open, as in the concepts;
  - window control buttons with painted icons;
  - a multi-line text field (a hybrid like the other fields);
  - an optional hint (placeholder) for `InputText()`.

  The titlebar sits on the painted backdrop while the chrome is on, and the focus border is painted. The marks also look at the foreground and background draw lists, and skip shapes ImGui draws fully transparent.
- **9b. ImGui's own buttons in title and tab bars.** ImGui draws them inside its title bar and tab bar code, with the text colour, and keeps no record of where. The ways to paint them:
  1. **ImGui's item hook (recommended):** with `IMGUI_ENABLE_TEST_ENGINE`, ImGui reports every item it adds, with its rect, and the close and collapse buttons with their names (`#CLOSE`, `#COLLAPSE`). The chrome takes ImGui's glyphs in those rects back to transparent, as it does for the backgrounds of OS windows, and paints the buttons, with hover and press from ImGui's hovered and active IDs. Exact rects, no copy of ImGui's layout. The hook exists for ImGui's test engine; Nyx would use it the same way.
  2. **Flags:** drop the collapse arrow and the dock window menu (`style.WindowMenuButtonPosition = ImGuiDir_None`). This loses the dock menu (its list of a node's tabs). Close buttons can't be removed this way without losing closing.
  3. **Leave them:** they are thin lines in the text colour and look like text. The done criterion then has these exceptions.
  4. **Full replacement:** ImGui makes none of them (no `p_open` passed to ImGui, `NoCollapse`, the dock nodes' `NoWindowMenuButton` and `NoCloseButton`), and painted `Nyx::UI` buttons are submitted in their place. Easy for floating windows' title bars; costly for tab bars: without `p_open` docked tabs lose their close buttons too, whose replacements would go into ImGui's dock host windows over its tab items, and the dock menu (with its list of a node's tabs) would be rebuilt on `imgui_internal.h`.
- **9b deferred** (2026-10-13): left as it is for now (way 3), to come back to if it matters. Way 1 is the recommendation then.
- After 9a and 9b, the main window's draw count shows how close it gets to one draw: ImGui's text between painted widgets still splits runs.

### 10. What painting the whole editor costs

Added 2026-10-13: with the editor painted, an overview of what it costs, per frame and per OS window, to measure the main window's draws and to see where the time goes.

- **CPU, timed in the renderer's frame:** building the UI (the editor's panels, ImGui's layout and the painting they record), painting the chrome, gathering the window painters, `ImGui::Render()`, and the segments (finding runs, appending, binning, uploading).
- **GPU, with timestamps:** the painter's draws, summed per OS window; the main window's whole UI pass (ImGui's drawing and the painter's); the glass backdrop. Read back the next frame, when the frame's fence has passed.
- **Counts per OS window:** segments, draws, commands, tiles and entries, binning time, and what ImGui still draws itself there: draw commands and vertices. For the frame: the bytes uploaded for the segments.
- **The Painter Test window's *Statistics* tab:** the last frame, the average and the peak over the last seconds, a table per OS window, and a graph of the times, painted.
- **Measured in Debug and Release**, with the numbers in the notes.

### 11. A heatmap of the whole editor

Added 2026-10-13, before reducing the tiles' work: where the painter's work lies on the screen, over every part of the editor.

- **The heat:** per 8x8 tile of each OS window, the commands of every draw that covers it, summed (what that tile's pixels walk on the GPU), and how many draws cover it. Draws overlap, so one draw's own counts (the canvas's heatmap) don't show it.
- **Summed by the renderer** from each draw's tile grid (`VulkanPainterSegments::AddHeat()`, into a `PainterHeatGrid` per OS window), while the heatmap is on.
- **Shown over each OS window** (`Nyx::UI::ShowPainterHeatmap()`, `UIDiagnostics.h`), the frame after: rects in ImGui's foreground draw list, merged into runs along each row, in the canvas heatmap's colours. ImGui's draw list rather than the painter, so that the heatmap doesn't add to the painting it measures. A legend in each OS window's corner, and beside the mouse the tile's commands and draws.
- **Two views:** commands per tile, and draws per tile. *Debug > Painter Heatmap* and the Statistics tab switch them.

### 12. Frame rates, and what the proposed changes would bring

Added 2026-10-13, before choosing between the per-tile reduction and binning on the GPU.

- **Frame rates:** the editor's whole frame timed in the renderer, and in the Statistics tab the editor's frames per second and the painter's alone: its CPU time after the UI and its GPU time added, as the editor waits for each frame's GPU before the next.
- **The per-tile reduction, estimated on the real frame:** the renderer walks each tile's entries back to the last command that covers the whole tile opaquely; the entries before it could go. Once with the look as it is, once with the docked cards counted as opaque.
- **Predictions** for the reduction, the reduction with opaque docked cards, binning on the GPU, and both, from the measured times and the estimate, by a stated model. Binning on the GPU can't be measured before it exists: its cost is an assumption, shown with the numbers.

### 13. Binning on the GPU, with the per-tile reduction

Decided 2026-10-13 after change 12's predictions: binning moves to a compute shader, and the per-tile reduction is built into it, so it exists once. The CPU binner goes; it is not kept as a fallback.

- **The CPU measures, once per command** (O(commands)): what each command can touch (its shape, or its group's reach, grown by the anti-aliasing, cut by its clip rect and the target), a line's band, and the rect it hides opaquely (a solid opaque rect less its rounding, cut by its clip rect; none for anything else). Also each segment's grid, the commands over the budget, and an upper bound on the entries (each command's tiles, before the reduction), which sizes the entries buffer.
- **The GPU bins** (`PainterBin.comp`), two passes over every tile of every segment:
  1. One invocation per tile walks its segment's commands in order and counts those that touch it. A command that hides the tile opaquely restarts the count: what lies under it is dropped. Each workgroup reserves its tiles' entries with one atomic; a tile's entries lie together, in drawing order. It also sums, per segment, the entries before the reduction, those kept, those kept if the docked cards were opaque, the tiles in use and the most per tile.
  2. One invocation per tile writes its kept commands' indices.

  Segments start at a whole workgroup, so a workgroup's sums belong to one segment.
- **On the GPU, for the GPU:** tiles and entries are device-local, written and read only by the GPU. Commands, groups and the measured commands are copied to device-local memory each frame, instead of the fragment shader reading them from host-visible memory.
- **Read back** after the frame's fence: the sums each frame (statistics, the reduction's numbers), the tiles and entries while asked for (the heatmap, the tests).
- **The entry budget:** tiles whose entries don't fit are drawn empty, and it is reported. The command budget stays on the CPU.
- **Canvases** bin the same way: a canvas is one segment over its whole target.
- **Tests:** the measuring on the CPU in `NyxPainterTests`; the binning (tiles, order, lines, groups, clip rects, the reduction, the budget) in `NyxPainterGpuTests`, read back.
- **Statistics:** the GPU's binning time (timestamps), the entries before and after the reduction, and in place of change 12's predictions, what opaque docked cards would bring, from the GPU's own count.

## Risks

- **Docking internals:** painted chrome leans on dock node state from `imgui_internal.h`; the hybrid tab bar (change 4) keeps that small.
- **Two layers for glass:** painting the docked layer into an image before the floating panels changes the frame's structure; it is limited to frames with glass panels open.
- **CPU cost of a fully painted editor:** every glyph and row is a command binned each frame. Measured from change 3 on; counting tiles for plain rects by area, and binning on the GPU, are the remedies.
- **ImGui updates:** each update may need the hybrid widgets and chrome checked; the Painter Test window, its *Compare with ImDrawList*, and *Debug > Mark ImGui Shapes* make differences visible quickly.

## As built

### 1. Blur chain

- **`VulkanBlur`** (`VulkanBlur.h`, `Blur.vert`, `Blur.frag`) blurs any image that is ready to sample, into a `VulkanBlurTarget`:
  - halve it `Downsamples` times (4: 1/16 of the size) with the "dual filter" downsample (the centre four times, plus four bilinear taps a source texel away diagonally: 16 texels, the nearer ones weighed more);
  - then `GaussianPasses` 9-tap Gaussians (5 bilinear taps) at the smallest size, along x into a temporary image and along y back.
- **The target** keeps its levels, framebuffers and one sampling descriptor set per level, plus one for the source. It makes them anew when the source's size or the options change, and repoints the source's set when the source's view changes, e.g. a scene view recreated.
  - The result is RGBA8 in `ShaderReadOnlyOptimal`.
  - The render pass doesn't load (every pass writes every pixel) and ends ready to sample, with dependencies like the canvases'.
- **Renderer:** `BlurSceneViews()` runs in `DrawFrame()` after the scene views render and before the UI is built.
  - It blurs every scene view (`SceneViewInstance::Blur`).
  - It gives each blur an ImGui texture: added once, and pointed at the new image in place when the size changes, so the texture ID stays the same.
  - The blur is released with the view, after `RenderTarget.Shutdown()` (which waits for the GPU), and in `Shutdown()`.
- **GPU time:** two timestamps (`BlurTimestamps`, bottom of pipe before and after, so the scene views' own rendering isn't counted), read after the next fence and turned into milliseconds with `timestampPeriod`. Where the graphics queue has no timestamp bits, there is no query pool, and the time reads negative.
- **`IRenderer`** additions:
  - `GetSceneViewBlurTextureId()`, `GetSceneViewBlurExtent()` and `GetBlurMilliseconds()`;
  - `GetSceneViewIds()`, and `GetSceneViewExtent()` (which the renderer already had, now on the interface);
  - `VulkanViewportTarget::GetImageView()`.
- **Painter Test window, *Blur* tab:**
  - pick a scene view by number and size (the largest until one is picked);
  - the blurred size and all blurs' GPU time;
  - the picture, the blur through `ImGui::Image()`, and the blur painted with `AddImage()`, side by side.
- **Tests** (`NyxPainterGpuTests`, 3 new):
  - a flat image stays flat (64 to 4 pixels);
  - a white/black edge becomes a falling gradient, the same in every row, with its mean kept at 0.5;
  - a 32x16 source blurs to 2x1, and with 2 halvings to 8x4;
  - `Blur.png` is saved with `NYX_PAINTER_TEST_OUTPUT`.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored):
  - the *Blur* tab on a 689x368 scene view gives a smooth 43x23 blur, the same through ImGui and painted;
  - all scene views' blurs took about 0.2 ms on the GPU, with no validation messages.
  - The editor took its own screenshot with `SaveScreenshot()`: capturing the window from outside (`PrintWindow`) gives black for a Vulkan window.
- **README:** a sentence on the *Blur* tab.

### 2. Image glow primitive

- **`Painter::AddImageGlow(texture, rect, reach, strength, uvs)`**, also on `WindowPainter`. One `ImageGlow` command (type 7):
  - outside the rect, a pixel takes the texture's colour at the nearest point of the rect, at `strength` at the edge, fading out linearly to nothing at `reach`, like a group's glow;
  - inside the rect, the nearest point is the pixel itself, so the glow there is the image at `strength`. A rounded card drawn over it leaves no unlit gap at its corners;
  - the nearest point of a box is the pixel clamped into it; the signed distance to the rect gives the fade.
- **Command layout:**
  - the payload is `PainterTexturedRectPayload` (rect and UV rect), as for images, which fills its 32 bytes;
  - so the reach went into the header: `PainterCommand::Unused` became `Reach` (a float, 0 for every other type);
  - the strength is the alpha of a white tint (`Color`), so the shader tints it like an image.
- **Bounds:** the rect grown by the reach (`GetShapeBounds()`), scaled by the transform like everything else.
- **Shader** (`ImageGlowColor()` in `Painter.frag`):
  - blended in `main()` instead of going through `ShapeDistance()`: it is light, not a shape;
  - the UV is kept half a texel inside the UV rect (`TextureSize()`, new), so the edge reads its own texels: never an atlas neighbour's, or the far side of a repeating texture;
  - a reach of 0 gives the image's one-pixel anti-aliased edge;
  - in a group it is left out of the group's shape, like a glyph, so it casts no shadow.
- **Sharp corners only:** the glow starts at the rect's edge, not a rounded one. Under a card with rounded corners, the difference near a corner is at most `radius * 0.41` pixels, inside a soft glow. A radius can come later if ambilight (change 5) shows it.
- **Test sheet:**
  - a new row, *Image glows*, before *Text, images*: an image with its glow, the glow behind a rounded card (ambilight), the glow of the image's left half (UVs), and a faint glow reaching far from a thin strip;
  - all from a new gradient image, `MakePainterTestGradient()` (32x16, a colour per corner), instead of the font atlas the plan named: the atlas' edges are nearly all transparent, so its glow showed almost nothing;
  - `DrawPainterTestSheet()` and `DrawPainterTestSheetRow()` take the gradient as a new parameter; the sheet is now 760x640, with 7 rows;
  - the Painter Test window makes the gradient as a UI image (`CreateUIImage()`) the first time, and gives it back in `Release()`;
  - the ImDrawList comparison leaves image glows out, like shadows and glows.
- **Tests:**
  - `NyxPainterTests`: the command (rect, UVs, reach scaled by the transform, strength as the tint, bounds, groups, shared textures, a negative reach and a strength above 1), binning out to the reach, and the sheet with 8 command types and 2 textures;
  - `NyxPainterGpuTests`: around a 2x2 texture, each side glows in its edge texel's colour, at 0.75 4.5 pixels out and 0.25 13.5 pixels out, nothing past the reach, the corner's texel off a corner, the image inside; with half strength and only the top left texel, the glow next to the part's edge is still that texel, not its neighbour;
  - `ImageGlow.png` is saved with `NYX_PAINTER_TEST_OUTPUT`; the GPU tests' texture pool grew from 16 to 32 sets.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored): the *Canvas* and *In a window* tabs show the new row, with no validation messages.
- **README:** image glows in the sentence on the test sheet.

### 3. Painted backdrop and panel cards

- **`UIChrome.h/.cpp`** (engine renderer, `Nyx::UI`): `SetPaintedChrome()`, `IsPaintedChromeEnabled()`, `PaintBackdrop()` and `PaintWindowCards()`. The editor turns it on in `EditorLayer::OnAttach()`; *Debug > Painted Chrome* switches it.
- **ImGui keeps layout, docking and input.** While the chrome is on, `SetPaintedChrome()` changes ImGui's style, and puts it back when turned off:
  - transparent: window, popup and title bar backgrounds, borders, the empty dock node background, and the resize grip colours (which ImGui's splitter highlight also uses);
  - no window or popup border sizes;
  - `DockingSeparatorSize` set to the theme's `CardGap`.
- **The gap is ImGui's own.** The plan had cards inset from their dock nodes, which would have let content that fills a window (a scene view) spill over the card's edge. Instead ImGui leaves `CardGap` between dock nodes, which is also its splitter. So a card is exactly its node's rect, and contents stay inside.
- **Backdrop:** `WindowsWindow::DrawDockspaceHost()` begins its window with `NoBackground` and calls `PaintBackdrop()` right after `Begin()`, so the backdrop is the first thing in that window. Its padding becomes `CardGap`, so the cards keep the gap from the window's edges too.
- **Docked cards:** one per dock node with windows, painted into the dock host's draw list.
  - A dock host draws into two channels until ImGui renders it: docked windows' backgrounds in the first, tab bars in the second. The cards go at the end of the first, so they lie behind the tab bars, and the docked windows render after their host.
  - All cards of a host are one group, so they cast one shadow under all of them and no card's shadow falls on its neighbour. A hairline (`CardBorder`) sits just inside each card.
  - Floating dock hosts (windows docked together outside the dock space) get the floating colour and shadow; one in an OS window of its own gets the backdrop behind it first.
- **Floating cards:** windows, popups, menus and tooltips that are not child windows, not docked, not dock hosts and not `NoBackground`.
  - The card must lie behind the title bar and everything else, which ImGui has drawn by then. So `WindowPainter` gained a placement: **`EWindowPainterPlacement::Behind`** puts a segment's two callbacks at the front of the draw list (after earlier Behind segments) instead of appending them. `GetPainterFor(drawList, viewport, name, placement)` is now public; there is one painter per draw list and placement.
  - A window that fills an OS window of its own gets a plain square card, without shadow.
- **When:** `VulkanRenderer::DrawFrame()` now calls `ImGui::EndFrame()` itself right after `buildUI()`, then `PaintWindowCards()`, before the window painters are gathered. Ending the frame first means the windows ImGui begins while ending it (drag and drop tooltips) are there too. `ImGui::Render()` skips its own `EndFrame()` then.
- **Splitters:** for each split dock node, a thin accent line along the middle of the gap, as long as the cards less their rounding, eased in with `Animate()` while the splitter is hovered (after ImGui's 0.04 s delay) or dragged.
  - Whether it is hovered compares ImGui's hovered and active IDs with the splitter's ID, worked out as ImGui makes it (`"##Splitter"` under the node's ID in the host window). This follows ImGui's internals; the test hovers a real splitter, so an ImGui update that changes it fails there.
- **Theme tokens:** `Backdrop`, `Card`, `CardBorder`, `CardRounding` (8), `CardGap` (6), `CardShadow`, `FloatingCard`, `FloatingCardShadow`, `Splitter`, `SplitterWidth`.
- **Tests:**
  - `NyxPainterTests`: a Behind painter's segments come first in the draw list, in order, before ImGui's drawing, while the draw list's in-order painter stays after it.
  - New **`NyxUIChromeTests`**: a headless ImGui context with a real dock space (two docked windows), a floating window and a `NoBackground` window:
    - the style taken over and given back exactly;
    - nothing painted while off;
    - the gap between docked nodes, each node's card exactly its rect, the theme's colour and rounding, one shared shadow, and the cards before everything else in the rendered host with the tab bars after;
    - the floating card first in its window's draw list, and no card for `NoBackground` windows;
    - no splitter line until the mouse rests in the gap, then one upright line along its middle.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages:
  - docked cards on the backdrop, with gaps and soft shadows;
  - a floating window and a tooltip with floating cards;
  - the chrome switched off mid-run gives ImGui's own look back;
  - `NyxGame` still runs.
- **Not checked by hand yet:** menus and combo popups (the same path as the tooltip), dragging a splitter, windows in OS windows of their own, and floating dock hosts.
- **Known limits:**
  - content drawn right to a window's edge (without window padding) would have square corners over the card's rounded ones; the editor's panels have padding, so none shows it now;
  - tabs keep ImGui's colours until change 4.
- **README:** a sentence on the cards, *Painted Chrome* and `NyxUIChromeTests`.

### 4. Painted tab bars

- **The hybrid reaches the look, so tab bars are not painted fully.** ImGui keeps each tab's behaviour (select, drag to undock or reorder, close, context menu), its label and its close button. The chrome makes its tab colours transparent and paints the shapes behind the labels. Hiding ImGui's tab bars and rebuilding them on `imgui_internal.h` is not needed.
- **Style:** `ImGuiCol_Tab`, `TabHovered`, `TabSelected`, `TabSelectedOverline`, `TabDimmed`, `TabDimmedSelected` and `TabDimmedSelectedOverline` join the colours made transparent while the chrome is on. ImGui 1.92 also uses `TabSelected` for its line under a tab bar, so that goes too.
- **What is painted** (`PaintTabBar()` in `UIChrome.cpp`), for every tab bar visible this frame:
  - a line (`Separator`) under the whole bar, where ImGui's own was;
  - a pill behind the tab whose contents show (`VisibleTabId`), and a fainter one behind a hovered or held tab, `TabPillInset` from the bar's top and bottom, each eased with `Animate()`;
  - an accent underline under the shown tab, inside its frame padding: `TabUnderline` where the tab bar has focus, `TabUnderlineDimmed` where it hasn't. Its ends are eased relative to the bar, so it slides to a newly selected tab and doesn't trail behind when the window moves.
  - Tabs are placed as `TabItemEx()` places them (offset, minus the scrolling for tabs in the bar's middle section), clipped to the bar's scrolling area. Tab buttons (`TabItemButton()`) are left to ImGui.
- **Dock tab bars** are painted in `PaintDockHost()`, after the cards, in the host's background channel; the labels are in the next channel. A dock tab bar has no ID, so its node's ID keys its animations. Hidden tab bars are skipped.
- **Tab bars in windows** (`ImGui::BeginTabBar()`, kept in `g.TabBars`) are painted with a Behind painter into their window's draw list, after the floating cards, so a tab bar in a floating window lies on its card. ImGui counts these as always focused, so their underline is the accent.
  - A tab bar inside a child window that draws its own background would be covered by that background. The editor has none.
- **Theme tokens:** `TabSelected`, `TabHovered`, `TabPillInset`, `TabUnderline`, `TabUnderlineDimmed`, `TabUnderlineWidth`.
- **Tests** (`NyxUIChromeTests`, 3 new; the dock space now has a second tab in the left node and a tab bar in the floating window):
  - docked: a pill behind the shown tab and none behind the other, the underline's ends, height and colour by focus, the line under the bar, and a pill for the other tab once hovered;
  - in a window: the tab bar painted after the window's card, both behind ImGui, with the accent underline;
  - selecting the other tab: the frame ImGui switches, the underline is part of the way there; later it is under the new tab.
  - The splitter test now looks at upright lines only, since underlines are lines too.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: docked tabs with pills and dimmed underlines, and the Painter Test window's tabs with the accent underline.
- **Left as it is:** ImGui draws every tab label in the text colour; unselected labels aren't muted.
- **README:** the tab bars in the sentence on the chrome.

### 5. Ambilight

- **`Nyx::UI::AddWindowGlow(texture, rect, reach, strength)`** (`UIChrome.h`): a window asks, between its own `Begin()` and `End()`, to glow with a picture around a rect, for this frame. The chrome keeps the frame's glows and paints each with its window's card. Nothing while the chrome is off, for a reach or strength of 0, or for a window that fills an OS window of its own.
- **Where the glow goes:**
  - Docked: in the dock host's painter, for the window each node shows (`VisibleWindow`), before every card of the host. A glow therefore lights the backdrop and lies under all its neighbours' cards.
  - Floating: in the window's Behind painter, before its card.
  - **In the cards' group, as its first shapes.** The shader draws a group's shadow before the group's first shape, so the glow lies over the cards' shadow, not under it. The first try put it before the group: the shared shadow then darkened the gaps between cards, the only place the backdrop shows in a full layout, and the glow was barely visible. A glow casts no shadow itself (change 2), so it doesn't change the group's shadow. A floating card with a glow is now a group of card and glows too.
- **Translucent cards:** the theme's docked `Card` colour has an alpha of 0.85, so a glow shows faintly through the cards beside a scene view. Floating cards stay opaque, so menus and tooltips stay readable. A separate weaker glow sampled under each card, which the plan left optional, isn't needed for now.
- **Scene views** (`SceneViewPanel::Draw()`, which now takes the preferences): with ambilight on, each view adds a glow of its blurred picture (`GetSceneViewBlurTextureId()`, change 1) around the rect its picture shows in. The blur is 1/16 of the picture's size and redone every frame, so the glow is soft and follows the picture.
- **Preferences** (`EditorPreferences`, `Editor.ini`): `Ambilight=1`, `AmbilightStrength=0.8` (0 to 1) and `AmbilightReach=60` (pixels, up to 400).
  - Read values outside the range are clamped, and unreadable ones skipped.
  - Floats are written with `std::to_chars`, so they read back exactly.
  - The Preferences window has an *Ambilight* section: a checkbox, which saves at once, and *Strength* and *Reach* sliders, which save when let go.
- **Tests:**
  - `NyxUIChromeTests`: docked windows that show glow once each, with the strength and reach asked for, before every card and in the cards' group; a floating window's glow comes before its card, in its group; a glow lasts for the frame it was asked in only.
  - `NyxPlayWallTests`: the ambilight settings round-trip, their defaults, clamping and unreadable lines.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages:
  - the gaps around the scene views take the pictures' colours, blue below the horizon, warm by the sky;
  - the panels beside them are faintly tinted.
  - With the editor's full layout only the gaps and margins show the backdrop, so the effect is subtle; the strength went from 0.6 to 0.8 by default after looking at it.
- **Not checked by hand yet:** the Preferences controls, and a scene view floating outside the dock space.
- **README:** a sentence on ambilight and its preferences.

### 6. Glass

- **A new primitive, `Glass`** (type 8, `PainterGlassPayload`: rect, one corner radius, refraction, edge width, rim strength), through `Painter::AddGlass(backdrop, rect, radius, style, glass)` and `WindowPainter::AddGlass()`. It is added through the checklist in `PainterCommands.h`.
  - The backdrop is a picture as large as the target; each pixel shows it at its own place (`gl_FragCoord / TargetSize`). So glass needs no texture coordinates, and the payload has room for the bend and the rim.
  - The style's colour is the **tint**, laid over the picture at its alpha. The style's effects give the glass a shadow, as for any shape; it is part of a group's shape, unlike a glow.
  - `PainterGlass`: `Refraction` (pixels the picture is pulled in from outside at the edge), `EdgeWidth` (how far in the bend and the rim reach), `RimStrength` (0 to 1). All but the strength scale with the transform.
- **Shader** (`GlassColor()` in `Painter.frag`):
  - the edge's outward direction is the distance's gradient, from four extra distance evaluations (central differences, half a pixel apart);
  - near the edge (`(1 + distance / EdgeWidth)²`, clamped) the picture is read from `pixel + outward × Refraction × nearEdge`, so the edge shows what lies just beyond it, bent;
  - the tint goes over the picture, then a rim of white light along the edge, strongest where the edge faces up and to the left (0.35 to 1 of `RimStrength`).
  - `TargetWidth`/`TargetHeight` in the push constants, used by `Painter.vert` until now, map pixels to the backdrop.
- **Glass in the editor** (`UIChrome.cpp`): floating cards in the main window are glass with the theme's `GlassTint` and `Glass`, and so are cards of windows docked together outside the dock space. Windows in OS windows of their own stay opaque: they can't see the desktop behind them. Windows' Mica or Acrylic is left for later.
  - The chrome notes the glass windows' draw lists every frame (`GetGlassDrawLists()`). It paints glass only once the renderer has handed it a backdrop (`SetGlassBackdrop()`), so the first frame with glass ever shows opaque cards.
- **The backdrop** (`VulkanGlassBackdrop`, `VulkanGlass.h/.cpp`; `VulkanRenderer::DrawGlassBackdrop()`):
  - `DrawFrame()` now calls `ImGui::Render()` itself, after the painters are prepared. If there is glass, it draws a copy of the main window's draw data, cut before the first glass window's draw list, into an image of its own, then blurs it (halved 3 times, to 1/8) before the main render pass. The backend's `DrawFrame()` finds the draw data already made.
  - The image's render pass has exactly the swapchain's format and one dependency, so ImGui's main pipeline and the painter's segment pipeline draw into it unchanged. The painter's segments in the docked layer are drawn twice, from the same upload.
  - ImGui's backend takes fresh vertex buffers for each draw, from as many as the swapchain has images; two draws a frame are safe, as each frame waits for the last.
  - **The first draw must update ImGui's textures.** The first run left the copy's texture list out and hit ImGui's assertion that the font atlas had no texture yet; the copy keeps it, and the main draw finds the textures done.
  - The blur's ImGui texture keeps its ID. `PointImGuiTexture()` now does that for the scene views' blurs and the backdrop alike.
- **What glass sees** is everything ImGui draws in the main window before the first glass window: the backdrop, the docked cards and scene views, and floating windows below the first glass one. A glass window on top of another glass window sees through both to the docked layer.
- **Fixed on the way, from change 5:** the scene view's ambilight reached up over the custom titlebar, which the dock space's window draws before the dock space. A dock space's host now clips its cards' shadows and glows to the dock space grown by `CardGap`, and with the chrome on `WindowsWindow` starts the dock space exactly `CardGap` below the titlebar (instead of the padding hack).
- **Test sheet:** a *Glass* row before *Text, images* (the sheet is now 760x720, 8 rows): lightly tinted glass, dark glass bent strongly, and a pill with a shadow and a bright rim. The gradient stretched over the sheet stands in for what lies behind. The ImDrawList comparison draws glass as a rounded rect in its tint.
- **Tests:**
  - `NyxPainterTests`: the glass command (rect, radius fitted, bend scaled, rim strength clamped, tint, no outline, backdrop texture, a shadow group, bounds), and the sheet with 9 command types.
  - `NyxPainterGpuTests`: clear glass shows each quarter of a 2x2 backdrop at its own place; tinted glass ending just left of a colour boundary shows the colour beyond it at its edge, and its own place inside; the rim's light over black, brighter on the edge facing up than down, nothing away from the edge. `Glass.png` is saved with `NYX_PAINTER_TEST_OUTPUT`.
  - `NyxUIChromeTests`: without a backdrop the floating card is opaque but noted as glass; with one it is glass of the window's rect, with the theme's tint and bend, the backdrop and its shadow; with the chrome off nothing is glass.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the Painter Test window floating over the scene view and a tooltip are frosted, showing the blurred sky behind them with their rims and shadows; the titlebar keeps its own colour. `NyxGame` still runs.
- **Not checked by hand yet:** menus and combos over the scene view, dragging a floating window around, and windows docked together floating.
- **README:** glass in the sentences on the test sheet and on the chrome.

### 7a. Tree rows

- **`Nyx::UI::TreeRow(label, options)`** (`UIWidgets.h`), returning `TreeRowResult { bClicked, bOpen }`, and `TreeRowEnd()`. A painted widget on ImGui's behaviour functions, like stage 2's:
  - `TreeRowOptions`: `bSelected`, `bLeaf`, `bDefaultOpen`, `Icon` (`ETreeRowIcon`: none, folder, entity).
  - The open state is kept in the window's storage per ID, as `TreeNodeEx()` keeps it; open rows push the ID and indent (`TreePushOverrideID()`), and `TreeRowEnd()` is `TreePop()`. A row scrolled out of view still pushes when open, so the calls stay paired.
  - Clicking the arrow toggles the row; clicking the rest of it, or activating it by keyboard, selects it (`bClicked`). A double click on the label toggles too. Left and Right with keyboard focus close and open it, cancelling the nav move, as ImGui's tree nodes do.
  - The row and its pill span the window's work rect at any indentation. For layout it is only as wide as what it shows (arrow, icon, label), as ImGui's `SpanAvailWidth` tree nodes are, so a column sized to its contents doesn't grow from it.
- **`PaintTreeRow(painter, rect, label, TreeRowLook, WidgetLook)`**:
  - a selection pill (`RowSelected`, faded in) and a hover pill (`RowHovered`) over it, `RowPillMargin` in from the ends and `RowPillInset` from top and bottom, rounded like frames; an accent outline while it has keyboard focus;
  - a guide (`IndentGuide`) for each level the row is inside (`DC.TreeDepth`), under the arrows of the rows above. Each row's piece reaches half the item spacing past its top and bottom, so the pieces join; the first look had gaps between rows.
  - the arrow: a small triangle pointing right, turned down as the row opens (eased). Leaves keep its room, so their labels line up with their siblings'.
  - the icon, painted from shapes until there is an icon font (overhaul change 9): a folder (a tab and a body, `FolderIcon`) or an entity (a rounded square, `EntityIcon`).
  - the label, painted.
- **Used by:**
  - the outliner: every entity is a leaf row with the entity icon (no hierarchy yet), selected on click; right-click still opens its menu, and clicking empty space still clears the selection;
  - the asset browser's folder tree: folder rows, open on the root from the start.
  - The asset browser's file list stays ImGui's until the asset cards (7d).
- **Not built:** visibility eye toggles in the outliner. The engine has no visibility to toggle yet.
- **UI gallery:** rows *Tree row* (a folder one level in, every state; live, a folder with one inside; ImGui's `TreeNodeEx` beside it) and *Tree row, open, selected*.
- **Tests** (`NyxEditorPanelTests`): the row spans the window's width and reports hover; a click on the label selects without opening; a click on the arrow opens without selecting, and the child is indented by `IndentSpacing`; the arrow closes it again; a double click on the label opens and selects; with keyboard focus, Left closes and Right opens.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the asset browser's folder tree with icons, arrows, joined guides and the selection pill, and the gallery's rows.
- **Not checked by hand yet:** the outliner's rows, and keyboard use in both trees.
- **README:** the tree rows in the sentence on the gallery.

### 7b. Property rows and text fields

- **Fields are hybrids:** ImGui's own `InputText()`, `DragScalar()` and `InputScalar()`, over a frame painted just before them. They keep ImGui's editing, dragging, typing after a double click or Ctrl+click, and their text. Rebuilding text editing on ImGui's behaviour functions, as the stage 2 widgets are built, would mean rewriting `InputTextEx()`.
  - While ImGui draws them (`ScopedFieldStyle`), its frame colours and keyboard cursor are transparent and its frame border is 0; the text selection and the text cursor take the theme's colours. Everything is given back after.
  - The frame goes where ImGui's will: at the cursor, `CalcItemWidth()` wide and a frame high. The test checks the painted rect is exactly ImGui's frame.
  - **The frame is painted before ImGui's widget runs**, so the text lies on top. Hover is only known after it runs, so the frame uses ImGui's hover of the frame before (`HoveredIdPreviousFrame`), eased; being edited is `ActiveId`, known before. ImGui's draw list has no place to put shapes behind a widget after the fact; channels are taken by tables.
- **`Nyx::UI::InputText(label, std::string*, flags)`**, **`DragScalar()`**, **`DragFloat()`**, **`InputScalar()`**: the frame is darker than the card (`Field`), lighter when hovered (`FieldHovered`). While edited: an accent border and a faint glow (`FieldEditGlow`); the focus glow with keyboard focus.
- **`SearchField(label, std::string*, placeholder)`**: round ends and a painted magnifier (`SearchIconSize`). ImGui's `InputTextWithHint()` draws the text and the placeholder, with its frame padding moved past the magnifier (`CalcSearchTextOffset()`). No clear button yet: an item over the field would take its hover.
- **`DragVector(label, values, count, speed, format)`**: one `DragFloat()` per component, sharing the item width, each behind its axis letter (`AxisX` to `AxisW`). The letter has room of its own at the frame's left (`CalcAxisTagWidth()`), and ImGui's field starts after it, so the centred value never covers it. The components are one group, as `DragFloat3()`'s, so `IsItemActivated()` and `IsItemDeactivatedAfterEdit()` after it cover all of them; the details panel's undo works unchanged.
- **`BeginPropertyRow(label)` / `EndPropertyRow()`**: a row across the window, a frame high.
  - The label column is the row's item (no keyboard navigation), so `IsItemHovered()` after `BeginPropertyRow()` is the label's, for tooltips. The label is painted after a tree row's arrow's room, so it lines up with tree rows' labels, and cut with "..." to fit (`PropertyLabel`).
  - The value goes on the same line at `CalcPropertyValueX()` (`PropertyLabelWidth` of the row), the same place at any indentation, with the item width to inside the hover pill's end.
  - The hover pill and the guides are the tree rows'. Hover is the mouse anywhere in the row, also while one of its fields is dragged.
  - Like ImGui's full-width items, a row takes its window's width; in a window or column sized to its contents it would shrink each frame. The gallery's live row is in a child window of its own for that.
- **`CollapsingHeader(label, bDefaultOpen)`**: a pill across the window (`Header`, `HeaderHovered`), an arrow that turns, the label. It keeps its open state in the window's storage under the label's ID, as ImGui's does, so a header moved from ImGui's to the painted one keeps its state. Left and Right close and open it.
- **`TreeRowOptions::bToggleOnClick`**: a click anywhere on the row opens and closes it, for headings in a list. No `bClicked` then, and no extra toggle on a double click.
- **Shared painting:** the arrow, the indentation guides and the row pill are helpers now, used by tree rows, property rows and headers.
- **The details panel** (`ReflectedPropertyDrawer.cpp`) has no table any more:
  - each property is a property row; categories and structs are tree rows with `bToggleOnClick`, open from the start, with their rows inside, so they get guides;
  - components are `CollapsingHeader()`s, and *Add Component* is a painted button;
  - the property widgets (`PropertyWidgetRegistry.cpp`) use the hybrid fields: drags for ints and floats, `DragVector()` for vectors and rotations in degrees, `InputScalar()` in hex for IDs, `InputText()` for strings. Checkboxes stay ImGui's until 7c.
  - Removed with the table: the unused `DrawReflectedTypeRows()`, `DrawCategoryRow()` and `DrawCategoryHeader()`, and the category header drawn with `ImDrawList`.
- **Game Link:** the Game Log's filter is a `SearchField()`; its buffer became a `std::string`.
- **Left as ImGui's:** the multiline text fields (crash cards, message details), and text fields in popups (Save As, play setups), which come with the popups in 7c; the mockup overlay's path field and offset.
- **UI gallery:** rows *Text field*, *Search field*, *Number field*, *Vector field*, *Property row* and *Collapsing header*. Their Pressed column shows the field being edited; the forced states paint a sample text, since ImGui draws the live ones'.
- **Tests** (`NyxEditorPanelTests`, 1 new):
  - a property row's value fills its column from the row's top, rows are a frame high, a vector's components share the column, and a row inside a tree row keeps the value column;
  - the number field's frame is painted exactly at ImGui's frame, and ImGui's style stacks are as before;
  - the label reports hover;
  - dragging a number raises it and commits one edit; a double click and typing sets it;
  - dragging a vector's Y changes only Y, and the group reports the commit;
  - typing at a text field's end changes the text;
  - a collapsing header starts open, closes on a click, and ImGui's header under the same label finds it closed.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the details panel of a mesh entity (framed fields, X/Y/Z letters in colour, categories and structs with guides, the read-only guid faded, the headers and the button), the Game Link filter as a search pill, and the gallery's new rows in every state.
- **Not checked by hand yet:** hover and editing in the details panel, typing and Escape in the fields, and the keyboard through property rows.
- **README:** the new widgets in the sentence on the gallery, and where they are used.

### 7c. Combos, menus, checkboxes, colour swatches and scrollbars

- **A second kind of hybrid: ImGui's widget drawn invisible, painted after it.** For menu items and selectables, ImGui keeps what makes them work in menus and popups: closing the popup on a click, keyboard navigation, hovering to move the keyboard's place, multi-select. While ImGui draws them (`ScopedInvisibleItem`), their highlight, text, disabled text and keyboard cursor are transparent; ImGui's draw list skips shapes and text with no alpha, so it draws nothing of them. Then they are painted at the rect ImGui laid out, with the state ImGui has after running. Unlike the fields (7b), which must lie under ImGui's text, nothing lags a frame.
- **`Nyx::UI::Checkbox(label, bool*)`**: a painted widget on `ButtonBehavior()`, like the toggle. A rounded box `CheckboxSize` of the frame height: a field when off, the accent when on, with a tick (`PaintCheckmark()`, two round-capped strokes) drawn in as it turns on. It shrinks a little while pressed.
- **`MenuItem(label, shortcut, bSelected, bEnabled)`**, and the `bool*` form: as `ImGui::MenuItemEx()` in a menu: the label, shortcut and tick columns' widths go to the menu's layout (`MenuColumns.DeclColumns()`), and an invisible `Selectable("")` under `PushID(label)`, the same ID as ImGui's, is the behaviour. Painted: the hover pill (`MenuItemHovered`), the label, the shortcut right-aligned and muted, and an accent tick at the right end. The item the keyboard is on shows as hovered, as in ImGui's menus. In a menu bar it is ImGui's own.
- **`Selectable(label, bSelected, flags, size)`**: ImGui's, invisible; painted with the selection pill (`RowSelected`), the hover pill and the label, cut with "..." where ImGui would clip it. With `SpanAllColumns` in a table its pill would be clipped to the column; the tables (7d) don't use it yet.
- **`Separator()`**: as ImGui's horizontal one, a painted line (`Separator`). Upright ones in menu bars and the old `Columns()` stay ImGui's.
- **`BeginCombo(label, preview)` / `EndCombo()`** and **`Combo(label, int*, items)`**: the frame is a painted widget on `ButtonBehavior()`, as in `ImGui::BeginCombo()`: a button-like frame, the chosen item cut to fit, and a chevron that turns up while the list is open. The list is ImGui's own popup (`BeginComboPopup()`, `EndCombo()`), placed and sized as ImGui's combos' lists, so it is a glass card with the chrome on; its items are `Selectable()`s.
- **`ColorSwatch(label, float* rgb)`**, returning `ColorSwatchResult { bChanged, bEditStarted, bEditEnded }`: a swatch the item width wide, in the colour, with a hairline (`SwatchBorder`, `SwatchBorderHovered`). A click opens ImGui's `ColorPicker3()` in a popup under it. The picker is one group, so its activation and deactivation tell where an edit starts and ends: each drag in it is one undo step, as with the other fields. The first try made opening and closing the picker one edit; that missed the close when the selection changed while it was open.
  - The picker itself is ImGui's, with ImGui's shapes. A painted picker can come later.
- **Scrollbars** (`UIChrome.cpp`): `ScrollbarBg` and the three grab colours join the colours the chrome makes transparent. `PaintScrollbars()`, after the tab bars in `PaintWindowCards()`, paints a pill where ImGui's grab is for every active window with a scrollbar, child windows included. The grab is laid out as `ImGui::ScrollbarEx()` lays it out (padding, the share shown, the scroll position, the fade of a short vertical scrollbar), worked out again from the window. The pill is `ScrollbarThumbWidth` wide, `ScrollbarThumbHoveredWidth` while hovered, in the accent while dragged (`ScrollbarThumb`, `ScrollbarThumbHovered`, `ScrollbarThumbHeld`). It is painted at the end of the window's draw list, over its contents, which ImGui keeps clear of its scrollbars. ImGui's layout and scrollbar size are unchanged.
- **Theme tokens:** `CheckboxSize`, `CheckboxRounding`, `CheckmarkWidth`, `MenuItemHovered`, `SwatchBorder`, `SwatchBorderHovered`, `ScrollbarThumb`, `ScrollbarThumbHovered`, `ScrollbarThumbHeld`, `ScrollbarThumbWidth`, `ScrollbarThumbHoveredWidth`.
- **Used by:**
  - every checkbox in the editor's panels (Game Link, Play's menu and setups, Preferences, the Painter Test window's toolbar) and the details panel's bools;
  - every menu item and separator in the titlebar's menus, the Play menu and the outliner's and details panel's menus;
  - the Load Scene popup's list and the asset browser's file list (until the asset cards, 7d);
  - the Preferences' source editor combo (its `"\0"`-separated list became an array) and the Painter Test window's scene view combo;
  - the details panel: a vec3 with `UI = Color` is a colour swatch; the directional light's `Color` is one now, instead of three drags.
- **Text fields in popups and the play setups**, left from 7b: the Save Scene As name and the play setups' names are `InputText()` on `std::string`s now (their `char` buffers went), and the setups' sizes `InputScalar()`. Preferences' ambilight sliders are the painted `SliderFloat()`.
- **Left as ImGui's:** buttons other than those changed before, including the titlebar's (a first try painted File, Window and Debug, which then didn't match Play beside them); `SeparatorText()` in the Play menu; the colour picker; the modal popups' dimming and title text; the Game Link tables (7d).
- **UI gallery:** rows *Checkbox, off*, *Checkbox, on*, *Menu item*, *Menu item, ticked*, *Selectable, selected*, *Combo*, *Combo, open* and *Colour swatch*.
- **Tests:**
  - `NyxEditorPanelTests` (1 new): a click ticks a checkbox; in a menu, ImGui draws nothing of painted menu items and separators, a disabled item neither runs nor closes the menu, a click runs an item and closes the menu, a `bool` item flips; a click opens a combo's list and a click on an item chooses it and closes the list; a click on a swatch opens its picker without starting an edit, and a drag in the picker changes the colour and starts and ends one edit.
  - `NyxUIChromeTests` (1 new, and the style test): ImGui's scrollbar colours are transparent while the chrome is on; a window with 40 lines has a thin thumb in the middle of its scrollbar, at the top and shorter than half of it, at the bottom when scrolled to the end, and wider while hovered.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the details panel of the sun (the white colour swatch, the ticked Primary checkbox, thin scrollbar), the gallery's new rows, the Window menu (glass card, shortcut, accent tick on Game Link) and the Load Scene list.
  - Earlier runs (7a, and the first ones here) found no menu in the shots. A menu opened by code opens at the mouse, which was outside the editor's window, so it went into an OS window of its own, outside the shot. Placed under its button, it shows.
- **Not checked by hand yet:** the colour picker and its undo, choosing in the Preferences combo, the keyboard in menus and combos, and dragging a scrollbar.
- **README:** the new widgets in the sentence on the gallery, the scrollbars in the one on the chrome and its tests.

### 7d. Asset cards, tables, the status bar, tooltips and notifications

- **The status bar and notifications aren't built.** The editor has neither yet; as with the outliner's eye toggles (7a), there is nothing to paint. They come when they exist, as painted widgets from the start.
- **Tooltips need nothing more:** they are floating windows, so they are glass cards with the chrome on (change 6); their text is ImGui's.
- **`Nyx::UI::AssetCard(label, AssetCardOptions)`**, returning `AssetCardResult { bClicked, bOpened }`: a painted widget on `ButtonBehavior()` (pressed on click), `CalcAssetCardSize()` big (`AssetCardWidth` wide).
  - A click selects; the second click of a double click opens as well; activated by keyboard, it selects and opens.
  - The picture is a square: the thumbnail when `Thumbnail` is set (none yet: thumbnails need the thumbnail renderer), else a painted folder (the tree rows' folder icon), or a page with its corner folded (`FileIcon`) over the type's name (`TypeLabel`). Under it the name, centred and cut with "...".
  - Lit while hovered (`AssetCard`, `AssetCardHovered`); selected, in `RowSelected` with an accent outline; outlined with keyboard focus.
- **The asset browser** shows its folder's contents as a grid of cards, as many side by side as fit (`SameLine()` while they do). Files show their name without the extension, which the type shows; the whole name is the card's ID and its tooltip.
  - A click now selects (`SelectedAsset`, new) instead of entering a folder; a double click enters a folder or opens an asset, as a double click opened assets before.
- **Tables: ImGui's, with their backgrounds and borders made transparent and painted instead.** `Nyx::UI::BeginTable()`, `TableNextRow()`, `TableHeadersRow()`, `TableClipperStep()` and `EndTable()` take the place of ImGui's; the rest of its table API is unchanged.
  - `BeginTable()` pushes ImGui's table colours (header and row backgrounds, borders, and the separator colours its column borders take when hovered or dragged) transparent, before `ImGui::BeginTable()`: a scrolling table is a child window, which must end with the style stack it began with. The first try pushed the separator colours just before `ImGui::EndTable()`, and ImGui asserted on the unbalanced child window.
  - **Rows are painted when they end, behind their cells.** A row's height is known only once its cells are in. Just before a row ends (the next `TableNextRow()`, `EndTable()`), its bottom is worked out as ImGui's `TableEndCell()` will (the lowest cell, the open one included), and it is painted into the table's background channel, the one ImGui draws row highlights into (`ScopedTableBackground`, as `TablePushBackgroundChannel()`, but restoring any channel). That channel renders before the cells', so the shapes lie behind them.
  - The header row is painted right after `ImGui::TableHeadersRow()`, while it is still open: with frozen rows, ending it moves the row's place and the background channel to the scrolling rows'.
  - **`TableClipperStep(clipper)`** paints the open row, then steps the clipper. `ImGuiListClipper` ends rows itself and then moves the row's bottom to where it seeks, so a row it ended can't be painted after.
  - Painted: a strip behind the header (`TableHeader`) with a separator between its cells, where ImGui's column borders are; with `ImGuiTableFlags_RowBg`, a rounded stripe (`TableRowStripe`) on the rows ImGui would colour (its own stripe counter, which the clipper keeps right). Body columns get no lines. A column border being dragged, or hovered past ImGui's short delay (`HoveredColumnBorder`, `ResizedColumn`), shows as an accent line (`Splitter`) down the table, painted after it.
  - `Selectable()` with `ImGuiSelectableFlags_SpanAllColumns` in a table paints its pills across the row, in the background channel too; its label stays in its cell.
- **The window painter** now also notices a table switching channels by the command buffer's memory changing: tables switch channels with a splitter of their own, not the draw list's, so the channel index it compared never changed, and only the buffers' sizes told. Two channels' buffers of the same sizes would have joined shapes into the wrong segment.
- **Used by:** the Game Link window's Game Log and Messages tables (the selected message's row across the table), the Play Setups table (its Up and Delete buttons are painted buttons now), and the UI gallery's own table, which has stripes now.
- **UI gallery:** rows *Asset card* (a mesh; ImGui's column shows the old list line), *Asset card, folder, selected*, and *Table*: a small painted table with a header, stripes and a selected row, live, and ImGui's beside it.
- **Theme tokens:** `TableHeader`, `TableRowStripe`, `AssetCardWidth`, `AssetCardPadding`, `AssetCard`, `AssetCardHovered`, `FileIcon`.
- **Tests** (`NyxEditorPanelTests`, 1 new):
  - a click on an asset card selects it without opening it, and a double click opens it;
  - a table with a header and four rows paints the header strip at the header's height, stripes on the second and fourth rows one row apart, and two separators between three header cells;
  - a row selected by its spanning selectable gets a pill across the table;
  - a hovered column border shows an accent line;
  - in a scrolling table with a clipper, every stripe is a row high and two rows from the next, the rows the clipper ends included.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the asset browser's folder cards and scene cards, the Play Setups table and the Game Log table with sample lines (header strip, separators, stripes).
- **Known limit:** the UI gallery's table is wider than the Painter Test window, so its last columns are squeezed; widen the window to see the live widgets.
- **Not checked by hand yet:** selecting and opening assets with the mouse and keyboard, resizing table columns, and selecting messages in the Messages tab.
- **README:** the asset cards and tables in the sentence on the gallery.
- **Change 7 is complete**, but for the status bar and notifications, which don't exist yet.

### 8a. One draw per run of segments

- **Runs:** segments that follow each other in an OS window's render order, with nothing ImGui draws between them, are drawn as one. `Nyx::UI::FindWindowSegmentRuns(drawData, breakBefore)` (`WindowPainter.h`) walks a viewport's draw data, made by `ImGui::Render()`, list by list and command by command:
  - a segment's callback joins the open run;
  - ImGui's render state resets, and commands without elements, come between without ending it;
  - a command with elements, or any other callback, ends it;
  - so does the start of each draw list in `breakBefore`. The renderer passes the glass windows' draw lists (`GetGlassDrawLists()`): the glass backdrop is drawn from the draw data cut before the first of them, and a run across the cut would be drawn half in the backdrop.
  - Runs reach across draw lists, e.g. from a window's last segment into the next window's card, and across clip rect changes, which used to start a segment of their own: every command keeps its clip rect.
- **`Painter::AppendSegment(source, segment)`** copies a segment's commands to the end of a painter's frame as they are, in target pixels with their clip rects. The groups they use come along once each, renumbered; their textures are found or added among the frame's, and textured commands (glyphs, images, image glows, glass) point at them. All or nothing: false, with nothing copied, when the frame has no room for the segment's textures (`PainterMaxTextures`).
- **The renderer** now handles window painting in two steps around `ImGui::Render()`, since runs only show in the finished draw data:
  - `GatherWindowPainters()`, where `PrepareWindowPainters()` was, ends the window painters and collects their problems;
  - `UploadWindowSegments()`, right after `ImGui::Render()` and before the glass backdrop is drawn, finds each OS window's runs. A run of one segment is added as before. A longer one is copied into a painter of its own (`RunPainters`), as many segments as its textures allow, and added as one segment: one binning, one draw. A run that runs out of texture room goes on in a next draw.
  - Each segment's callback looks up what it draws (`WindowSegmentDraws`): its run's draw at the run's first segment, nothing at the others. Only this frame's painters' segments are drawn.
  - The run painters are kept apart, one object per draw: `VulkanPainterSegments` tells painters apart by their address within a frame.
- **Statistics** (`PainterWindowStats`): `Draws`, and per OS window (`OsWindows`) its name, segments and draws. The Painter Test window's *In a window* tab shows them, and the count of ImGui shapes while they are marked.
- **In the editor's first frames:** the main window's 10 segments are drawn in 6 draws. ImGui's own drawing between the painting (text, the titlebar, the remaining ImGui buttons) keeps it from being one: a fully painted OS window is one draw, and the marks below show what stands in the way.
- **Debug > Mark ImGui Shapes** (`UIDiagnostics.h/.cpp`, new): outlines, in red over everything in its OS window, each shape ImGui still draws itself.
  - ImGui draws shapes and text from the same texture, its font atlas: shapes read the atlas's white pixel or its baked lines, glyphs the rest. A triangle all of whose corners read those is a shape; text and images aren't marked.
  - A shape is the triangles of one primitive: they use vertices of their own, after the previous primitive's, so a triangle using none of the current shape's vertices starts the next.
  - Marked once the frame has ended and the chrome is painted (after `PaintWindowCards()`), with the foreground painters; capped at 4000 marks a frame.
  - `GetImGuiShapeCount()` and `GetImGuiShapeWindowCount()` count them.
  - In the editor's first frames it marks 30 shapes in 4 windows: the titlebar's buttons and window controls (Play included), the outliner's and Game Link's ImGui buttons, the dock nodes' menu and close buttons, and the Game Log's scrolling region's background. They are what is left for a painted editor.
- **Tests:**
  - `NyxPainterTests`: two painters' segments appended into one renumber their groups and share a texture both use; part of a segment brings only its own; a full run refuses a segment with another texture and stays as it was. In real draw data, segments run across a clip rect change and into the next window, stop at ImGui's text, and stop before a draw list asked to break before. A button's frame is the one shape marked in a bare window, at its rect, and its text isn't; nothing is marked while marking is off.
  - `NyxPainterGpuTests`: another painter's segment and the test sheet in two halves, drawn as one run, paint what the three segments paint one after the other. `SegmentRun.png` is saved with `NYX_PAINTER_TEST_OUTPUT`.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), with no validation messages: the editor looks as before with the runs merged, and the marks outline the shapes listed above. `NyxGame` still runs.
- **Not checked by hand yet:** runs in an OS window of its own, and glass windows over merged runs.
- **README:** the runs and the marks in the sentence on the chrome, and the tests.
- **Stage 3's changes are built with 8a;** 8b, the fallback, was dropped (see *Dropped: the ImDrawList fallback* under change 8). Of the stage's done criteria, what remains is outside its changes: the light theme and fonts (overhaul change 9), the status bar, and the shapes *Mark ImGui Shapes* still finds.

### Fix: windows in OS windows of their own (after 8a)

Reported after 8a: a floating window dragged out of the editor lost its glass and looked like ImGui's again, which broke the look.

- **The cause:** ImGui draws the background and title bar of a window that fills an OS window of its own fully opaque, whatever their colours' alpha (`bg_col |= IM_COL32_A_MASK`), since the OS window can't show what lies behind it. The chrome only makes those colours transparent, so ImGui's solid grey covered the card and the tab bars painted behind it; only what is painted in order with the widgets showed. It was so since change 3; the OS windows had never been checked by hand.
- **The background is taken back:** `ClearOsWindowBackground()` (`UIChrome.cpp`) sets the alpha of ImGui's background and title bar vertices in such a window to 0 when its card is painted. They are the first things in its draw list; the vertices are found by colour (the window, child, popup and title bar colours' RGB) from its start until one has another colour. The same goes for a host of windows docked together in an OS window of its own.
- **Glass there too:** an OS window is opaque to the OS (the GPU's surfaces offer only opaque composite alpha), so nothing of the desktop can show through it. Instead, glass there shows a plain backdrop: `SetPlainGlassBackdrop()`, a one-pixel UI image in the theme's backdrop colour, which the renderer makes the first time (`GetPlainGlassBackdrop()`). The card is the same glass as in the main window, tinted and lit along its edge, over an empty backdrop, with its hairline. Floating dock hosts' cards in OS windows are glass the same way.
- **Rounded corners:** Windows 11 rounds the OS windows ImGui makes, when asked (`DWMWA_WINDOW_CORNER_PREFERENCE`), by as much as the cards' rounding at 100%. `VulkanImGuiBackend::RoundOsWindowCorners()` asks once per OS window, after `UpdatePlatformWindows()`, and asks again, for the default, while the chrome is off. The card is painted rounded over the backdrop's colour, which fills the corners where Windows 10 doesn't round.
- **Left as it is:** no shadow around the OS window; Windows gives undecorated windows none, and the card has no room for one.
- **Tests** (`NyxUIChromeTests`, 1 new): with multiple viewports and stand-in platform functions, ImGui gives a window outside the main viewport a viewport of its own without making an OS window. Its draw list starts with transparent vertices; its card is glass over the plain backdrop, filling the OS window; its tab bar has its underline.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), on a screen capture, as the editor's own screenshot only covers its main window: the Painter Test window outside the editor showed (21, 21, 21), ImGui's window colour, without the tab bar's pill and underline; with the fix, a rounded glass card with the tab's pill and underline.
- **Not checked by hand yet:** menus and tooltips reaching out of the editor, windows docked together outside it, and switching the chrome off while one is out.
- **README:** in the sentence on glass.

### Fix: resizing windows in OS windows of their own

Reported after the fix above: a floating window outside the editor couldn't be resized from its edges. Not the painter: two bugs in the forks of the dependencies, both fixed in the submodules.

- **ImGui's side worked:** in `NyxUIChromeTests`' setup, with the mouse 2 px inside the right edge of a window in a viewport of its own, ImGui hovers its resize border and asks for the resize cursor; the chrome doesn't get in the way. Only the inner 4 px (`WindowBorderHoverPadding`) can be grabbed there, as the outer half lies outside the OS window.
- **The press never reached ImGui (GLFW fork):** the fork's custom titlebar (`GLFW_TITLEBAR` false: no default titlebar, resize borders kept) was read from the global window hints, `_glfw.hints.window.titlebar`, instead of per window. The editor sets the hint for its main window and it stays set, so every window GLFW made afterwards, ImGui's OS windows included, answered `WM_NCHITTEST` with `HTLEFT`, `HTRIGHT` and so on within 4 px of its edges. A press there was a non-client click, which GLFW doesn't pass on, in exactly ImGui's 4 px; Windows didn't resize either, as those windows have no sizing frame. Popups and menus in OS windows lost presses along their edges the same way.
- **The GLFW fix:** each window keeps its own `titlebar` (`_GLFWwindow`, from the hints when made; `glfwSetWindowAttrib()` and `glfwGetWindowAttrib()` use it). The Win32 window procedure handles the frame itself only for `hasCustomTitlebar()` windows: decorated and without the default titlebar. Undecorated windows have no frame to replace, whatever the hint says. The editor's main window behaves as before.
- **The cursor (ImGui fork):** ImGui's GLFW backend remembers the last cursor it set, once for all OS windows, and since ImGui 1.92.6 (Dec 2025) skips the call when the cursor is unchanged. It sets the main window's cursor first, so the others never got a changed cursor: no resize, text or hand cursor outside the editor. Fixed with upstream ImGui's fix (ocornut/imgui d7950e2c4, #9545, Sep 2026) in `backends/imgui_impl_glfw.cpp`: the cursor is worked out once per frame and, when it changes, set on every OS window; a new OS window resets it. Upstream's earlier amend (51f590a2a, restoring the cursor after `GLFW_CURSOR_DISABLED`) is left out. The next merge of upstream's docking branch brings the same change.
- **No test:** both need real OS windows; `NyxUIChromeTests` has none.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), without moving the mouse, with the Painter Test window outside the editor:
  - `WM_NCHITTEST` 2 px inside each window's right edge: without the GLFW fix, `HTRIGHT` for the main window and the three OS windows; with it, `HTRIGHT` for the main window and `HTCLIENT` for the OS windows. In the middle, `HTCLIENT` for all.
  - With the resize cursor asked for, `WM_SETCURSOR` sent to each window and `GetCursor()` read back: without the ImGui fix, the resize cursor in the main window and the arrow in the OS windows; with it, the resize cursor in all.
- **Not checked by hand yet:** dragging an edge and a corner of a window outside the editor, the text cursor in its fields, and resizing the editor's main window from its edges.

### 9a. The editor's own

- **New widgets** (`UIWidgets.h`), each in the UI gallery:
  - `Button()` takes an `EButtonTone`: `Neutral`, `Go` (Play, Play All) or `Stop`, in their own theme colours (`ButtonGo...`, `ButtonStop...`). Its behaviour is shared with the two below (`AddButton()`).
  - `MenuBarButton(label, size, bOpen)`: flat, with the menu items' hover pill, and the selection's pill (`RowSelected`) while `bOpen`, eased. The titlebar's File, Window and Debug pass `ImGui::IsPopupOpen()` of their menus.
  - `WindowControlButton(id, EWindowControl, size)`: minimise, maximise, restore and close, filled while hovered or pressed (close in `WindowCloseHovered`), with icons of painted lines on whole pixels, `WindowControlIconSize` wide and `WindowControlIconWidth` thick. They replace the titlebar's `InvisibleButton()`s and ImDrawList icons (`Icon::Util` in `WindowsWindow.cpp`, removed).
  - `InputTextMultiline(label, std::string*, size, flags)`: the fields' hybrid, sized as ImGui sizes a multi-line field. ImGui draws its text in a child window, after the painted frame. The crash cards' call stack and log lines and Game Link's message details use it.
  - `InputText()` takes an optional hint; `SliderFloat()` takes `ImGuiSliderFlags`. With `ImGuiSliderFlags_Logarithmic` over a range above 0, the knob sits by ratio, where ImGui's grab is.
- **The titlebar** (`WindowsWindow::DrawTitlebar()`): with the chrome, no fill of its own, so it lies on the backdrop; without it, ImGui's fill as before. The focus border is painted with the foreground painter while the chrome is on (`WindowFocusBorder`, `WindowFocusBorderWidth`, `WindowRounding`; none while maximised), and drawn by ImGui while it is off. Play's green and red come from the tones instead of pushed ImGui colours.
- **Panels:** the outliner's, Game Link's, the crash cards' and Play Setups' buttons are `Nyx::UI::Button()`s. Game Link's Pause and Resume keep one ID (`###Pause`), so the hover doesn't start over when the label changes. The crash cards' "Call stack" and "Last log lines" are collapsing headers instead of tree nodes. Play Setups' "Play All on this monitor" is a section header, and its preview is painted: the work area as a field, the windows as buttons, with ImGui's text over them. Play's options menu has a separator and a muted line instead of ImGui's separator text. The mockup overlay's path is a `std::string` in a painted field with a hint, and its offset a two-component vector field (clamped to ±8000 after an edit). The Painter Test window's zoom slider and 100% button are painted.
- **`ChildBg` is taken over by the chrome:** child windows, e.g. the Game Log's scrolling table, kept ImGui's opaque grey on the card. That was the "scrolling region's background" 8a found.
- **The marks** (`UIDiagnostics.cpp`): triangles whose corners are all fully transparent aren't shapes, so ImGui's background taken back in an OS window of its own isn't marked. Each viewport's background and foreground draw lists are looked at too, when used this frame, and count as a window each.
- **Tests:**
  - `NyxEditorPanelTests`, 2 new:
    - The titlebar's widgets: a Go button fills in its colour; a menu bar button is flat while closed and shows the selection's pill while open; a close button fills red while hovered and clicks; a multi-line field's frame is painted and a read-only one keeps its text; clicking a logarithmic slider's painted knob keeps the value, so knob and grab agree.
    - The panels leave no shapes to ImGui: outliner, details (with a component), a crash card with a call stack, Game Link and Play Setups, drawn headless with the chrome on and the marks on, give 0 shapes. ImGui's text colour is made transparent there, which hides 9b's close and collapse buttons; with it visible, the test finds 11 shapes, so it can fail.
  - `NyxPainterTests`: a fully transparent shape isn't marked; a shape in the foreground draw list is.
  - `NyxUIChromeTests`: `ChildBg` is transparent while the chrome is on.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored), on the editor's own screenshots of its main window:
  - Before, with the Painter Test window and Play Setups floating, the marks (logged with their windows and rects) found the titlebar's fill, its five buttons and three window controls, the outliner's buttons, the Painter Test toolbar's slider and button, Play Setups' buttons, and ImGui's buttons in title and tab bars. Game Link and a crash card were then opened too: after the change, the marks find only ImGui's buttons in title and tab bars, which 9b covers.
  - The titlebar: File's pill with its menu open, green Play and Play All, crisp window controls, the rounded focus border, on the backdrop. The panels' painted buttons, the crash card's headers and call stack field, and the Game Log's buttons and table, without the grey behind it.
  - The gallery's new rows in every state: Go and Stop buttons, menu bar buttons closed and open, the window controls with the close button red while hovered.
  - Windows placed in the main window by code kept OS windows of their own (their viewports from `imgui.ini`; ImGui merges them back only when moved by hand); `SetNextWindowViewport()` put them in the main window for the check.
- **Not checked by hand yet:** the window controls (minimise, maximise and restore, close), the menus opened by click, Play and Stop with a game running, Play Setups' preview (behind another window in the check), the mockup overlay's fields, and the main window's draw count now.
- **README:** the titlebar in the sentence on the cards, the new widgets in the gallery's list, and what the marks still find, with the new test.

### 10. What painting the whole editor costs

- **Statistics** (`PainterWindowStats`, `PainterTiles.h`):
  - per OS window, besides its segments and draws: commands, tiles in use and entries, binning time, the painter's GPU time there, and ImGui's own draw commands and vertices;
  - for the frame: the bytes uploaded for the segments (`VulkanPainterSegments::GetUploadBytes()`), and `FrameTimes`, where the UI's time went.
- **CPU times**, taken in `VulkanRenderer::DrawFrame()` with a steady clock:
  - building the UI (the editor's panels, ImGui's layout and the painting recorded in them, from `BeginFrame()` to `ImGui::EndFrame()`);
  - painting the chrome (`PaintWindowCards()`) and gathering the painters (`GatherWindowPainters()`);
  - `ImGui::Render()`;
  - the segments (`UploadWindowSegments()`: runs, appending, binning, upload).

  The painter's CPU time after the UI is chrome, gathering and segments (`GetPainterCpu()`). The marks and the Painter Test canvas aren't counted. The times before the statistics start for the frame wait in `PendingFrameTimes`.
- **GPU times**, from a timestamp query pool (`UITimestamps`) of pairs:
  - the main window's UI pass, around its render pass;
  - the glass backdrop;
  - each segment draw in any OS window (at most 254 a frame), added up per OS window.

  The pool is reset at the start of the frame's command buffer, before the OS windows' command buffers, which ImGui submits after it. Only the pairs written are read (`ReadUITimestamps()`), at the start of the next frame, once the frame's fence has passed. The UI built then shows the CPU and GPU times of the same frame. The painter's GPU time also counts its draws into the glass backdrop, so it can be more than the main window's UI pass.
- **The Painter Test window's *Statistics* tab:**
  - tables of the CPU and GPU times and of the draws and commands, each with the last frame, the average and the peak over the last 240 frames;
  - the frame's other counts (windows painting, segments, tiles, upload);
  - a table per OS window;
  - a painted graph of four times over the last frames: building the UI, the painter's CPU, the painter's GPU, the main window's UI pass. Its scale rounds up to 1, 2 or 5 times a power of 10.

  The samples are kept while the window is open, whichever tab shows, and start over when it opens again.
- **Tests:** `NyxPainterGpuTests`: the segments' upload holds at least both painters' commands and every tile entry, and nothing once cleared. The times themselves need the renderer's real frame; they were checked in the editor.
- **Measured** (temporary code averaging 600 frames after 300, Painter Test window closed; RTX 3070 Ti; `imgui.ini` restored):

  | | Debug, 1296x759 | Release, 1296x759 | Release, 3456x1399, a selection, Game Link as glass |
  | --- | --- | --- | --- |
  | Building the UI | 0.44 ms | 0.20 ms | 0.26 ms |
  | Painter on the CPU after the UI | 2.18 ms | 0.35 ms | 2.15 ms |
  | of which binning | 1.92 ms | 0.28 ms | 1.65 ms |
  | Chrome, gathering, `ImGui::Render()` | 0.04 ms | 0.01 ms | 0.01 ms |
  | Painter's draws, GPU | 0.53 ms | 0.32 ms | 3.03 ms (with the glass backdrop's) |
  | Main window's UI pass, GPU | 0.56 ms | 0.33 ms | 1.81 ms |
  | Glass backdrop, GPU | none | none | 1.34 ms |
  | Segments, draws | 12, 8 | 12, 8 | 20, 15 |
  | Commands | 204 | 204 | 345 |
  | Tiles in use, entries | 46,645, 136,279 | the same | 238,892, 691,865 |
  | Uploaded per frame | 932 KB | 932 KB | 4,684 KB |
  | ImGui's own draw commands, vertices | 8, 520 | 8, 520 | 18, 1,322 |
  | Frame | 5.1 ms | 1.3 ms | 6.5 ms |

  With the Statistics tab open in the busy Debug editor, the window's own painting brings it to 1,387 commands, 25 draws and 11.7 ms of painter CPU.
- **What the numbers say:**
  - Building the UI costs little; the painter's cost is binning and the GPU, and both grow with the area painted, not the commands: 204 commands make 136,279 tile entries.
  - Big shapes reach many tiles: the backdrop, each card with its shadow, and each segment's grid that a card covers. Each of the main window's 8 draws bins its own grid over most of the window; tiles in use (46,645) are three times the window's tiles (15,390).
  - The upload is mostly tiles and entries: 932 KB a frame at 1296x759, 4.7 MB at 3456x1399.
  - The main window is 8 draws, 15 when busy: ImGui's text between painted widgets still ends runs.
  - Glass doubles the painter's GPU time behind the glass windows: their backdrop is painted twice, into the backdrop image and onto the screen.
- **Ways to bring it down, for later** (not built):
  - A tile wholly inside an opaque shape needs nothing under it: entries before it there could go. The backdrop is opaque; the docked cards are slightly translucent (ambilight).
  - Coarser tiles for shapes that cover many, or a shape's inside as one span per row instead of an entry per tile.
  - Binning on the GPU (stage 2's "left for later"), which also ends most of the upload.
  - Fewer runs ended by text: painting the remaining ImGui text (labels, tab names) would let runs go on.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored): the tab in the busy Debug editor shows the tables, the table per OS window and the graph; no validation messages. The numbers above come from the same statistics, logged.
- **Not checked by hand yet:** the tab with windows outside the editor (a row per OS window), and on a GPU without timestamps.
- **README:** the Statistics tab in the sentence on the Painter Test window.

### 11. A heatmap of the whole editor

- **The heat** (`PainterHeatGrid`, `PainterTiles.h`): per 8x8 tile of an OS window's target, the commands of every draw covering it, summed, and how many draws do. `Reset()` sizes it for the target; `Add()` adds one draw's tile and ignores tiles outside.
- **Summed by the renderer** while the heatmap is on: in `UploadWindowSegments()`, each OS window's grid is reset to its target, and after each draw is added, `VulkanPainterSegments::AddHeat()` adds that draw's tiles where its grid lies on the target. Each OS window's statistics also carry its viewport's ID and pixels per ImGui unit, to place the heat.
- **Shown** by `Nyx::UI::ShowPainterHeatmap()` (`UIDiagnostics.h`), which the renderer calls after the marks:
  - It draws the last frame's heat, which the statistics still hold, over each OS window: rects in its foreground draw list, one per run of tiles with the same value along a row, at 55% opacity, in the canvas heatmap's colours. It uses ImGui's draw list, not the painter, so as not to add to the painting it measures; after the marks, so they don't mark it.
  - Each OS window gets a legend in its top right corner, below the titlebar, and the tile under the mouse shows its commands and draws beside it.
- **Two views** (`EPainterHeatmap`):
  - commands per tile: blue for a few, through green, to red at 16 or more (`PainterHeatmapCommandsRed`), as the canvas heatmap. A first try with red at 64 left the whole editor blue.
  - draws per tile: blue for one, to red at 8 or more (`PainterHeatmapDrawsRed`).

  *Debug > Painter Heatmap: Commands* and *...: Draws* switch it on (one at a time, again for off); so does the combo under the table per OS window in the Statistics tab.
- **The heat counts the screen's draws only:** the glass backdrop paints the same segments again behind glass windows, which the heat leaves out (the Statistics tab's GPU time has it).
- **Tests:**
  - `NyxPainterGpuTests`: the heat of the four segments holds all their tile entries and tiles in use, a tile under the first segment has its heat, and a tile only the second painter's segment covers has one draw.
  - `NyxPainterTests`: nothing is shown while the heatmap is off; while on, two tiles of the same heat side by side are one rect, a tile of another heat a rect of its own, and tiles without painting nothing.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored; no validation messages), at 1296x759 with Game Link as glass, on the editor's own screenshots of both views:
  - Every tile of the main window is painted (15,390): 9.4 commands and 3.3 draws on average, at most 16 and 6.
  - In the commands view, the editor is green throughout: about 9 commands lie on every tile, plain card areas included (the backdrop, the windows' glows, the card, its border and the shadows near it).
  - Buttons with shadows, the tab bars and the glass window's buttons are red.
  - In the draws view, 3 draws lie everywhere, up to 6 over the glass window's controls.
- **What it shows for the next change:** the per-tile reduction would go first at the 9 commands under every plain tile. Only what a fully opaque shape covers can go: the docked cards are 85% opaque, so that ambilight shows faintly through them, and what lies under them still shows. Dropping it there means making them opaque, or painting the glow through them another way.
- **Not checked by hand yet:** the tile's counts beside the mouse, and windows outside the editor.
- **README:** the heatmap after the marks.

### 12. Frame rates, and what the proposed changes would bring

- **The editor's frame** (`PainterWindowStats::Times::Frame`): from the start of `DrawFrame()` before to this one's, steady clock.
- **The Statistics tab's Frame section** has these lines:
  - the editor's frame, and the painter's CPU time after the UI plus its GPU time, each last, on average and at its peak;
  - the editor's frames per second, on average and at its slowest frame;
  - the painter alone, in frames per second and as a share of the frame.

  The editor runs one frame at a time: each frame waits for the last one's fence before its CPU work, so CPU and GPU times add up rather than overlap.
- **The per-tile reduction's estimate** (`VulkanPainterSegments::EstimateOcclusion()`, `PainterOcclusionEstimate`):
  - For each segment's tile, the entries are walked from the last back to the first command that covers the whole tile opaquely (`CoversTileOpaquely()`): a solid filled rect without outline, opaque (alpha byte 255) or of a colour asked for. The tile must lie inside its clip rect and inside the rect less its largest rounding, which is cautious at rounded corners.
  - The entries from that command on are kept. It's done twice: as the look is, and with the docked cards' colour (`theme.Card`, packed) counted as opaque.
  - The renderer makes it when `Nyx::UI::RequestPainterEstimates()` asked for it this frame, after the segments' time is taken: it walks every entry. The Statistics tab asks every tenth frame, and averages the times over the frames without it and the entries over those with it.
- **The model** (`PredictPainterScenarios()`, `PainterTestWindow.h`):
  - **The reduction:** the painter's GPU time scales with the entries kept, as if dropped and kept commands cost the same. Shadows, glows and image glows cost more, so this is cautious where those go. The upload drops by the entries dropped, and binning on the CPU stays, as it still walks every command's tiles.
  - **The reduction with opaque docked cards:** the same, with that estimate.
  - **Binning on the GPU:** the CPU's binning goes. The upload is the commands and groups only (`GetCommandUploadBytes()`). The GPU adds an assumed 0.2 ns per entry and 0.05 ms for its dispatches (`AssumedGpuBinning...`), for a GPU like the RTX 3070 Ti; an integrated one, several times as much.
  - **Both:** the reduction with opaque cards, on the GPU, which bins all entries before dropping any.
  - **The editor's frame** changes by what the painter's CPU and GPU times change.
- **The Statistics tab's prediction section:** the entries kept by each variant, and a table of the painter's CPU and GPU time, the upload, and the painter's and the editor's frames per second for now and each change, with the model and assumption under it.
- **Tests:**
  - `NyxPainterGpuTests`: an opaque backdrop under a translucent card rounded by 4 keeps all 100 entries; with the card's colour counted as opaque, the 16 tiles inside its rounding drop the backdrop (84 kept).
  - `NyxEditorPanelTests`: the model, with round numbers: 75% kept cuts the GPU time and frame by a quarter of the painter's GPU time, and the upload by the dropped entries; binning on the GPU moves the binning off the CPU and the tiles off the upload and adds its own GPU time; both add up.
- **Predicted, from the measured frames** (Release, Painter Test window closed, the estimate every tenth frame; temporary code, undone):

  1296x759, default layout: 1.57 ms a frame, 638 fps; 136,403 entries.

  | | Now | Per-tile reduction | Reduction, opaque docked cards | Binning on the GPU | Both |
  | --- | --- | --- | --- | --- | --- |
  | Entries kept | 136,403 | 136,403 (100%) | 94,848 (70%) | all, binned on the GPU | 94,848 |
  | Painter CPU | 0.39 ms | 0.39 ms | 0.39 ms | 0.09 ms | 0.09 ms |
  | Painter GPU | 0.32 ms | 0.32 ms | 0.22 ms | 0.40 ms | 0.30 ms |
  | Uploaded | 933 KB | 933 KB | 770 KB | 16 KB | 16 KB |
  | The painter alone | 1,407 fps | 1,407 fps | 1,629 fps | 2,082 fps | 2,608 fps |
  | The editor | 638 fps | 638 fps | 680 fps (+7%) | 748 fps (+17%) | 806 fps (+26%) |

  3456x1399, a selection, Game Link as glass: 7.49 ms a frame, 134 fps; 692,267 entries.

  | | Now | Per-tile reduction | Reduction, opaque docked cards | Binning on the GPU | Both |
  | --- | --- | --- | --- | --- | --- |
  | Entries kept | 692,267 | 692,250 (100%) | 440,223 (64%) | all, binned on the GPU | 440,223 |
  | Painter CPU | 2.17 ms | 2.17 ms | 2.17 ms | 0.55 ms | 0.55 ms |
  | Painter GPU | 3.08 ms | 3.08 ms | 1.96 ms | 3.27 ms | 2.15 ms |
  | Uploaded | 4,686 KB | 4,686 KB | 3,701 KB | 27 KB | 27 KB |
  | The painter alone | 190 fps | 190 fps | 242 fps | 262 fps | 370 fps |
  | The editor | 134 fps | 134 fps | 157 fps (+18%) | 165 fps (+23%) | 203 fps (+52%) |

- **What the predictions say:**
  - **The per-tile reduction brings nothing with the look as it is.** The only opaque shape that covers whole tiles is the backdrop, and it is first in every tile: everything after it is translucent (the docked cards at 85%, for ambilight), glass, or too small to cover a tile.
  - **With opaque docked cards** it keeps 64 to 70% of the entries: the backdrop and the windows' glows under each card go. That changes the look, though: ambilight would no longer show through the panels, unless it is painted on the cards another way.
  - **Binning on the GPU is the larger step:**
    - it takes 75 to 80% of the painter's CPU time away (the binning) and almost all of the upload;
    - it adds an assumed 0.1 to 0.2 ms of GPU time;
    - it doesn't depend on the look.
  - **Together** they come to +26% for the editor at 1296x759 and +52% at 3456x1399.
  - **What these numbers can't tell:** the GPU binning's cost is an assumption until it is built. The GPU savings are cautious where dropped commands are costly (glows under cards). Integrated GPUs would spend several times as long on the GPU.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored; no validation messages): the Statistics tab's Frame section and prediction table in the Debug editor, on the editor's own screenshots. With the Painter Test window open, its own painting is in what it measures (about 1,250 commands instead of about 200).
- **README:** the frame rates and the predictions in the sentence on the Statistics tab.

### 13. Binning on the GPU, with the per-tile reduction

- **The CPU measures** (`MeasurePainterSegment()`, `PainterTiles.h`), once per command, into a `PainterBinCommand` (80 bytes):
  - `Reach`: what the command can touch. Its shape, or its group's whole reach for a grouped command, grown by the anti-aliasing, cut by its clip rect and the target; empty when it touches nothing (culled).
  - `LineFrom`, `LineTo`, `LineReach`: a line on its own also has its band, half its width plus a pixel, so it only lands in the tiles along it.
  - `Hides`: the rect it covers opaquely. Only a solid rect without outline, of alpha 255, less its largest rounding, cut by its clip rect; empty for anything else.
  - `HidesIfAlsoOpaque`: the same, with the colours asked for counted as opaque too (`SetAlsoOpaqueColors()`; the renderer passes the docked cards' colour).

  It also fits the segment's grid, drops the commands past the budget's `MaxCommands` with a problem, and sums each command's tiles into `MaxEntries`: the entries before any reduction, an upper bound that sizes the entries buffer.
- **The GPU bins** (`Engine/Shaders/PainterBin.comp`, 64 invocations a workgroup, one per tile), over all segments' tiles one after the other:
  - **Pass 0** walks the tile's segment's commands in order and counts those that touch it, with the same rect and line band tests the CPU binner had. A command whose `Hides` contains the whole tile starts the count over: the per-tile reduction. The first kept command is kept for pass 1.
  - The workgroup adds up its 64 counts in shared memory and reserves their entries with one `atomicAdd`, so a tile's entries lie together. A tile whose entries would pass the buffer's end is drawn empty, and its segment is marked as overflowed.
  - Each workgroup sums, per segment, the entries before the reduction, those kept, those kept with `HidesIfAlsoOpaque`, the tiles in use and the most per tile, then adds them to the segment's sums once.
  - **Pass 1** writes each tile's kept commands' indices, from its first kept command.
  - Each segment's tiles start at a multiple of 64 (`PainterBinWorkgroupSize`), so a workgroup only ever bins one segment; a tile finds its segment by a binary search on the first tiles.
- **Buffers** (`VulkanPainterBuffers`), grown by doubling, never shrunk:
  - device-local: commands, groups, measured commands, segments, tiles, entries, first kept, sums;
  - host-visible: the four uploads, and the read-backs of the sums and, while asked for, of the tiles and entries.

  `Bin()` copies the uploads in, zeroes the sums, runs both passes and copies the read-backs out, with barriers between. The draws read the tiles and entries straight from the GPU's own buffers: the segment's first tile is its `TileBase`, and `EntryBase` is 0.
- **The entry budget:** the entries buffer holds `MaxEntries`, at most the budget's `MaxTileEntries`. Tiles past it are drawn empty and reported (`GetReadBackProblems()`).
- **Read back** at the start of the next frame, after its fence (`VulkanRenderer::ReadWindowSegments()`, `VulkanPainterSegments::ReadBack()`):
  - every frame: the sums, which fill the statistics' tiles in use, entries and the reduction (`PainterReduction`);
  - while the heatmap is on (`SetReadBackTiles()`): the tiles and entries too, for the heat.

  A canvas reads back at the start of its next draw; the renderer's `GetPainterCanvasTiles()` gives its last binning, for the canvas's statistics and tile heatmap. The tests bin, wait and read back.
- **Gone:** the CPU binner (`BinPainterCommands()`, `BinPainterSegment()`), change 12's estimate (`EstimateOcclusion()`, `RequestPainterEstimates()`) and predictions (`PredictPainterScenarios()`), and the upload of tiles and entries.
- **Statistics:**
  - the GPU's binning time, from its own timestamp pair (pairs: main window's UI 0, glass backdrop 1, binning 2, segment draws from 3, at most 253);
  - the CPU's measuring time, in place of binning;
  - the per-tile reduction section of the Statistics tab: the entries before, kept, and kept with opaque docked cards; and a table of now and with opaque docked cards (`PredictOpaqueDockedCards()`), where the painter's GPU time scales with the entries kept, as change 12's model did.
- **Tests:**
  - `NyxPainterTests`: the grid covers the target; a shape's reach is the shape with its anti-aliasing; commands off the target or clipped away are culled; the clip rect cuts the reach; a group reaches its whole group; opaque rects hide, less their rounding; the command budget drops the rest; measuring reuses its result.
  - `NyxPainterGpuTests`, binned on the GPU and read back: shapes land in their tiles, in drawing order; lines walk their tiles; groups cover their whole reach; what an opaque rect hides is dropped; the entry budget leaves tiles empty and reports it; segments keep their own sums. The pixel tests paint through the GPU's binning.
  - `NyxEditorPanelTests`: the opaque cards' prediction.
- **Measured** (temporary code averaging 600 frames after 300, Painter Test window closed; RTX 3070 Ti; `imgui.ini` restored), against changes 10 and 12:

  | | Debug, 1296x759 | Release, 1296x759 | Release, 3456x1399, a selection, Game Link as glass |
  | --- | --- | --- | --- |
  | Frame | 5.1 → 3.1 ms | 1.57 → 1.22 ms (638 → 820 fps) | 7.49 → 4.1 ms (134 → 245 fps) |
  | Painter on the CPU after the UI | 2.18 → 0.36 ms | 0.39 → 0.06 ms | 2.17 → 0.07 ms |
  | of which measuring | 0.11 ms | 0.02 ms | 0.03 ms |
  | Binning, GPU | 0.09 ms | 0.06 ms | 0.11 ms |
  | Painter's draws, GPU | 0.53 → 0.36 ms | 0.32 → 0.25 ms | 3.08 → 2.29 ms |
  | Uploaded per frame | 932 → 33 KB | 933 → 33 KB | 4,686 → 55 KB |
  | Entries before, kept, kept with opaque cards | 136,403, all, 94,848 | the same | about 692,000, all, about 440,000 |

- **What the numbers say:**
  - **Binning on the GPU beat change 12's prediction:** 820 fps where 748 were predicted, about 245 where 165 were. The binning costs 0.06 to 0.11 ms on the GPU, less than the assumed 0.1 to 0.2 ms. The painter's draws got faster too: they read the tiles, entries and commands from device-local memory instead of host-visible.
  - **The reduction keeps everything with the look as it is**, as predicted. With opaque docked cards it would keep the 94,848 and about 440,000 that change 12 estimated, so making the cards opaque now brings that drop straight away.
- **Checked in the editor** (temporary code, undone; `imgui.ini` restored): the busy layout on the editor's own screenshot paints as before; no validation messages, also with GPU-assisted and synchronization validation.
- **A GPU fault at startup that predates this change:**
  - Busy runs of the optimized editor sometimes logged NVIDIA driver errors (event 153) in the first frames or at the resize. A few times the editor then ended with `vk::DeviceLostError` from `waitForFences()`, uncaught, which aborts (`0xC0000409`). This was about 5 of some 100 runs, in Release and RelWithDebInfo; Debug runs showed none.
  - The committed code before this change logs the same errors, in 3 of 60 runs; once it froze instead of exiting.
  - Each error falls in the same second as a scene view recreating its render target and picking resources (`VulkanRenderer::DrawFrame()`, `bResizePending`), which happens several times while the dock layout settles and at the resize.
  - Ruled out for the painter, with temporary code: the binning's passes agree on every tile; full barriers don't change it; a guard against oversized tiles doesn't either.
  - Left for its own change.
- **README:** the GPU sorting shapes into tiles, the Statistics tab's reduction numbers, and what the painter tests check.

### Fix: vsync and a frame rate cap (after 13)

- **Why:** the editor drew frames as fast as it could: the swapchain preferred mailbox, and nothing paced the loop. With frames of 1 to 4 ms, that is several hundred a second, most of which the monitor never shows. In Task Manager, the GPU looked busy the whole time.
- **Vsync** (`IWindow::SetVSync()`, which used to call `glfwSwapInterval()` and did nothing for Vulkan):
  - The window passes it to the renderer (`IRenderer::SetVSync()`), which sets the swapchain's present mode: FIFO with vsync, mailbox (or FIFO where there is none) without.
  - A change recreates the swapchain after the frame; set before `Initialize()`, it is used from the first swapchain on.
  - On by default, as `WindowsWindow` always meant it to be: the game gets it too.
  - ImGui's OS windows keep their own mailbox swapchains: they don't block, so the main window sets the pace.
- **The frame rate cap** (`FrameRateLimiter`, Core):
  - `Application::Run()` calls `IWindow::WaitForNextFrame()` before polling events, so a frame waits before it reads input, not after.
  - The next frame is due one interval after the last one was, so a frame that starts a little late keeps the pace. A loop behind by more than an interval starts at once instead of rushing to catch up (`NextFrameDue()`).
  - On Windows it waits on a high resolution waitable timer: a plain sleep wakes up as much as 15 ms late.
  - It applies with or without vsync, e.g. 60 on a 165 Hz monitor.
- **Preferences** (`EditorPreferences`, `Editor.ini`: `VSync=`, `FrameRateCap=`): *Window > Preferences > Frame rate* has a *Vsync* checkbox and a *Frame rate cap* of none, 30, 60, 90, 120, 144 or 240 fps (`FrameRateCaps`). A saved cap not in that list is skipped. `EditorLayer::ApplyFrameRate()` passes them to the window on attach and every frame; the window acts only on a change.
- **The Statistics tab:**
  - While vsync or a cap is on, a line in the theme's new `TextInfo` blue under the frame rates says what holds the editor's frame rate back. The same line sits under the opaque cards' prediction, whose editor fps the same limit holds.
  - The editor's frame includes the wait; the painter's frame rate and the CPU and GPU times are the work alone. A GPU that waits often clocks down, though, and takes longer for that work: at 60 fps, binning took 0.44 ms where it took about 0.06 ms uncapped.
- **Tests:**
  - `NyxFrameRateLimiterTests`: a frame on time or a little late keeps the pace; a loop behind starts at once; without a cap nothing waits; five frames at 100 fps take at least 40 ms on the real clock.
  - `NyxPlayWallTests`: the frame rate settings survive a round trip; without them, vsync is on and there is no cap; caps not offered, unreadable caps and vsync values are skipped.
- **Measured** (Release, 1296x759, 165 Hz monitor; temporary code, undone; `imgui.ini` and `Editor.ini` restored). The GPU column is the busiest GPU engine of the editor's process (NVIDIA's `Graphics_1`), as Task Manager shows it, over 4 seconds. Another editor was running at the same time, which held the uncapped rate down.

  | | No vsync, no cap (before) | Vsync | Cap 120 | Cap 60 | Vsync and cap 60 |
  | --- | --- | --- | --- | --- | --- |
  | Frame rate | 319 fps | 164 fps | 120.0 fps | 60.0 fps | 60.0 fps |
  | GPU busy | 27% | 13% | 13% | 5% | 7% |

- **Checked in the editor:** the Preferences section and the info line, on the editor's own screenshots. One run hit the GPU fault at startup described under change 13, which predates this.
- **README:** the Frame rate section of the preferences, and the info line in the Statistics tab.

### Fix: the whole frame in the statistics, and the scene views' editor passes on demand (after 13)

- **Why:** the Statistics tab timed the UI and the painter, about half of the frame. The rest (waits, the scene views, presenting) only showed in the total. And every scene view drew its picking pass and two selection mask passes every frame, though picking is only needed for a click, and the masks only change with the selection, the camera or the scene.
- **The frame, step by step** (`PainterWindowStats::Times`):
  - New CPU times, in frame order:
    - outside the renderer (events, the layers' updates, the frame rate cap);
    - waiting for the last frame's fence;
    - acquiring the next image;
    - the world and the scene views (extraction to the blurs);
    - the painter canvases with the marks and heatmap (timed before, but not kept);
    - recording and submitting the UI after the segments (glass backdrop, UI pass, OS windows);
    - presenting.
  - The editor's frame is now timed from the last frame's `DrawFrame()` end to this one's, as its steps are, so they add up to it. Before, it ran from start to start and was set beside the next frame's steps; "the rest of the frame" then jumped by the vsync wait (peaks of 5.5 ms; now 0.3).
  - New GPU timestamp pairs: the scene views' editor passes (3), the scene views (4) and the frame's whole command buffer (5); segment draws start at 6, at most 250.
  - To time the editor passes in one go, the scene view loop is split. All views' resizes, uniforms and editor passes come first, then all views' own render passes. Each view's uniforms and images are its own, so the order within a view is unchanged.
- **The Statistics tab:**
  - The CPU table lists the steps in order, then "the rest of the frame" (the frame less every line; at least 0) and the painter's share.
  - The GPU table lists the frame's command buffer, then picking and selection masks, the scene views, the blurs, binning, the glass backdrop, the UI pass and the painter's draws.
  - A note under each table says how to read it.
- **With vsync, the wait for the monitor shows as waiting for the last frame's GPU,** not in acquiring or presenting (0.005 and 0.03 ms). The acquire returns at once, the GPU then waits for the image the monitor frees, and the fence passes when it is done.
- **Picking only for a click** (`DrawFrameOnDevice()`): a view draws its picking pass only while a pick request is pending, in the frame after the click, as before; the selection lands two frames after the click either way. `RenderedWorldRevision`, which refuses clicks until the view shows the current world, is set every frame as before.
- **The selection masks only when they change** (`GetSelectionMaskFingerprint()`, `SceneViewInstance::SelectionMaskFingerprint`):
  - An FNV-1a hash of what the mask passes draw from: the selected entity, the world revision, the view's view-projection and size, and every drawn object's entity, transform, vertex and index buffers and index count. All objects count, as the visible mask holds the depth of those in front of the selected one.
  - The masks are drawn when it differs from the last drawn one. With nothing selected there is none: the masks aren't drawn, and the outline isn't either (as before).
  - New mask images reset it (`CreateSelectionMaskResourcesForView()`), so they are drawn before the outline reads them. Between draws, the images keep their content: stored, and left in a shader-readable layout.
  - Not covered: a mesh whose buffers are rewritten in place keeps its outline until something else changes. Meshes are loaded once today.
- **Checked in the editor** (temporary code, undone; `imgui.ini` and `Editor.ini` restored; no validation messages in Debug):
  - Scripted: the selection was cleared, a click picked the middle cube ("Reflective Cube", two frames later), the cube was moved by 1 along x, and it was deselected. On the editor's own screenshots, the outline is on the picked cube, follows it to its new place, partly behind the cube beside it, and is gone after deselecting.
  - The editor passes' GPU time, Release, default layout: 0.008 ms in the frame of the pick (picking), 0.045 ms when the selection changed and 0.053 ms when the cube moved (the masks), and 0.001 ms or less in every other frame. Before, every frame paid about 0.06 ms for them in this three-cube scene; they draw every object up to three times a view, so it grows with the scene.
  - The Statistics tab with vsync and the Painter Test window open (Release, 1296x1360): frame 6.06 ms (165 fps), of which 5.09 waiting for the last frame's GPU. On the GPU: 2.02 ms for the command buffer, of which 0.08 the scene views, 0.05 the blurs, 0.61 binning, 0.64 the glass backdrop and 0.64 the UI pass; the painter's 1.01 ms of draws lie within the last two. The GPU is clocked down while it waits.
- **What the breakdown shows:** in the editor's own scene, the painter is most of the GPU frame; the scene views are a few percent of it.
- **README:** the frame's steps in the sentence on the Statistics tab, and the scene views' passes on demand.
