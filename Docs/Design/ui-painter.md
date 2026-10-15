# UI painter

How the editor's 2D drawing works, and why it works that way. The code is in
`Engine/Source/Runtime/Renderer` (`Painter*`, `WindowPainter`, `UIWidgets`, `UIChrome`,
`VulkanPainter`, `VulkanBlur`, `VulkanGlass`) and `Engine/Shaders` (`Painter.frag`,
`PainterBin.comp`).

## Why a painter

ImGui draws everything as triangles from its font atlas. Rounded corners are polygons, soft
shadows don't exist, and nothing stays crisp when scaled. The painter keeps every shape a shape
until the pixel shader, which draws it as a signed distance field:

- edges are anti-aliased exactly at any size or zoom (one pixel of fade, centred on the edge);
- shadows and glows are a distance away from the shape, so they cost almost nothing;
- ImGui still does the layout, the input and most text: the painter only replaces how things
  look.

The idea follows Wessel Mast, "UIPainter: Tile-Based UI Rendering in One Draw Call" (Guerrilla,
GDC 2024).

## Recording

`Painter` records one frame of draw commands for one target. It is plain C++ without Vulkan, so
it is tested without a GPU.

- **Commands** (`PainterCommands.h`) are what the shader reads: 80 bytes each, a header (type,
  colour, group, clip rect, outline, texture) and 32 bytes of payload. The types are few: rect
  (rounded or not), circle, circle sector (arcs too), line, triangle, glyph, image, image glow
  and glass. The file starts with the checklist for adding one.
- **Shadows and glows belong to groups**, never to a single command: a shape with effects of its
  own becomes a group of one. The shader then handles effects in one place, a group of shapes
  casts one shadow, and commands keep their size.
- **Colours** are premultiplied RGBA8 on the GPU, in the same sRGB-encoded space as ImGui's, so
  painted and ImGui-drawn colours match side by side.
- **Textures** are ImGui texture references, resolved only when the frame is drawn, since ImGui
  may still update its font atlas until then. With ImGui's Vulkan backend a texture *is* a
  descriptor set, so the painter binds ImGui's sets as they are: at most 7 per draw
  (`PainterMaxTextures`), which with the commands' set is the 8 every desktop GPU binds.
- **It never logs or asserts.** Misuse is collected in `GetProblems()`, values given the wrong
  way round are made safe, and the frame is drawn as well as possible.
- **Budgets** (`PainterBudget`) cap the commands and tile entries per frame, so nothing allocates
  without bound. What doesn't fit is dropped from the end, so what is drawn is always the start
  of the frame, never a frame with holes.

## Tiles and binning

The target is cut into 8x8 tiles. A GPU runs pixel shaders in waves of 32 or 64 pixels,
scheduled in 8x8 blocks, so all pixels of a wave walk the same list of commands.

- **The CPU measures** each command once (`MeasurePainterSegment()`): what it can touch (its
  shape, or its group's whole reach, grown by the anti-aliasing and cut by its clip rect), a
  line's band, and the rect it covers opaquely, if any.
- **The GPU bins** (`PainterBin.comp`), one invocation per tile: it walks the commands in order
  and keeps those that touch the tile. A command that covers the whole tile opaquely starts the
  list over, since nothing drawn before it shows there (the per-tile reduction). A workgroup
  reserves its tiles' entries with one atomic add, so a tile's entries lie together.
- **The shader** (`Painter.frag`) draws one quad over the tiles; each pixel walks its tile's
  commands and blends them in, in registers, and writes once, premultiplied. Empty tiles
  discard, so what lies beneath stays.

## Painting inside ImGui windows

A painted shape has to land in order with ImGui's own drawing: in front of the widgets drawn
before it, behind those drawn after. `WindowPainter` does this:

- Inside a window, `Nyx::UI::GetWindowPainter()` gives the window's painter. It records in
  ImGui's screen coordinates, with ImGui's current clip rect, so painting is clipped like ImGui's
  drawing.
- Its shapes are cut into **segments** wherever ImGui has drawn in between. Each segment starts
  with a callback in the window's draw list; ImGui renders a draw list in order, so the callback,
  where the renderer draws the segment, lands exactly where the shapes were recorded.
- **Runs:** segments that follow each other with nothing ImGui draws between them are copied
  into one painter (`Painter::AppendSegment()`) and drawn as one. A run stops before the first
  glass window, whose backdrop is drawn from what comes before it.
- All of a frame's segments share one set of buffers and are binned together, once per frame.

The renderer waits for the previous frame before recording the next, with one fence that covers
the frame's own command buffer and ImGui's OS windows (an empty submission after them). So every
painter target has one set of buffers, rewritten each frame.

## The painted UI

- **Widgets** (`UIWidgets.h`) are built on ImGui's behaviour functions, so IDs, keyboard
  navigation and the last-item queries work as for ImGui's. Each widget works out a look
  (hovered, pressed, focused, eased over frames) and paints it, so every state can be painted
  without the mouse. Fields, menu items and selectables are ImGui's own, drawn invisible, with
  their frame painted, so they keep ImGui's text editing and popup behaviour.
- **The window chrome** (`UIChrome.h`) paints every window as a card on a backdrop, and the tab
  bars and scrollbars. ImGui keeps the layout, docking and input; the chrome makes the colours it
  replaces transparent.
- **Glass:** floating windows in the main window show what lies behind them, blurred. The
  renderer draws ImGui's draw data up to the first glass window into an image of its own
  (`VulkanGlass`), blurs it, and the glass samples it. Windows in OS windows of their own can't
  see behind them, so their glass shows a plain backdrop.
- **Ambilight:** a scene view's picture glows onto the backdrop around it (an image glow), and
  docked cards are slightly translucent so it shows faintly through them.
- **The blur** (`VulkanBlur`) halves the picture a few times, then blurs the small result with a
  Gaussian along x and y: a wide blur stays cheap.
- **Themes:** every colour, size and effect comes from `Nyx::UI::GetTheme()`, never from a
  literal in a widget.

## Finding problems

- *Debug > Painter Heatmap* colours each tile by the commands its pixels walk, or the draws
  covering it.
- *Debug > Mark ImGui Shapes* outlines what ImGui still draws itself.
- In Debug builds, *Debug > Painter Test* shows the test sheet, the widget gallery and what
  painting costs per frame.
- `NyxPainterTests` (recording, measuring, segments and runs), `NyxPainterGpuTests` (binning and
  pixels on a GPU), `NyxUIChromeTests` and `NyxEditorPanelTests`.

## Not done

- ImGui still draws text between painted widgets, and the close, collapse and window menu buttons
  of title and tab bars. Each piece of it ends a run, so a window takes several draws.
- Fonts and icons are ImGui's; icons painted from shapes stand in for an icon font.
