# Nyx features

What the editor and the game do. How the code behind them is organised is in
[Architecture.md](Architecture.md).

- Editor with scene outliner, details panel, asset browser and two viewports
- Picking, transform gizmo, undo/redo
- Vulkan renderer
- Reflection system (see below)
- Animated startup banner

## Play

- **Play** in the toolbar runs the open scene in `NyxGame.exe`, as a separate program, with
  unsaved changes included.
- While the game runs, the button is **Stop**: it asks the game to quit, and ends it if it
  hasn't after 3 seconds. Clicking it again while the game is stopping ends it at once.
- Opening or creating a scene stops the game.
- **Pause** (F6), next to Play All, stops the game's simulation while the game runs: no fixed
  steps, while its frames, its camera and live edits go on. The button stays lit while paused;
  click it again to resume. A game that starts or restarts while paused starts paused, and Stop
  ends the pause.
- **Step** (F10, repeating while held) runs exactly one fixed step while paused.
- To debug the game, either:
  - right-click Play, turn on *Game Waits for Debugger*, and attach Visual Studio to
    `NyxGame.exe`; or
  - install *Microsoft Child Process Debugging Power Tool 2022+* and turn on child process
    debugging under *Debug > Other Debug Targets > Child Process Debugging Settings*: Visual
    Studio then attaches to the game by itself.

## Play All

- **Play All**, next to Play, starts several games at once, side by side on the monitor the
  editor is on: one per *play setup* marked for it.
- A play setup is a named picture size, e.g. *Steam Deck* at 1280x800. *Window > Play Setups*
  edits them and previews where Play All puts the windows; Play's right-click menu marks which
  ones Play All starts. Play itself uses the first setup.
- If the windows don't fit, all of them shrink by the same factor, and their titles say so.
- Each game has its own editor link and gets every edit. **Stop** asks all of them to quit; a
  game that crashes or is closed doesn't affect the others. Pause and Step act on all of them.
- The games of Play All have no console windows unless *Console Windows in Play All* is on.
  Their log reaches the Game Link window either way.
- Play setups are saved with the editor preferences in `%LOCALAPPDATA%\Nyx\Editor.ini`.
- Tests: `NyxPlayWallTests` (the setups and the tiling), `NyxGameInstanceTests` (several games
  as real processes, copies of the test program standing in for them; Pause and Step through
  Play All).

## The editor link and live edits

- A game started by the editor connects back to it over the *editor link*: TCP on `127.0.0.1`,
  on a port the system picks, so only programs on this machine can connect. Both programs log
  when the link connects and ends; hovering **Stop** shows its state.
- While the game runs, edits show up in it live: moving an entity, changing a property in the
  details panel, adding or deleting an entity, and undo and redo of those.
- Values being dragged, with the gizmo or in the details panel, and text being typed show up in
  the game every frame; the end of the drag is one undo step.
- Edits made while the game is still starting are sent once it is linked; edits after Stop
  aren't sent.
- Tests: `NyxEditorLinkTests` (both ends of the link in one program), `NyxLiveEditTests` and
  `NyxGameLinkSubscriberTests`.

## The Game Link window

- *Window > Game Link* shows the games' log lines (*Game Log* tab) and every message of their
  links in both directions (*Messages* tab), each with its game's name.
- Select a message to see all its fields; pause, filter by type and copy.
- *Record* writes each game's messages of each play session to a `.nyxlinklog` file in a
  `LinkLogs` folder next to the editor. `NyxGame.exe --link-log <file>` records the game's side.

## Crash cards

- A game that crashes sends its call stack to the editor before it ends. The editor's *Game
  Crashed* window shows a card for it: what happened, the call stack with files and lines, the
  game's last log lines, and **Restart** (the same game again, with the scene as it is now),
  **Copy** and **Dismiss**.
- A game that crashes before it is linked still gets a card, with its exit code.
- In Debug builds, Ctrl+Alt+Shift+C in a game window crashes it on purpose, to try this.
- `NyxGameInstanceTests` crashes a stand-in game for real and checks the report.

## The game on its own

```
NyxGame.exe [scene file] [--window-size <width>x<height>] [--window-pos <x>,<y>] [--window-title <text>]
```

- Without a scene file it runs `Scenes\Default.nyxscene` from the project's assets, or else from
  the engine's (`Assets\Scenes\Default.nyxscene` in Nyx's own build).
- The size is that of the picture, without titlebar and borders; the position is where the
  window's outer top-left corner goes, and may be negative on a monitor left of or above the
  main one.
- `NyxGameLaunchOptionsTests` tests the command line.

## NyxDump

Prints what a scene file or a link recording contains, e.g. from the repository folder:

```
Build\Windows\Binaries\Debug\NyxDump.exe Assets\Scenes\Default.nyxscene
```

A `.nyxlinklog` file prints the same way.

## Physics

Rigid bodies on [Jolt](https://github.com/jrouwe/JoltPhysics), while a game runs:

- Add a collider (*Box Collider*, *Sphere Collider* or *Capsule Collider*) to an entity, and it is
  something others bump into. Add a *Rigid Body* too, and it falls, slides and tumbles; marked
  kinematic, it moves only where its transform is set, pushing what it meets. A *Physics
  Material* sets friction and bounce.
- The entity's transform places the body and scales its shape.
- While the game runs, moving an entity in the editor puts its body there and stops it; changing a
  collider or rigid body makes the body again.
- The editor doesn't simulate: Play shows physics.

## Debug drawing

Code can draw lines over a world to show what it does: forces, anchors, bounds, paths.

- A game's systems draw through their step's `SimContext::Debug`: lines, arrows, boxes, spheres,
  axes and crosses (`DebugDraw`). What a step draws stays until the next step; a shape given
  seconds stays that long in the world's time, so pausing the game keeps it.
- The game's view draws them over the scene, depth tested, in the colours given. A view can hide
  them (`IRenderer::SetSceneViewShowDebugDrawing()`).
- Release builds compile debug drawing out: drawing does nothing there.
- The editor's overlays draw each object's axes and bounds the same way.

## Profiling

Every program has the [Tracy](https://github.com/wolfpld/tracy) profiler built in, while the CMake
option `NYX_PROFILER` is on (the default). It collects nothing until a viewer connects, so it
costs next to nothing otherwise.

- Run the Tracy viewer of the same version as `Dependencies/tracy` (`tracy-profiler.exe`). Its
  start screen lists the programs running on this machine; open one to record it.
- Each program listens on the first free port from 8086, only on `127.0.0.1`: the editor usually
  gets 8086, and the games of Play or Play All the next ones. To pick one by hand, type
  `127.0.0.1:<port>` into the viewer's address field.
- Each frame is one frame in the viewer. Zones show the input, each layer's update, the fixed
  steps with each step, and the renderer's steps: the fence wait, the scene views, the UI build
  and the submit.
- `-DNYX_PROFILER=OFF` builds without Tracy: the profiler macros compile to nothing.

## The painted UI

The editor's UI is drawn by the *painter*: 2D shapes as signed distance fields on the GPU, so
edges stay crisp at any size. The GPU sorts the shapes into the 8x8 tiles each pixel checks,
leaving out what an opaque shape hides.

- **Window chrome**: windows are rounded cards with soft shadows on a backdrop, with a gap
  between docked cards; hovering the gap shows the splitter. Tab bars have a pill and an accent
  underline that slides to the tab selected; scrollbars are thin pills that widen while hovered.
- **Glass**: floating windows, menus and tooltips in the main window show what lies behind
  them, blurred. Dragged into OS windows of their own, they keep the glass's tint and rim over a
  plain backdrop.
- **Ambilight**: each scene view's picture glows onto the backdrop around it, and faintly
  through the panels beside it. *Window > Preferences* turns it off or sets its strength and
  reach.
- **The window's frame**: a titlebar with the logo, the open scene's tab, the workspaces and
  window controls; a toolbar with the menus, the command search, Play, Play All, Pause, Step and
  Build; a status bar with the last scene saved or opened, the errors logged, the graphics API
  and the frame rate. What Nyx doesn't do yet (projects, some workspaces and menus, the command
  search and Build) is there but disabled, with a tooltip that says so. Empty parts
  of the titlebar and toolbar drag the window.
- **Painted widgets** (`UIWidgets.h`): buttons, tabs, toggles, sliders, tree rows, fields,
  checkboxes, menus, combos, colour swatches, asset cards and tables, built on ImGui's own
  behaviour.
- **Frame rate**: *Window > Preferences* holds the editor's frame rate back, so it doesn't keep
  the GPU busy drawing frames the monitor never shows: *Vsync* (on by default) and a *Frame rate
  cap* of 30 to 240 fps, with or without vsync.
- **Scene views** draw their picking pass only for a click, and their selection masks only
  while something is selected and what they show changed.

Debug tools:

- *Debug > Painted Chrome* switches back to ImGui's own look.
- *Debug > Mark ImGui Shapes* outlines every shape ImGui still draws itself, to find what isn't
  painted yet. `NyxEditorPanelTests` checks that the editor's panels leave none.
- *Debug > Painter Heatmap* colours each 8x8 tile of every OS window by the painter's work there:
  the commands its pixels walk, or how many draws cover it.
- In Debug builds (`NYX_DEV_TOOLS`), *Debug > Painter Test* shows the painter's test sheet (every
  shape it can draw, zoomable, with a tile heatmap and a comparison with ImDrawList), the sheet
  painted inside a window, the widget gallery in every state (*Save PNG* writes it to
  `Saved/Gallery`), the scene views' blur, and what painting costs on the CPU and the GPU.
- In Debug builds, *Debug > Mockup Overlay* lays an image from disk half transparent over the
  editor, to match a design.

Tests: `NyxUIChromeTests` (cards, splitters, tab bars and scrollbars, without a GPU),
`NyxPainterTests` (recording, measuring for the tiles, segments and runs inside ImGui windows),
`NyxPainterGpuTests` (binning and painting on the GPU, checked by pixel; with the environment
variable `NYX_PAINTER_TEST_OUTPUT` naming a folder, it saves what it painted as PNG files).

## Themes

- *Window > Theme* edits every colour and size of the painted UI live, grouped and searchable:
  colours with a picker, a hex code (`#RRGGBB` or `#RRGGBBAA`) and their alpha, sizes with a
  drag.
- Themes are text files of `Name = value` lines in `Editor/Themes`: `Default` (the default),
  `Dark`, `Ice`, `Light`, `Night` and `SoftLight`.
- The window's list loads any of them, *Open...* one from anywhere; *Save* writes the one loaded,
  *Save As...* a new one. Loading another while there are unsaved changes asks first.
- The theme loaded is remembered in `Editor.ini` and loaded at startup, and read again whenever
  its file changes on disk, so it can be edited in a text editor too.
- `NyxUIThemeTests` checks that every theme value is in the file and reads back as written.

## Reflection

Types and properties are exposed to the editor, saving and live edits with macros:

```cpp
NYX_REFLECT(Component, meta = (DisplayName = "Transform Component"))
struct TransformComponent
{
    NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DragSpeed = 0.1))
    glm::vec3 Position{ 0.0f };
    // ...
};

#include "Generated/Runtime/TransformComponent.reflect.h"
```

During the build, `NyxHeaderTool` reads every header in `Engine/Source/Runtime/Scene/include` and
writes the generated files to `Build\Windows\Generated\Runtime`. Its tests compare its output
against golden files: `Tools\NyxHeaderTool\tests\RunHeaderToolTests.bat`.

The generated files belong to a *module*, Runtime for the engine's types. `--module-name <Name>`
names another one: its registration functions become `Register<Name>ReflectedTypes()` and
`Register<Name>ReflectedSources()`, and its init header `Generated/<Name>/<Name>.reflect.init.h`.
A name must be a C++ identifier. In the header tool's tests, a fixture names its module in a
`Module.txt`; `RegenerateGoldenFiles.bat` reads it too.
