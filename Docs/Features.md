# Nyx features

What the editor and the game do. How the code behind them is organised is in
[Architecture.md](Architecture.md).

- Editor with scene outliner, details panel, asset browser and any number of scene views
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

## A game's player

- A game module can have a player (`PLAYER` in `nyx_add_game_module()`): it adds its own input
  actions and contexts, and each frame turns them, with the ray through the cursor, into commands
  for the simulation. The next step's systems apply them; nothing else changes the game's world.
- A command is a reflected struct of the game's whose Serialize properties are all that arrives:
  entities by guid, never by handle.
- The player can draw its own UI over the game's picture, with ImGui, each frame
  (`IPlayerController::DrawUI()`): a hotbar, or a ghost of what a click would place at a point of
  the world (`PlayerView::ToScreen()`). It shows in every build, unlike the debug drawing.
- `NyxGameModuleTests` tests it with the test module's player: a key press brightens a lantern
  in the next step, once, and the cursor's ray reaches the player.
- The engine has one command of its own, `MoveCharacterCommand`, which walks a character; a
  game's player makes it from the Walk and Jump actions (`MakeMoveCharacterCommand()`). Without a
  game module's player, Nyx's game uses its own, which does just that (`ThirdPersonPlayer`).

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

A `.nyxlinklog` file prints the same way, and so do an input asset or a player's input settings
(`.nyxinput`) and an input recording (`.nyxinputrec`).

## Physics

Rigid bodies on [Jolt](https://github.com/jrouwe/JoltPhysics), while a game runs:

- Add a collider (*Box Collider*, *Sphere Collider* or *Capsule Collider*) to an entity, and it is
  something others bump into. Add a *Rigid Body* too, and it falls, slides and tumbles; marked
  kinematic, it moves only where its transform is set, pushing what it meets. A *Physics
  Material* sets friction and bounce.
- The entity's transform places the body and scales its shape.
- A *Mesh Collider* makes a static body of the triangles of the entity's mesh made at run time
  ([Meshes made at run time](#meshes-made-at-run-time)), scaled as the entity: a generated ground
  that crates land and slide on, characters walk on (its slopes and steps as on boxes) and rays hit.
  It is always static, also with a *Rigid Body* (which is warned of once). Its body is made again
  when the mesh's revision or data, or the entity's scale, changes, and only then: that costs as
  much as making it (a 128 m ground's 32 768 triangles, about 0.3 s in a Debug build). So while the
  mesh keeps changing, e.g. as a value of its ground is dragged in the editor during Play, the old
  body stays, and the new one is made once the mesh has held still for a quarter of a second, or
  after a second at most. A ray from behind a triangle passes through it, as from under a ground. Its
  *Smooth Edge Degrees* says where two triangles meet flat enough that what slides over the edge
  doesn't catch on it.
- While the game runs, moving an entity in the editor puts its body there and stops it; changing a
  collider or rigid body makes the body again.
- Moving bodies are drawn between their last two steps, so they move smoothly on monitors faster
  than the 60 steps a second; paused, where the last step left them. Kinematic bodies too, when a
  game's system moves them before the physics step.
- A *Character* walks an upright capsule, standing on the entity's position: it falls, climbs
  steps up to its step height, slides down ground steeper than its max slope, follows the ground
  down and pushes light crates aside. Its walk speed, acceleration, air control, jump speed,
  gravity and ground snap are on the component. A game says where it wants to go and when to jump;
  rays see it unless told to leave it out. Moving it in the editor while the game runs puts it
  there; changing its shape makes it again.
- `Assets/Scenes/CharacterTest.nyxscene` has a character among ramps, steps and crates.
- `Assets/Scenes/GroundTest.nyxscene` has the character, crates and an orbit camera in a valley
  ([The ground](#the-ground)).
- The editor doesn't simulate: Play shows physics.

## Input

Keyboard, mouse and gamepads (Xbox, DualShock 4, DualSense, through SDL3) go through the engine's
input system: code reads actions such as *Move* or *Look*, bound to keys, buttons and sticks in
contexts, and the context highest on the stack that binds a control gets it.

- Scene views fly with W/A/S/D, the mouse and a pad. In the game, so does a camera with a *Fly
  Camera Component*, which sets its speed and look sensitivity. Nyx's scenes and new scenes have
  one on their camera; a scene without one has no fly camera, and the game's own camera gets the
  input.
- An *Orbit Camera* circles its target entity (an entity picker in Details), looking at a point
  above it (*Target Offset*), at a distance it zooms between *Min* and *Max Distance* in *Zoom
  Steps*, its pitch kept between *Min* and *Max Pitch*. It follows the target as drawn between
  steps, smoothed by *Follow Smoothing*, and starts from where the scene placed the camera. Walls
  don't stop it yet.
- In the game, the right stick turns an orbit camera, the wheel zooms it, and the mouse turns it
  while the right button is held, leaving the cursor free to point with otherwise. A game's player
  can ask for free look instead, where the mouse turns it without a button and the cursor is
  captured. Its bindings are in `Assets/Input/Default.nyxinput`, tuned in *Window > Input* like the
  fly camera's. An orbit camera wins over a fly camera in the same scene.
- In a scene with a *Character* and an *Orbit Camera* (`Assets/Scenes/CharacterTest.nyxscene`),
  without a game module's player: W/A/S/D or the left stick walk the character relative to where
  the camera looks, Space or the south button jump, the mouse looks around freely, and Tab frees
  the cursor (then the right button held looks) and back. It goes through the same commands a
  game's player sends, and the bindings are in `Assets/Input/Default.nyxinput`.
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
- The dropdown at its top shows a running game's input instead, any game of Play All: while it is
  shown, the game streams its input frames, and the editor replays them through the same actions
  and contexts, so traces, the stack and the timeline are the game's.
- *Record* beside it writes the input shown to a `.nyxinputrec` file in an `InputRecordings`
  folder next to the editor; `NyxGame.exe --input-record <file>` records a game from its start,
  e.g. for a bug report.
- *Replay...* opens a recording as the source: it plays at the recorded pace through the recorded
  actions and contexts, and can be paused, stepped a frame at a time and moved to any frame, with
  each frame's traces in the Timeline. `NyxGame.exe --input-replay <file>` plays a recording into
  the game itself instead of its devices, with the game's own actions and contexts, then goes on
  with the devices.
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
- *Keep as A* remembers the tuning; edits after it are B, and *Showing A/B* swaps them here and in
  every running game, as does holding a pad's Back (View, Share, Create) and pressing its top face
  button while playing. A recording replayed *Through A and B* draws both in the Timeline: the
  same hands, two tunings.
- `NyxPadCheck.exe` lists the pads, prints what they report and tries their output without a
  window.
- `NyxInputTests`, `NyxInputRouterTests`, `NyxInputGamepadTests`, `NyxInputInspectionTests`,
  `NyxInputAssetTests`, `NyxInputChecksTests`, `NyxLiveInputTests`, `NyxInputStreamTests`,
  `NyxInputRecordingTests`, `NyxFlyCameraTests`, `NyxEditorCameraTests` and
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
  by how it moves (static grey, kinematic blue, awake green, asleep dark green, sensors yellow),
  a mesh's triangles' edges (a mesh of more than 8192 triangles as its bounds, so it doesn't crowd
  out everything else), characters' capsules in orange, the
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

## The editor's layout

- The editor starts as it was closed: the window's size and place, maximized or not, the dock
  layout, and the floating panels' sizes and places. A window last on a monitor that is gone
  comes back on one there is; ImGui does the same for panels floating in OS windows of their own.
- Each workspace's layout is kept in the project, in `Saved/Layouts` (`Scene.ini`,
  `Materials.ini`): saved a few seconds after it changes and when the editor closes, so stopping
  the editor in the debugger loses at most the last few seconds. The working directory doesn't
  matter; there is no `imgui.ini`.
- *Window > Reset Layout* puts the current workspace's panels back where they first were, and
  opens them.
- **Scene views**: *Window > New Scene View* opens another, "Scene 2", "Scene 3" and so on, docked
  beside the first and looking from where it looks; each has its own camera after. Any can be
  closed but the last. *Window > Game View in ...* shows a view the way the game sees the scene,
  through its primary camera and without the editor's overlays. The views open, each one's camera
  and whether it shows the game are kept with the layout. *Debug > Culling Bounds*, *Freeze
  Culling* and *Shadow Cascades* reach every view, those opened later too.
- `NyxEditorLayoutTests` saves and loads layouts headless: the docking, the window's placement,
  saving while running, switching workspaces, Reset Layout and keeping the window on the monitors.
  `NyxEditorSceneViewGpuTests` opens and closes scene views on a GPU: their cameras, culling and
  debug views, and their section of the layout read back.

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

## Entity references

An `EntityGuid` property names another entity, such as a surface anchor's host. The details panel
shows the entity's name, *None* for no guid, or *Missing (0x...)* for a guid no entity of the scene
has; the guid itself is in the tooltip. It is saved and sent to games as the guid, like any
`UInt64`.

- The combo lists the scene's entities by name, with a filter (a name or a guid), and *None*.
- An entity dragged from the outliner can be dropped on the field.
- The eyedropper button picks the next entity clicked in a scene view, even under the gizmo; Esc,
  another click on it, or selecting another entity stops it.
- The target button selects the entity named.
- Each change is one undo step.
- The outliner's right-click menu has *Copy Guid*, as saved scenes, logs and NyxDump show it.
- The header tool gives every `EntityGuid` property `UI = EntityRef`; one that names its own UI is
  drawn as that instead, such as the entity's own guid in the *Guid Component* (`UI = Hex`).
- Tests: `NyxEditorPanelTests` (choosing from the combo and its undo, a drop from the outliner,
  *None*, a missing guid, the eyedropper, saving and loading), `NyxHeaderToolTests`.

## Surface anchors

A `SurfaceAnchor` property hangs something on another entity's surface, such as a mark drawn on a
crate: a point, the surface's normal and a tangent, kept in the host's local space and naming the
host by guid. It saves, undoes and goes over the editor link like any other property, and the
details panel shows its host by name (see Entity references) and its three vectors. Without a host
it is in world space.

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
- `WorldToScreenPoint()` goes the other way: where a point of the world shows in the picture, or
  nowhere behind the camera. A game's player draws its UI at world points with it
  (`PlayerView::ToScreen()`).
- `RaycastSurfaceDecals()` finds the nearest decal whose quad a ray passes through from the front,
  and where on it (its UV), in front of the surface it lies on: what the cursor points at when it
  is over a mark.
- In a game window, the physics debug view (F3, Debug builds) shows where the cursor points at a
  body. It follows the cursor with each step, so not while paused.
- Tests: `NyxCursorRayTests` (a point drawn on a pixel lies on that pixel's ray, for cameras moved
  and turned, at the centre and the corners; picking a box drawn there, the floor behind it when
  the box is left out, sensors only when asked, nothing past everything; an anchor where the ray
  hit following the box; a world point back on its pixel, none behind the camera),
  `NyxSurfaceAnchorTests` (the tangent), `NyxSurfaceDecalTests` (the nearest decal under a ray,
  where on it, not from behind, not past the distance, not hidden).

## Meshes made at run time

A game's code can give an entity a mesh it makes itself, such as a generated ground or a tree whose
mesh is made again as it grows.

- The mesh is plain data (`MeshData`): vertices (position, normal, UV, colour, tangent), indices,
  three a triangle, and *sections*, runs of the indices each drawn with the material of its slot, so
  a tree's trunk and leaves are one mesh with two materials. A mesh without sections is one section
  in slot 0. `CheckMeshData()` says what is wrong with one, `ComputeMeshBounds()` where it is.
- The entity gets a `RuntimeMeshComponent` holding the data, shared, and a revision. With its
  *Mesh Renderer*, it is drawn in place of the renderer's *Mesh*: slot 0 with *Material*, slots 1 and
  on with *Extra Materials*; a slot without a material draws with *Material*.
- The renderer puts each mesh on the GPU once, when it is first drawn, and again only when the
  component's revision goes up: change the data, or give new data, then raise it
  (`SetRuntimeMesh()`, `MarkRuntimeMeshChanged()`). Entities sharing one `MeshData` share its upload.
  A mesh no view draws any more is let go of. Data that isn't a valid mesh isn't drawn, and is
  logged once a revision.
- It is drawn, picked, outlined when selected, shows its bounds, is drawn between steps when it
  moves, and carries surface decals, as a mesh loaded by its path does.
- It isn't saved, edited or sent to a game: whoever made it makes it again after a scene is loaded.
  Content made from a component's values, such as the ground, is made in the component's post-load
  step, so it shows in the editor and the game alike and follows every edit
  ([Architecture: Generated content](Architecture.md#generated-content)).
- The editor's details panel shows it read-only under *Mesh Made at Run Time*: its vertices and
  triangles, each section's triangles and material slot, whether it is drawn, and its revision.
- A *Mesh Collider* makes it ground to stand on ([Physics](#physics)).
- Tests: `NyxMeshTests` (which meshes are valid, the sections a mesh without any is drawn in, the
  bounds), `NyxRuntimeMeshGpuTests` (a sphere made in code draws exactly as the built-in sphere;
  changed and its revision raised it is drawn anew, unchanged it is uploaded once; entities sharing
  a mesh share its upload, and one nobody draws is let go of; one that isn't valid draws nothing; a
  click picks it, selected it is outlined; two sections draw with two materials; a decal on it is
  drawn), `NyxEditorPanelTests` (the details panel's lines).

## Faces and two-sided materials

Only the front of a triangle is drawn, the side its normal faces: half of a closed mesh's
triangles cost nothing.

- Every mesh Nyx makes winds counter-clockwise seen from its front, as glTF's do: the built-in cube,
  sphere, capsule and plane, a ground, a scatter's copies.
- An entity mirrored by a negative scale still shows its outside, and is picked and outlined there.
- A **two-sided** material draws both sides, for single sheets such as leaves, blades of grass and
  petals: tick *Two-Sided* under *Material* in the material editor, or write `TwoSided = true` in
  the `.nyxmat` file. An instance takes it from its parent unless it sets its own. The back is lit
  as a front: its normal is turned round, so a light on that side lights it.
  `Materials/VertexColorTwoSided.nyxmat` draws vertex colours two-sided.
- A click on a one-sided back picks what is behind it; on a two-sided one, the entity.
- Tests: `NyxMeshTests` (every built-in mesh, a ground and the glTF files wind with their
  normals), `NyxMaterialTests` (the option read, inherited, named when wrong, written as a line),
  `NyxSceneGpuTests` (a one-sided quad's back neither drawn nor picked; a two-sided one's drawn,
  picked, and lit from behind as its front from before; a mirrored cube drawn as the cube, also
  beside an unmirrored one; the reference pictures unchanged).

## Shadows

The primary *Directional Light* casts shadows, in the editor's scene views and in the game alike:
cascaded shadow maps, near shadows sharp and far ones coarser, steady as the camera moves and turns.

- Everything with a *Mesh Renderer* casts, unless its *Cast Shadows* is off, and every lit surface
  receives, decals too. Instanced meshes cast, each cascade drawing only the cells near it. A
  two-sided material casts from both sides.
- The light's *Shadows* settings: *Cast Shadows*; *Cascades* (1 to 4); *Shadow Distance* (metres
  from the camera; shadows fade out over its last tenth); *Split Blend* (how the cascades share the
  distance: 0 evenly, 1 each a few times the one before, so near ones are small and sharp);
  *Depth Bias* and *Normal Bias* (in texels: more takes acne away, too much lifts shadows off their
  casters); *Softness* (how many texels on each side are filtered together, 0 to 3); *Resolution*
  (texels across each cascade, 256 to 4096). All with undo, saved in scenes and sent to a running
  game.
- The editor's free camera shows the scene's sun and its shadows, as the game's camera does.
- **Seeing it**: *Debug > Shadow Cascades* in the editor (F5 in a Debug build of the game) tints the
  picture by the cascade each surface's shadow comes from (red, green, blue, yellow, nearest first)
  and shows each cascade's shadow map along the bottom left. The profiler has a zone per cascade.
- Four cascades of 2048 texels cost the instancing benchmark's field of 50 000 cubes, on a ground,
  about 1 ms of GPU time and 0.1 ms of the renderer's CPU time a frame in Release ([Shadows](Design/shadows.md#numbers)).
- Tests: `NyxShadowCascadeTests` (the splits; the spheres round the view's slices hold them, no
  bigger, the same however the camera turns; the light's view of a sphere holds it, reversed, back
  to the casters; a still point keeps its place in its texel as the camera walks and turns),
  `NyxShadowGpuTests` (a box and an instanced field shadowing a plane, against a reference image;
  no shadow with the light's or the box's casting off; a one-sided quad's back casts nothing, a
  two-sided one's does; each cascade culled by its own frustum; two cascades; the editor's camera;
  the debug view), the scene and material reference pictures unchanged.

## Instanced meshes and culling

Many copies of one mesh, such as grass, flowers or stones scattered over a ground, are drawn as one
entity, and every scene view draws only what its camera can see.

- An entity with a *Mesh Renderer* (or a mesh made at run time) and a `MeshInstancesComponent` is
  drawn at every transform of its instances, each relative to the entity's transform, with the
  entity's materials, section by section. The transforms may be in *cells*, each with the bounds of
  its instances' origins and a run of the transforms; without cells, all of them are one.
- Like a mesh made at run time, the instances are plain shared data with a revision
  (`SetMeshInstances()`, or raise `Revision` after changing them): uploaded once, again only for a
  new revision; entities sharing them share the upload; instances nobody draws are let go of. They
  aren't saved, edited or sent to a game: whoever made them makes them again in a post-load step.
- A click on any instance picks the entity; selected, all of its instances are outlined; its bounds
  are around all of them. The details panel shows *Mesh Instances* read-only: how many, in how many
  cells, the revision.
- **Culling**: every scene view, the editor's and the game's alike, leaves out what is outside its
  camera's frustum: an entity by its bounds where it is drawn, an instanced one cell by cell (a
  cell's bounds hold the mesh at each of its instances, even when the cell's own bounds are too
  small). Cells in view that follow on in the transforms are one draw.
- **Batching**: entities drawn once that draw alike, the same mesh with the same material in each
  section (many trees on one cached mesh, say), are drawn in one draw.
- **Seeing it**: *Debug > Culling Bounds* in the editor (F4 in a Debug build of the game) draws the
  bounds each view culls by, green drawn and red culled; *Freeze Culling* (F4 again) keeps culling
  by where the camera was, its frustum in yellow, while the camera moves on and shows what was left
  out. `IRenderer::SetSceneViewCullingDebug()`.
- 50 000 small cubes in 200 cells and 300 spheres take 0.15 ms of the renderer's CPU time in Release
  instead of 24.6 ms as 50 300 entities, and 1.4 ms a frame instead of 30 ([Instancing](Design/instancing.md#numbers)).
- Tests: `NyxCullingTests` (the frustum against boxes inside, outside and across it; boxes taken
  into other spaces; cells with the mesh added at each instance, cut short, grown; the runs of
  cells inside, outside and straddling), `NyxInstancingGpuTests` (a row of turned and squashed
  spheres drawn instanced as five entities draw them, in one draw; uploaded again only for a new
  revision or new data, shared, let go of; no transforms draw nothing; a click on an instance picks
  its entity and it is outlined; entities and cells out of view culled, the frozen view, the bounds
  drawn), `NyxEditorPanelTests` (the details panel's lines). `NyxInstancingBenchmark` measures the
  numbers.

## The ground

A *Ground* is a landscape made from a few values, a placeholder until the game's terrain is
decided: by default a valley about 128 by 128 m, nearly flat in the middle, its sides rising.

- Add a *Ground*, a *Mesh Renderer* with `Materials/VertexColor.nyxmat` (its *Mesh* left empty) and
  a *Mesh Collider*. The ground's mesh is made at once, in the editor and in the game, and again
  whenever a value changes: in the details panel when an edit ends (a drag when it is let go),
  with undo and redo, and in a running game with each live edit. It is never saved.
- *Size* and *Spacing*: the area around the entity and the mesh's grid, at most 512 squares a side.
  *Seed*: another ground of the same kind. *Noise*: layers (*Octaves*) of smooth hills, the first
  *Frequency* hills a metre, up to *Amplitude* metres high, each layer *Lacunarity* times finer and
  *Gain* times fainter. *Shape*: a *Basin* (a valley along Z, a bowl, or none) rising *Basin
  Depth* metres at the edges from a floor *Basin Floor* of the way out; a nearly flat place
  *Flat Radius* metres round the middle, the hills coming back over *Flat Blend*; terraces of
  *Terrace Height* metres, from gentle slopes (*Terrace Sharpness* 0) to steps (1).
- *Colours*: *Grass*, *Earth* and *Stone* (linear RGB) as vertex colours: grass on gentle low ground,
  thinning to earth from *Grass Thins From* to *Grass Gone At* metres up, earth past *Earth Slope*
  degrees, stone past *Stone Slope*, blended over *Slope Blend*. UVs are in metres.
- `Materials/VertexColor.nyxmat` (`Shaders/VertexColor.slang`) draws a mesh in its vertex colours,
  lit as any surface; every surface material can read them (`SurfaceInput.Color`).
- The height is a function of the values alone, the same on every machine
  (`SampleGroundHeight()`), and apart from the mesh: what stands on the ground uses the mesh and
  rays, so the ground can become chunks or voxels later.
- Making the default ground takes about 21 ms in a Debug build.
- `Assets/Scenes/GroundTest.nyxscene` (written by `NyxGroundTests --write-scene`): the default
  ground, crates on the flat place and up the valley's side, the character and an orbit camera.
- Tests: `NyxGroundTests` (the same heights every time, to the bit and as when written; smooth;
  the mesh valid, sized as asked, at the heights, its normals up; steep ground stone, low flat
  ground grass; the post-load step makes the mesh, nothing again for the same values, a new
  revision for new ones, the collider follows, removing takes the mesh away, a loaded scene has
  it; a character stands on it and a crate slides down the valley's side; the sample scene),
  `NyxEditorPanelTests` (added, edited, undone, removed and undone again in the editor).

## Scattering

A *Scatter* puts many copies of its entity's mesh over another entity's surface, such as plants
over a ground ([Design: Scattering](Design/scattering.md)).

- Give an entity a *Mesh Renderer* (or a mesh made at run time) and a *Scatter*, and pick the
  *Surface*: an entity with a mesh made at run time, such as a *Ground*. Without one, the copies are
  on the flat plane through the entity, so a scatter just added shows them at once. The copies are
  made in the editor and the game, again whenever a value changes, and again whenever the surface's
  mesh changes, either entity moves or a blocker changes, whichever was loaded first. They are never
  saved.
- *Size*: the area around the entity. *Density*: copies a square metre of surface. *Seed*: other
  places for the same values. *Spacing*: the least distance between two copies; it wins over the
  density. *Limits*: the *Slope Range* in degrees and the *Height Range* along the entity's Y.
  *Placement*: *Scale Range*, a random turn about the up of up to *Yaw Range* degrees, *Align To
  Normal* from upright (0) to along the surface's normal (1), and *Sink Depth* into the surface.
  *Masks*: *Use Blockers* (see below), and a *Noise Mask* that thins the copies in patches, for
  drifts with soft edges or, with *Noise Invert*, clearings: *Noise Frequency* (patches a metre),
  *Noise Threshold* (0.5 leaves out about half), *Noise Softness* (how softly they thin at a patch's
  edge), *Noise Seed*. *Culling*: the copies are made and grouped in squares of *Cell Size*, which
  the renderer culls whole.
- A *Scatter Blocker* keeps every scatter's copies out of its entity, e.g. grass out of a crate or a
  tree's trunk: its box, sphere or capsule collider (*Shape* *Collider*), or a circle of *Radius*
  round it (*Circle*, or a collider blocker without a collider), scaled with the entity; no copy
  within *Margin* metres of it, and fewer over the *Falloff* past that. A scatter with *Use
  Blockers* off grows through them.
- An edit makes only the squares it reaches again: a blocker added, removed, edited or moved by more
  than a centimetre, or a surface changed in one place. The rest keep their copies, and the result
  is exactly what scattering all of it gives.
- The surface's triangles are sampled by their area, square by square, so any mesh works; the same
  values, surface and blockers give the same copies on every machine.
- The copies are a `MeshInstancesComponent` (transforms relative to the entity, in cells with
  bounds), which the renderer draws; any code can fill one.
- Scattering about 50,000 copies on the default 128 m ground takes about 210 ms in a Debug
  build, 20 ms in Release. Dragging a blocker over 40,000 copies takes about 9 ms a frame in
  Debug (it was about 150 ms, when every change scattered everything).
- Tests: `NyxScatterTests` (the same transforms every time, to the bit and as when written; the
  density's count; the spacing; the slope and height limits; copies on the ground, moved and
  turned, scaled, turned and sunk as asked; the cells cover every copy once, with their bounds; the
  post-load step makes them, nothing again for nothing new, again for an edited scatter, an edited
  or moved ground; a surface loaded after its scatter is found; removing takes them away; no copy
  within a blocker and its margin, for each shape; the falloff's density; blockers moved, added,
  edited and removed, and a surface edited in one place, make only their cells again, the same as a
  full scatter; the noise mask the same every time, about half at 0.5, inverted the complement; a
  scatter that opts out of blockers; blockers and masks saved and loaded; timed, a drag too).

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
- The renderer finds a decal's material by its *Material* path, once per path and world, so a
  decal that a system adds while the game runs, with only its path, is drawn, and one whose path
  a system changes is drawn with the new material. A path that names no material is logged once,
  and the decal isn't drawn.
- It is a quad on its anchor's frame, lifted 3 mm off the surface so the two don't fight for depth,
  so on a curved surface it lies on the surface's tangent plane. It is drawn after the scene's
  objects and sky, hidden by what is in front of it, and blended: a material can cover what is
  beneath (its opacity), glow on it (its emission), or both.
- On a moving body, which the game draws between its last two steps, a decal is drawn where the
  body's mesh is drawn, not a step ahead of it.
- What moves by itself, such as the pulse, follows the world's time. In a game it is the
  simulation's: paused, a decal stands still, and Step moves it on by one step. In the editor's
  own world it is the renderer's, so decals and material previews keep moving while editing.
- Any surface material can be a decal's. The *masked decal material* (`Shaders/MaskedDecal.slang`)
  draws a mask, white on black, in the decal's colour, with smooth edges and an optional glow
  around it; Reveal uncovers it as a wipe along a direction, out from the middle or round it, with
  a brighter edge where it moves, and Pulse dims it and back, *Pulse Rate* times a second. Its
  sample mask, `Textures/DecalRing.png`, a ring, is made by `Scripts/MakeDecalMask.py`.
- The editor's scene views show decals as the game does. Saving, undo and live edits work as for
  any component, and a decal's shader and material reload when saved.
- A click on a decal in a scene view selects it, where its material says the decal is (the masked
  decal: its mask where revealed, not its glow); a click where the mask is empty, such as in a
  ring's middle, picks what is beneath. A hidden or unrevealed decal isn't picked. A selected
  decal's quad is outlined in the selection's colour.
- Tests: `NyxSurfaceDecalTests` (the quad following its host, also between two steps, what is drawn,
  a decal with only its material's path, saving and loading, a game's world's time paused and
  stepped), `NyxSceneGpuTests` (a decal on a cube brightens it where it is, partly when partly
  revealed, and not at all unrevealed, without its host, or behind another cube; on a cube drawn
  between two steps, it is on the cube; with only its material's path, it is drawn as with its
  material, and with a path changed, with the new one; a click on its ring picks it, one in the
  ring's middle the cube; selected, its quad is outlined; its pulse moves in the editor's world, and
  stands still in a game's world while paused until it is stepped).
