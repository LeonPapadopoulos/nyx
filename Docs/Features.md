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
- The pads drive only the game whose window has the focus, unless *Gamepads Drive All Games* is
  on in Play's right-click menu (`NyxGame.exe --input-background`); switching it reaches running
  games at once.
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

A `.nyxlinklog` file prints the same way, and so does an input asset or a player's input settings
(`.nyxinput`).

## Physics

Rigid bodies on [Jolt](https://github.com/jrouwe/JoltPhysics), while a game runs:

- Add a collider (*Box Collider*, *Sphere Collider* or *Capsule Collider*) to an entity, and it is
  something others bump into. Add a *Rigid Body* too, and it falls, slides and tumbles; marked
  kinematic, it moves only where its transform is set, pushing what it meets. A *Physics
  Material* sets friction and bounce.
- The entity's transform places the body and scales its shape.
- While the game runs, moving an entity in the editor puts its body there and stops it; changing a
  collider or rigid body makes the body again.
- Moving bodies are drawn between their last two steps, so they move smoothly on monitors faster
  than the 60 steps a second; paused, where the last step left them.
- The editor doesn't simulate: Play shows physics.

## Input

Keyboard, mouse and gamepads (Xbox, DualShock 4, DualSense, through SDL3) go through the engine's
input system: code reads actions such as *Move* or *Look*, bound to keys, buttons and sticks in
contexts, and the context highest on the stack that binds a control gets it.

- Scene views fly with W/A/S/D, the mouse and a pad; so does the game's primary camera.
- *Window > Input* shows the editor's input:
  - *Devices*: each device with its controls live, sticks with their dead zones and a plot of
    where they have been, calibration that suggests dead zones, and buttons that try rumble,
    trigger effects and the light;
  - *Actions*: every action with its value and phase, and for the one selected each binding's
    trace, where it stopped and why;
  - *Contexts*: the contexts on the stack, and which one decides the cursor;
  - *Control map*: which action each control gets, now or with other contexts (*What if*), and
    every binding of the next control pressed (*Press to find*);
  - *Events*: the events, and what was sent to the pads;
  - *Timeline*: chosen actions over the last seconds, with each frame's traces.
- In Debug builds, Ctrl+Alt+Shift+I shows the same over a game.
- The bindings and their tuning are data, in `Input\Default.nyxinput` in the project's assets, or
  else the engine's (`Assets\Input\Default.nyxinput` in Nyx's own build). The editor writes it
  from the built-in defaults when there is none. A pad model's profile can tune bindings while
  that model drives them, and the game also reads the player's own tuning from
  `%LOCALAPPDATA%\Nyx\PlayerInput.nyxinput`. Bindings that can't be used are logged with where
  they are.
- The Input window's *Tuning* tab edits the asset with undo, felt at once in the scene views. For
  each binding: what the checks find, with their fixes; what its values mean in units (an inner
  dead zone of 0.15 "ignores the first 15 % of the stick's travel") and which layer each comes
  from; its response curve with the stick on it; its trigger as a timeline with the last press.
  *Add Action* asks what the action is for and writes a recipe's bindings.
- The *Checks* tab lists mistakes that are easy to make and hard to feel (two actions on one
  control, mouse movement times the frame's time, a hold shorter than the tap beside it, ...) and
  which devices can do each action.
- Running games, all of Play All included, get every change of the asset as it is made, with their
  player's settings over it; held buttons stay held.
- `NyxPadCheck.exe` lists the pads, prints what they report and tries their output without a
  window.
- `NyxInputTests`, `NyxInputRouterTests`, `NyxInputGamepadTests`, `NyxInputInspectionTests`,
  `NyxInputAssetTests`, `NyxInputChecksTests`, `NyxLiveInputTests`, `NyxEditorCameraTests` and
  `NyxEditorInputTuningTests` test it with virtual devices.

## Debug drawing

Code can draw lines over a world to show what it does: forces, anchors, bounds, paths.

- A game's systems draw through their step's `SimContext::Debug`: lines, arrows, boxes, spheres,
  axes and crosses (`DebugDraw`). What a step draws stays until the next step; a shape given
  seconds stays that long in the world's time, so pausing the game keeps it.
- The game's view draws them over the scene, depth tested, in the colours given. A view can hide
  them (`IRenderer::SetSceneViewShowDebugDrawing()`).
- Release builds compile debug drawing out: drawing does nothing there.
- The editor's overlays draw each object's axes and bounds the same way.
- **The physics debug view**: F3 in a game window (Debug builds) shows every body's shape, coloured
  by how it moves (static grey, kinematic blue, awake green, asleep dark green, sensors yellow), the
  last step's contact points with their normals in red, and constraints as pink lines between
  their anchors. It changes with the next step, so while paused, with Step. A cyan cross and arrow
  show where the cursor points at a body, and the surface's normal there ([Cursor rays](#cursor-rays)).

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

Properties can be numbers, vectors, quaternions, strings, reflected structs, enum classes
declared with `NYX_ENUM()`, and `std::vector`s of any of these; enums are saved as their
enumerators' numbers. The details panel edits an array's elements, and adds, removes and moves
them (right-click an element), with undo.

During the build, `NyxHeaderTool` reads every header in `Engine/Source/Runtime/Scene/include` and
writes the generated files to `Build\Windows\Generated\Runtime`. Its tests compare its output
against golden files: `Tools\NyxHeaderTool\tests\RunHeaderToolTests.bat`.

The generated files belong to a *module*, Runtime for the engine's types. `--module-name <Name>`
names another one: its registration functions become `Register<Name>ReflectedTypes()` and
`Register<Name>ReflectedSources()`, and its init header `Generated/<Name>/<Name>.reflect.init.h`.
A name must be a C++ identifier. In the header tool's tests, a fixture names its module in a
`Module.txt`; `RegenerateGoldenFiles.bat` reads it too.

`--type-root <dir>` names another module's headers, such as the engine's: a type of this module may
hold one of their types as a property, and nothing is generated for them. A game module's run gets
the engine's, so a game's component can hold an `AssetReference` or a `SurfaceAnchor`.

## Surface anchors

A `SurfaceAnchor` property hangs something on another entity's surface, such as a mark drawn on a
crate: a point, the surface's normal and a tangent, kept in the host's local space and naming the
host by guid. It saves, undoes and goes over the editor link like any other property, and the
details panel shows its host (in hex) and its three vectors. Without a host it is in world space.

- `ResolveSurfaceAnchor()` gives where it is in the world now (`SurfaceFrame`: position, unit
  normal and tangent, bitangent, a matrix), following the host as it moves, turns and is scaled;
  nothing when the host is gone.
- `MakeSurfaceAnchor()` makes one from a frame in the world, such as where a ray hit.
- `ResolveSurfaceAnchor(host, anchor)` resolves it on a host transform given, such as the host as
  the renderer draws it between two steps.
- Many anchors in a frame: `MakeEntityGuidLookup()` once, then resolve through it, instead of
  each one going through every guid of the world. A game's systems have one made for them each
  step (`SimContext::Guids`).
- Tests: `NyxSurfaceAnchorTests`; `NyxGameModuleTests` saves and loads one in a module component.

## Cursor rays

What lies under a point of a view, for clicking on the world.

- `ScreenPointToRay()` turns a point of a camera's picture, in pixels from its top left corner as
  ImGui gives the mouse over an image, into a ray through the world from the near plane. It goes
  back through the same view and projection the renderer draws scene views with
  (`CameraProjection.h`), so the ray passes through what is drawn there.
- `PickPhysicsBody()` follows a ray to the first body and gives its entity, the point, the
  surface's normal and the distance; nothing when the ray meets nothing within the distance given,
  such as the camera's far plane. Its `QueryFilter` can leave out one body, such as the player's
  own, and counts sensors only when asked.
- `MakeSurfaceAnchor(world, pick)` hangs a surface anchor on the entity where the ray hit, such as
  for a mark placed by a click. Its tangent is world X along the surface, or world Z on a surface
  facing along X (`GetSurfaceTangent()`), so it doesn't depend on where the camera was.
- In a game window, the physics debug view (F3, Debug builds) shows where the cursor points at a
  body. It follows the cursor with each step, so not while paused.
- Tests: `NyxCursorRayTests` (a point drawn on a pixel lies on that pixel's ray, for cameras moved
  and turned, at the centre and the corners; picking a box drawn there, the floor behind it when
  the box is left out, sensors only when asked, nothing past everything; an anchor where the ray
  hit following the box), `NyxSurfaceAnchorTests` (the tangent).

## Surface decals

A *Surface Decal* draws a material flat on another entity's surface and moves with it, such as a
glowing mark on a crate.

- Its *Anchor* is a surface anchor: the host, by guid, and a point, normal and tangent on it.
  Without a host, the decal is in world space. *Size* is its width along the tangent and its height
  across it, in metres: the host's scale moves the anchor, but doesn't stretch the decal.
- It is drawn with its *Material*, such as `Materials/MaskedDecal.nyxmat`, and gives the material its
  *Color* times its *Intensity* (above 1, it glows), its *Reveal* (0 to 1: how much of it is drawn)
  and its *Pulse* (0 to 1: how strongly it throbs). At Reveal 0, or with *Visible* off, it isn't
  drawn; nor is a decal whose host is gone, until the host is back (an undo, say).
- It is a quad on its anchor's frame, lifted 3 mm off the surface so the two don't fight for depth,
  so on a curved surface it lies on the surface's tangent plane. It is drawn after the scene's
  objects and sky, hidden by what is in front of it, and blended: a material can cover what is
  beneath (its opacity), glow on it (its emission), or both.
- On a moving body, which the game draws between its last two steps, a decal is drawn where the
  body's mesh is drawn, not a step ahead of it.
- Any surface material can be a decal's. The *masked decal material* (`Shaders/MaskedDecal.slang`)
  draws a mask, white on black, in the decal's colour, with smooth edges and an optional glow
  around it; Reveal uncovers it as a wipe along a direction, out from the middle or round it, with
  a brighter edge where it moves, and Pulse dims it and back, *Pulse Rate* times a second. Its
  sample mask, `Textures/DecalRing.png`, a ring, is made by `Scripts/MakeDecalMask.py`.
- The editor's scene views show decals as the game does. Saving, undo and live edits work as for
  any component, and a decal's shader and material reload when saved.
- Tests: `NyxSurfaceDecalTests` (the quad following its host, also between two steps, what is
  drawn, saving and loading), `NyxSceneGpuTests` (a decal on a cube brightens it where it is, partly
  when partly revealed, and not at all unrevealed, without its host, or behind another cube; on a
  cube drawn between two steps, it is on the cube).
