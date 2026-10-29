# Nyx features

What the editor and the game do. How the code behind them is organised is in
[Architecture.md](Architecture.md).

- Editor with scene outliner, details panel, asset browser and any number of scene views
- Picking, transform gizmo, undo/redo
- Vulkan renderer
- Reflection system (see below)
- Animated startup banner

## The asset browser

- Right-click a file or folder: what its type can make (*New Material Instance* on a material, *New
  Material for Shader* on a shader), then *Show in Explorer*, which opens Windows' File Explorer at
  its folder with it selected (a folder opened), and *Copy Path*, its full path on the clipboard.
- Right-click where there is no card: the same for the folder shown, and what can be made there
  (*New Material...*, and *New \<type\>* for each reflected asset type, such as *New Physics
  Material* or a game's own, every value written at its default).

## The scene outliner

The scene's entities by name, in folders of the editor's own
([Scene outliner](Design/scene-outliner.md)).

- **Search** at the top: shows the entities whose names have its text, whatever the case, and the
  folders that hold them, open. Ctrl+F reaches it while the outliner has the focus; the cross
  beside it empties it.
- **Order**: drag a row before or after another; a line shows where it goes. A new entity goes
  right after the selected one, in its folder (pasted and duplicated ones too), or at the end. The
  order is saved in the scene, and each move is one undo step.
- **Folders** only sort the outliner: they parent no transform and the game never sees them.
  *New Folder* (the button, or right-click) makes one in the selected folder, or beside the
  selected entity. Drag entities and folders into one (a box shows it) and out again, before or
  after others or below the rows; folders nest, and show before the entities in theirs. Rename one
  with a double click or F2; delete one with *Delete Selected* or its right-click menu, which keeps
  what was in it a level up. Saved in the scene and undoable.
- Which folders are open is remembered per scene, as the editor's own state
  (`Saved/OutlinerFolders.txt`), not the scene's and not undone.
- A scene saved before there was an outline loads unchanged, in its file's order.
- Tests: `NyxEditorOutlinerTests`.

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
- An asset open in its window (a physics material, a game's own type) reaches the game the same way,
  every frame it changes, saved or not, to whoever in the game listens for its type
  ([Reflected assets](#reflected-assets)).
- Tests: `NyxEditorLinkTests` (both ends of the link in one program), `NyxLiveEditTests` and
  `NyxGameLinkSubscriberTests`.

## Runtime inspection

What a running game made of an entity while it runs, in the editor's Details panel
([Runtime inspection](Design/runtime-inspection.md)).

- Some component types are made while the world runs and never saved: a mesh made at run time,
  mesh instances, and a game's own, such as a system's state. A game module makes a reflected type
  one in its `RegisterTypes()` (`ComponentTypeRegistry::SetRuntime<T>()`); a type that isn't
  reflected is registered as one with a name and, if it likes, a list of names and values saying what
  it is (`RegisterRuntime<T>()`, `SetDescribe<T>()`).
- They aren't saved, copied, undone or offered under Add Component. The Details panel shows them
  greyed in a *Runtime* section after the entity's components: their reflected properties with the
  usual widgets, read-only, and their values.
- While Play runs, the section shows the first game's: the editor asks it about the selected entity
  over the editor link four times a second, only while the section is open, and shows its answer
  with the step it was taken at (*Game 1, step 1234*), *waiting for Game 1*, or *Game 1 has no such
  entity*. Without a game it shows the editor's own world's, and only for an entity that has any.
- While the editor asks about an entity, the game marks it (`InspectionRequestComponent`, gone a
  second after the editor stops asking): a game's system that keeps a view of its own for the
  inspector, such as the terms of what it computed, fills it only for marked entities.
- The Game Link window and NyxDump show the questions and answers (`InspectEntity`,
  `InspectedEntity`).
- Tests: `NyxEditorLinkTests` (a question answered over the link, an entity not found, the answer
  read back whole, cut off or oversized refused, both described), `NyxGameModuleTests` (the test
  module's run-time glow made by its system, not saved, answered with its properties read back),
  `NyxComponentTypeRegistryTests` (a reflected type made a run-time type, one that isn't registered
  once, neither saved, data naming one skipped, both inspected), `NyxEditorPanelTests` (what a mesh
  made at run time and mesh instances say, not saved or offered to add, the section from the world
  or a game's answer, drawn with known and unknown types, which entity is asked about),
  `NyxEditorGameModuleTests` (a game's answer with the module's type drawn), `NyxGameInstanceTests`
  (a stand-in game answering, about four questions a second, another entity, none).

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
- In every build but Release, Ctrl+Alt+Shift+C in a game window crashes it on purpose, to try this.
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
- `--window-hidden` never shows the window, e.g. for an automated test; `--frame-stats` and
  `--benchmark` are under [Frame statistics and benchmarks](#frame-statistics-and-benchmarks).
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
- A collider's *Material* names a physics material (`.nyxphysmat`), or none for the engine's
  (friction 0.5, no bounce, the density of water): its *Friction*, *Restitution* and *Density*,
  which gives a body's mass unless its *Rigid Body* gives one, and numbers of the game's own by name
  (*Extras*, e.g. `Extra.Flammability = 0.8` in the file), which Nyx keeps and serves without
  knowing what they mean: a game reads them with `GetPhysicsMaterial(entity)` and
  `GetExtra(material, "Flammability", fallback)`. Right-click in the asset browser for *New Physics
  Material*; double-click one to edit it in its window, a reflected asset's
  ([Reflected assets](#reflected-assets)), with the editor's undo, and *Save* (Ctrl+S); friction or
  density below 0 and restitution outside 0 to 1 show as problems at their values. While the game runs, its bodies take
  the values as they are edited, saved or not: friction and bounce at once, as they move, a density
  that gives a mass by making the body again. A file saved by hand while the game runs is read
  again too. The file keeps its comments and any line Nyx doesn't know. A *Physics Material*
  component still sets one entity's friction and bounce over its collider's material.
- A *Constraint* joins an entity's body to another's, or to the world: *Fixed* holds them together,
  *Point* is a ball joint, *Hinge* a door, *Slider* a drawer, *Distance* a rope or a rod. Each end is
  a point and a rotation in its own entity's space, its *Rest*: the bodies are pulled to where the
  two meet, wherever they are placed. Hinges and sliders can have *Limits* and a *Motor*, turning
  at a speed or pulled to a target; limits and a rope's range can be springs. It can sit on either
  entity or a third, naming both. *Enabled* switches it off and on without losing its rest. Given a
  *Break Force* or *Break Torque*, it breaks once its load stays past it for *Break Seconds*,
  switching itself off; switching it on mends it. A game reads what it held each step (its force
  and torque, on the entity's `ConstraintStateComponent`) and which broke
  (`SimContext::Constraints`). While the game runs, editing it makes it again; switching it and its
  motor reach it as they are.
- A game can make several entities one rigid body while it runs (a *Body Group*, set by its
  systems, never saved): each keeps its collider, its material and its mass, and moves with the
  whole; a ray on it finds the entity it hit; made, the whole keeps their momentum, and broken up
  each flies off as its place moved. A character stands on it and rides it as it moves and turns.
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
- `Assets/Scenes/GroundTest.nyxscene` has the character, crates and an orbit camera in a valley,
  in grass that sways in the wind ([The ground](#the-ground)).
- The editor doesn't simulate: Play shows physics.

## Input

Keyboard, mouse and gamepads (Xbox, DualShock 4, DualSense, through SDL3) go through the engine's
input system: code reads actions such as *Move* or *Look*, bound to keys, buttons and sticks in
contexts, and the context highest on the stack that binds a control gets it.

- Scene views fly with W/A/S/D, the mouse and a pad. In the game, so does a camera with a *Fly
  Camera Component*, which sets its speed and look sensitivity. Nyx's scenes and new scenes have
  one on their camera; a scene without one has no fly camera, and the game's own camera gets the
  input.
- **F** flies the scene view (the hovered one, else the one flown last) to the selected entity in
  0.3 s, keeping the way it looks, close enough that it fills most of the picture: what it draws,
  or a box of its scale where it draws nothing. Moving or looking stops the flight where it is.
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
- In every build but Release, Ctrl+Alt+Shift+I shows the same over a game.
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
- **The physics debug view**: F3 in a game window (every build but Release) shows every body's shape, coloured
  by how it moves (static grey, kinematic blue, awake green, asleep dark green, sensors yellow),
  a mesh's triangles' edges (a mesh of more than 8192 triangles as its bounds, so it doesn't crowd
  out everything else), characters' capsules in orange, the
  last step's contact points with their normals in red, and constraints that are on as pink lines
  between their anchors. It changes with the next step, so while paused, with Step. A cyan cross and arrow
  show where the cursor points at a body, and the surface's normal there ([Cursor rays](#cursor-rays)).

## Build configurations

Four, chosen in Visual Studio's configuration box, with `--config` or with `ctest -C`; each
builds into its own `Binaries\<configuration>` ([Architecture](Architecture.md#build-configurations)).

- **Debug**: nothing optimised, the debug runtime with its checked containers and Vulkan's
  validation layers. A game's tight loops run tens of times slower than in Release.
- **DebugGame**: the engine, the editor and the libraries optimised, the game's own code not:
  to step through the game at nearly the engine's speed. A game marks its targets with
  `nyx_mark_game_code()`, and may optimise its hottest files with `nyx_optimize_game_sources()`.
- **Development**: everything optimised, with debug information, asserts, the debug keys and
  tools, logging and the profiler: for playing and profiling every day.
- **Release**: what ships: no asserts, debug keys, debug drawing or dev tools.
- `NYX_DISABLE_OPTIMIZATION` ... `NYX_ENABLE_OPTIMIZATION` (`Optimization.h`) around functions
  turns optimisation off there, to step through one file of an optimised build.

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

## Frame statistics and benchmarks

- **F8 in a game window** (every build) shows the frame's numbers over the picture's top left: the
  frame rate and time with the last second's worst; on the CPU the updates, the simulation's fixed
  steps, extracting, culling, recording, the UI and waiting for the GPU; on the GPU the frame and
  the scene views' sky tables (in the frames they are made), cloud layer, volumetric clouds, shadows, scene (with the MSAA
  resolve), ambient occlusion, particles, light shafts, bloom, picture and colour grading's bake; the scene views
  drawn and their MSAA samples, their draw calls (and how many draw many instances), instances
  drawn and culled, triangles, shadow cascades with their draw calls and
  triangles, decals, the point lights lit, and the particles with their emitters; and a graph of the last five seconds'
  frame times, the CPU's and the GPU's. The numbers are each second's average, so they hold still
  to be read. `NyxGame.exe --frame-stats` shows them from the start.
- **The editor's status bar** shows the same frame rate and time; the frame time's tooltip has the
  breakdown and the counts.
- **A benchmark** runs a scene, records every frame, writes a CSV and quits:

  ```
  NyxGame.exe Scenes\Valley.nyxscene --benchmark 10 --benchmark-out valley.csv
  ```

  It turns vsync off, waits 3 s for the scene to settle (`--benchmark-warmup <seconds>`) and for
  its shaders to compile, records for the seconds given, and ends with exit code 0 (1 if it
  failed). The CSV has a row a frame with every number above, then the average, the 1% low (what
  only the slowest 1% of frames exceed) and the worst; the log has the summary in a line. Without
  `--benchmark-out` it goes to `Saved\Benchmarks\<scene>-<date>-<time>.csv`.
- **The camera** of a benchmark: `--benchmark-camera <file>` moves a camera of its own along a path,
  a line a key: `<seconds> <x> <y> <z> <look-at x> <look-at y> <look-at z>`, in a straight line
  from key to key. Without one, a scene with several cameras is seen through each in turn, for an
  equal share of the time; else through its primary camera.
- The simulation runs as usual, so a seeded scene repeats, though the fixed steps fall in other
  frames on every run; compare averages and 1% lows of runs of ten seconds or more.
- How it works, and its numbers: [Frame statistics](Design/frame-stats.md).
- Tests: `NyxFrameStatsTests` (the windows' averages and worst, the history, the summary, the CSV,
  the camera path), `NyxGameLaunchOptionsTests` (the options), `NyxGameBenchmarkTests` (a second
  of the default scene in a hidden window, and the CSV it writes).

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
  while something is selected and what they show changed. A scene view, or the material preview,
  is drawn at all only while its window shows it: behind another tab, or in a workspace not shown,
  it culls, casts shadows and draws nothing, and shown again it is drawn from the next frame.

Debug tools:

- *Debug > Painted Chrome* switches back to ImGui's own look.
- *Debug > Mark ImGui Shapes* outlines every shape ImGui still draws itself, to find what isn't
  painted yet. `NyxEditorPanelTests` checks that the editor's panels leave none.
- *Debug > Painter Heatmap* colours each 8x8 tile of every OS window by the painter's work there:
  the commands its pixels walk, or how many draws cover it.
- In every build but Release (`NYX_DEV_TOOLS`), *Debug > Painter Test* shows the painter's test sheet (every
  shape it can draw, zoomable, with a tile heatmap and a comparison with ImDrawList), the sheet
  painted inside a window, the widget gallery in every state (*Save PNG* writes it to
  `Saved/Gallery`), the scene views' blur, and what painting costs on the CPU and the GPU.
- In every build but Release, *Debug > Mockup Overlay* lays an image from disk half transparent over the
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
- Docked panels can be dragged as narrow as 10 pixels (`Nyx::UI::DockedWindowMinWidth`), the
  outliner and details among them; what they hold is cut off at their edge.
- **Scene views**: *Window > New Scene View* opens another, "Scene 2", "Scene 3" and so on, docked
  beside the first and looking from where it looks; each has its own camera after. Any can be
  closed but the last. *Window > Game View in ...* shows a view the way the game sees the scene,
  through its primary camera and without the editor's overlays. The views open, each one's camera
  and whether it shows the game are kept with the layout. *Debug > Culling Bounds*, *Freeze
  Culling* and *Shadow Cascades* reach every view, those opened later too.
- `NyxEditorLayoutTests` saves and loads layouts headless: the docking, the window's placement,
  saving while running, switching workspaces, Reset Layout and keeping the window on the monitors.
  `NyxEditorPanelTests` drags docked panels narrow. `NyxEditorSceneViewGpuTests` opens and closes
  scene views on a GPU: their cameras, culling and debug views, drawn only while shown, and their
  section of the layout read back.

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

## Reflected assets

A game's own data files, edited in the editor without any of the game's code in Nyx
([Reflected assets](Design/reflected-assets.md)).

- A game registers an asset type in its module's `RegisterTypes()`: a file extension, a display name,
  a reflected struct whose properties are the file's contents, and, if it likes, a check of its values
  (`ReflectedAssetTypes::Register()`). Nyx's physics materials are one.
- The file is text, `Name = value` lines, read and written through reflection: numbers, bools,
  strings, enums, colours and vectors, asset references by path, arrays of values on one line,
  structs' values by `Struct.Value`, named entries (an array of structs with a `Name`, each by its
  name: `Attack.Bite.Damage = 12`, or one line for an entry of one value, `Resist.Fire = 0.5`),
  structs on one line (`NYX_REFLECT(meta = (Inline))`: `Range.Sight = 30 10 60`), other arrays of
  structs by index. `TextName` gives a property another name in the file. Writing keeps the file's
  comments and lines it doesn't know, and changes only the lines of values that changed.
- Double-click one in the asset browser to open it in a window of its own, any number at once: its
  values in the details table with the editor's undo, a dirty marker, *Save* (Ctrl+S). Named
  entries are added with a name of their own (*Entry*, *Entry2*, ...), renamed by their *Name* and
  taken away or moved by their row's right-click menu.
- What is wrong is shown under the value it is about, in red: values in the file that can't be read,
  the type's own check (e.g. a minimum above its maximum), and entries' names that are missing, twice
  or not letters, digits and _.
- A file changed on disk is read again into its window if it has no changes; with changes the window
  says so, with *Reload*.
- While a game runs, each change reaches it, saved or not, for whoever listens for the type
  (`ReflectedAssetListener`).
- Tests: `NyxReflectedAssetTests`, `NyxReflectedAssetEditorTests`.

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

## Overridable values

A component that inherits most of its values from somewhere else, such as a data file or a parent,
and overrides a few, as a material instance does its parent's parameters
([Overridable values](Design/overridable-values.md)). It keeps only its overrides; everything else
follows the source, also when the source changes.

- The overrides are a property `std::vector<Nyx::ValueOverride>` (a name and a value) with
  `UI = Overrides`: saved, copied, undone and sent to a running game like any array.
- A game module lists what an instance can override in its `RegisterTypes()`
  (`ComponentTypeRegistry::SetOverridableValues<T>()`): each value's name, display name, inherited
  value, range, tooltip and source. The list may depend on the instance, so it is asked every time
  the Details panel draws.
- The Details panel shows one row per listed value instead of the array: an inherited value greyed,
  with where it comes from in its tooltip; an overridden one in the normal colour with a reset
  arrow. Editing a greyed value overrides it; edits stay in the value's range. Each edit, reset and
  removal is one undo step.
- An override that isn't listed (renamed, or of a value this instance doesn't have) or a second one
  of a name is shown as *not listed*, with *Remove*; it is never dropped by itself.
- Without a provider the property is drawn as any array.
- Floats only, for now. `GetOverriddenValue()` gives what an instance uses.
- Tests: `NyxEditorGameModuleTests` (the test module's lantern: rows, overriding, reset, a value
  not listed, undo of each, the range, a component without a provider), `NyxComponentTypeRegistryTests`.

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
- In a game window, the physics debug view (F3, every build but Release) shows where the cursor points at a
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

- The mesh is plain data (`MeshData`): vertices (position, normal, UV, colour, tangent, four
  custom channels for the materials), indices, three a triangle, and *sections*, runs of the
  indices each drawn with the material of its slot, so a tree's trunk and leaves are one mesh with
  two materials. A mesh without sections is one section in slot 0. `CheckMeshData()` says what is wrong with one, `ComputeMeshBounds()` where it is.
- The entity gets a `RuntimeMeshComponent` holding the data, shared, and a revision. With its
  *Mesh Renderer*, it is drawn in place of the renderer's *Mesh*: slot 0 with *Material*, slots 1 and
  on with *Extra Materials*; a slot without a material draws with *Material*.
- The renderer puts each mesh on the GPU once, when it is first drawn, and again only when the
  component's revision goes up: change the data, or give new data, then raise it
  (`SetRuntimeMesh()`, `MarkRuntimeMeshChanged()`). Entities sharing one `MeshData` share its upload.
  A mesh no view draws any more is let go of. Data that isn't a valid mesh isn't drawn, and is
  logged once a revision.
- Its buffers are in GPU memory the renderer keeps in blocks of 64 MB (`MeshMemoryPool`), each bound
  to its part of a block, so a mesh costs no allocation of its own and a world can have thousands
  (drivers allow few allocations, often 4096); 2500 small meshes fit in one block. Its data is
  copied in through a staging buffer by the frame's commands before anything draws
  (`MeshUploads`), and the GPU reads it from its own memory. A block that empties goes, but the
  last stays for the next meshes.
- It is drawn, picked, outlined when selected, shows its bounds, is drawn between steps when it
  moves, and carries surface decals, as a mesh loaded by its path does.
- It isn't saved, edited or sent to a game: whoever made it makes it again after a scene is loaded.
  Content made from a component's values, such as the ground, is made in the component's post-load
  step, so it shows in the editor and the game alike and follows every edit
  ([Architecture: Generated content](Architecture.md#generated-content)).
- The editor's details panel shows it greyed in its *Runtime* section, as *Mesh Made at Run Time*:
  its vertices and triangles, each section's triangles and material slot, whether it is drawn, and
  its revision; while Play runs, as the game has it ([Runtime inspection](#runtime-inspection)).
- A *Mesh Collider* makes it ground to stand on ([Physics](#physics)).
- Tests: `NyxMeshTests` (which meshes are valid, the sections a mesh without any is drawn in, the
  bounds), `NyxMeshMemoryPoolTests` (the pool's free ranges: aligned, apart, first fit, merged when
  given back), `NyxRuntimeMeshGpuTests` (a sphere made in code draws exactly as the built-in sphere;
  changed and its revision raised it is drawn anew, unchanged it is uploaded once; entities sharing
  a mesh share its upload, and one nobody draws is let go of; 2500 meshes in one block of the pool,
  their memory given back when let go of and used again; one that isn't valid draws nothing; a
  click picks it, selected it is outlined; two sections draw with two materials; a decal on it is
  drawn), `NyxEditorPanelTests` (what the details panel says of it).

## Faces and two-sided materials

Only the front of a triangle is drawn, the side its normal faces: half of a closed mesh's
triangles cost nothing.

- Every mesh Nyx makes winds counter-clockwise seen from its front, as glTF's do: the built-in cube,
  sphere, capsule, plane and crossed quads, a ground, a scatter's copies.
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
- A **material** can cast no shadows: untick *Cast Shadows* under *Material* in the material editor,
  or write `CastShadows = false` in the `.nyxmat` file; instances take it from their parent unless
  they set their own. Everything drawn with it, on any number of entities, then stays out of the
  shadow maps and costs the shadow passes nothing, while it is still drawn and still receives
  shadows. A mesh casts where both its *Mesh Renderer* and its material do: of a mesh with several
  materials, only the sections whose materials cast. It takes effect at once, while the scene is
  open.
- The light's *Shadows* settings: *Cast Shadows*; *Cascades* (1 to 4); *Shadow Distance* (metres
  from the camera; shadows fade out over its last tenth); *Split Blend* (how the cascades share the
  distance: 0 evenly, 1 each a few times the one before, so near ones are small and sharp);
  *Depth Bias* and *Normal Bias* (in texels: more takes acne away, too much lifts shadows off their
  casters); *Softness* (how many texels on each side are filtered together, 0 to 3); *Resolution*
  (texels across each cascade, 256 to 4096). All with undo, saved in scenes and sent to a running
  game.
- The editor's free camera shows the scene's sun and its shadows, as the game's camera does.
- **Seeing it**: *Debug > Shadow Cascades* in the editor (F5 in the game, every build but Release) tints the
  picture by the cascade each surface's shadow comes from (red, green, blue, yellow, nearest first)
  and shows each cascade's shadow map along the bottom left. The profiler has a zone per cascade.
- Four cascades of 2048 texels cost the instancing benchmark's field of 50 000 cubes, on a ground,
  about 1 ms of GPU time and 0.1 ms of the renderer's CPU time a frame in Release ([Shadows](Design/shadows.md#numbers)).
- Tests: `NyxShadowCascadeTests` (the splits; the spheres round the view's slices hold them, no
  bigger, the same however the camera turns; the light's view of a sphere holds it, reversed, back
  to the casters; a still point keeps its place in its texel as the camera walks and turns),
  `NyxShadowGpuTests` (a box and an instanced field shadowing a plane, against a reference image;
  no shadow with the light's or the box's casting off; a one-sided quad's back casts nothing, a
  two-sided one's does; a box and the field in a material that casts none cast nothing, the field's
  cells not even culled by the cascades, the plane still shadowed by the rest, and the material
  saved or set from the editor switching casting on and off live; of two sections, only the one
  whose material casts; each cascade culled by its own frustum; two cascades; the editor's camera;
  the debug view), `NyxMaterialTests` (the `CastShadows` option read, written, inherited and
  overridden), the scene and material reference pictures unchanged.

## Fog and the sky

An *Environment* component on any entity gives the world its air, in the editor's scene views and
in the game alike: height fog, thick near the ground and thinning upwards, and a sky that follows
the sun. Without one, nothing changes.

- **Height fog** (*Fog*): *Density*, *Height Falloff* (how fast it thins upwards), *Height* (where
  it is as thick as its density), *Start Distance* (nothing nearer is fogged), *Color*, *Max
  Opacity* (below 1, the far hills and the horizon still show), *Sun Scattering* and *Sun Lobe*
  (the haze glowing towards the sun, in the light's colour). Meshes, instanced ones, two-sided
  ones and decals are fogged, a decal's glow dimmed through it; the sky is hazy on the horizon. In
  the Atmosphere mode the fog takes the sky's colour; *Haze Brightness* and *Haze Tint* change it.
- **The sky** (*Sky*), as its *Mode* makes it:
  - **Atmosphere** (the default): the sky made from the sun's place by the light the air scatters
    (Hillaire's sky), no texture: blue by day, gold, orange and red as the sun goes down, haze round
    the sun as a halo round it, a sun's disk (*Sun Disk Size*, *Sun Disk Brightness*) bright enough
    to bloom, darker and redder at its edge. Its light lights the world: surfaces the sun doesn't reach take the
    sky's colour, bluish by day, dimmer and warmer at golden hour; the fog takes the sky's colour the
    way it looks (the horizon's below it), brighter towards the sun, so fog and sky agree; the sun's light is what the air
    leaves of it. For a camera high up (a treetop, a cloud's height), the horizon sinks below eye
    level and the sky above darkens. The air is the earth's by default and every part of it a
    setting: *Rayleigh Scattering*, *Scale* and *Height* (the molecules: the blue), *Mie
    Scattering*, *Scale*, *Absorption*, *Height* and *Anisotropy* (the haze: the glow), *Ozone
    Absorption* and *Scale*, *Ground Albedo*, *Planet Radius*, *Atmosphere Height*, and *Warmth*, a
    stylising tint of the sky's light. At night the air's sky is black; *Night Sky Brightness* and
    *Night Sky Color* keep an artistic light in it (and in its light on the world), fading in as the
    sun goes down, none by day; 0 is none, the air's own sky (0.02, a dark blue night, by default).
    *Sky Light Intensity* multiplies the sky's light on surfaces, not the sky itself (2.5 by
    default, so shade isn't flat next to the cube map's; 1 is the air's own).
    Its tables are made again only when the air, the sun or the
    camera's height changes: about 0.13 ms of GPU time then, nothing otherwise
    ([Atmosphere](Design/atmosphere.md#the-sky-made-by-the-air)). *Debug > Sky Tables* in the
    editor (every build but Release) shows them.
  - **Tinted Cube Map**: the sky's cube map and its light take the sun's hue (*Sun Tint*) and dim
    through dusk to *Night Brightness*; the sun's light warms and weakens as it goes down, through
    more air (*Sun Reddening*), and is gone once it has set; a glow round the sun (*Sun Glow*, *Sun
    Glow Lobe*).

  - **Cube Map**: the sky's cube map as it is, whatever the sun does.

  *Exposure Compensation* (*Exposure*) brightens or darkens the whole picture before tonemapping,
  in stops, in every mode. A low sun gives warm light, warm haze and long warm shadows. Scenes saved with the old *Follow
  Sun* off load as *Cube Map*, as they looked; with it on, in the mode they had.
- All with undo, saved in scenes and sent to a running game. `Assets/Scenes/AtmosphereTest.nyxscene`:
  the ground test scene with small cubes scattered over its ground, haze and a low sun.
- The fog and the sky cost no measurable time, on the GPU or the CPU, with 50 000 instanced cubes in
  view ([Atmosphere](Design/atmosphere.md#numbers)).
- Tests: `NyxAtmosphereTests` (the sun through the air; the daylight; the environment's values in
  range; the fog along rays against the fog summed in steps; the air's transmittance, 1 at no
  distance and falling with the path, the sun's light redder low, the horizon's dip 2 km up, the
  sky's settings in range and what is made again as they change; the component saved),
  `NyxAtmosphereGpuTests` (fog on surfaces and the sky against the CPU's numbers; a decal over
  fog; instanced, plain and two-sided alike; shadows through fog; the tinted sky darker, warmer
  and glowing round a low sun, the light warmer; nothing changed with it all off; a reference
  picture; the test scene), `NyxSkyAtmosphereGpuTests` (the air's tables against the CPU's; the
  sky bluer at noon than at sunset; from 2 m and 2 km up, the horizon at and below eye level and the
  zenith darker; a wall lit by the sky's light; the fog the sky's colour at the horizon; the sun's
  disk blooming; made again only as needed; the glow round a low sun a halo, not a band up the sky, with fog and without; the night's light,
  none at 0 and none at noon; unchanged to the bit in the *Cube Map* mode; its GPU
  times), the other reference pictures unchanged.

## Time of day

A *Time Of Day* component on any entity turns the sun by the time, so the sky, the fog and the
shadows, which follow the sun's direction, follow the time: from noon through a long warm sunset to
night. In the editor's scene views and in the game alike; the world's first one counts. Without
one, nothing turns the sun.

- *Time*: hours, 0 to 24 (6 sunrise, 12 noon, 18 sunset); past 24 it wraps. Drag it in the Details
  panel and the sun turns at once, in the editor's views and, as a live edit, in a running game,
  also while it is paused; undo turns it back.
- *Time Runs* and *Day Length*: a running game moves the time on with its steps, a day every *Day
  Length* seconds. The time after a step is the time it ran from plus the world's time since, so a
  recording played again, or another machine, gets the same sun whatever its frame rate; Pause
  stops it, Step moves it one step. A time sent from the editor while it runs goes on from there.
  The editor shows the time as it is set; it runs only in a game.
- The sun's path (*Sun*): which entity is the sun (a *Directional Light*; none: the first primary
  one), *Path Tilt* (like a latitude: 0 overhead at noon, 30 at noon 60 degrees up), *Sunrise
  Direction* (where on the horizon it rises, degrees round from +X towards -Z).
- **Night**: below the horizon the sun's light fades out over *Horizon Fade* degrees, so a sun under
  the ground lights nothing from below, with an *Environment* or without; a light that gives no
  light casts no shadows, so none are drawn at night. With an *Environment*, the air reddens the
  sun as it goes down and the sky dims to its night (the tinted mode's *Night Brightness*, the
  Atmosphere mode's *Night Sky Brightness*).
- The sun's rotation is the time of day's: turned by hand, it turns back to the time. Its position,
  colour, intensity and shadows are its own.
- All with undo, saved in scenes and sent to a running game.
- Tests: `NyxTimeOfDayTests` (the sun's direction at sunrise, noon, sunset and midnight on a
  straight and a tilted path, the time wrapping; the light's rotation away from the sun all day,
  without rolling or jumping; the fade below the horizon, and the air's light and sky from noon to
  night; the sun found by guid or as the primary light; a game world's steps moving the time on,
  through midnight, without drift, the same to the bit with different frame times; a time sent live
  going on from there; a new day length without a jump; the component saved),
  `NyxTimeOfDayGpuTests` (the sky and the ground darker at sunset and at midnight than at noon; a
  ceiling under a sun straight below lit by the light alone, not by the time of day's sun).

## Bloom

Light brighter than the screen can show spreads round what gives it off: emissive materials and
decals glow, in the editor's scene views and in the game alike. On with an *Environment*, under
*Bloom*; without one, nothing changes.

- *Threshold* (how bright light must be to bloom, after the exposure: 1 is white on the screen),
  *Knee* (how softly it starts below the threshold), *Intensity*, *Radius* (how far it spreads,
  0 to 1), *Levels* (how many halvings, 1 to 8; each spreads twice as far). All with undo, saved in
  scenes and sent to a running game.
- Each scene view has its own, made for its size. `Materials/Emissive.nyxmat` glows orange; a
  `MaskedDecal` with an intensity above 1 glows in its colour. The atmosphere test scene has both.
- **Seeing it**: *Debug > Bloom* in the editor (F6 in the game, every build but Release) shows the bloom
  alone.
- Its passes take about 0.05 ms of GPU time a 1280 × 720 view ([Atmosphere](Design/atmosphere.md#numbers)).
- Tests: `NyxAtmosphereTests` (the levels, their sizes, the threshold's knee, the settings in
  range), `NyxBloomGpuTests` (an emissive cube and decal spreading their light into the black
  round them, and only there; a threshold above them, no intensity, an exposure under the
  threshold and no environment change nothing; a wider radius spreads further; a view's own chain,
  a second view's; the debug view; a view whose world goes away; a reference picture; the test
  scene), `NyxEditorSceneViewGpuTests` (*Debug > Bloom* reaching views opened later), the other
  reference pictures unchanged.

## Ambient occlusion

The sky's light reaches less far into creases, corners and the feet of things: crates sit on the
ground, grass darkens down among its blades. On with an *Environment*, under *Ambient Occlusion*;
without one, nothing changes. Only the sky's light is dimmed, never the sun's.

- *Radius* (how far round a point what hides the sky is looked for, in metres), *Intensity*,
  *Quality* (Low, Medium, High: more samples are smoother and cost more). All with undo, saved in
  scenes and sent to a running game.
- Found from the scene's depth at half the view's size, blurred and filtered up along edges, in
  every scene view; it works the same on instanced meshes, decals and through fog.
- **Seeing it**: *Debug > Ambient Occlusion* in the editor (F6 twice in the game, every build but Release)
  shows the occlusion, white where all of the sky's light reaches.
- It takes about 0.1 ms of GPU time a 1280 × 720 view ([Atmosphere](Design/atmosphere.md#numbers)).
- Tests: `NyxAtmosphereTests` (its size, samples and settings; a slice of the sky open, beside a
  wall, walled in; a tilted open surface wholly visible), `NyxAmbientOcclusionGpuTests` (a box's
  foot darker and the open floor not; never the sun's light; radius, intensity, qualities, no
  environment; through fog and on a decal; a dense instanced field, its noise and cost; the debug
  view; a reference picture), `NyxEditorSceneViewGpuTests` (*Debug > Ambient Occlusion* reaching
  views opened later), the other reference pictures unchanged.

## Light shafts

The sun streams through the gaps in what stands before it, in shafts out from the sun across the
picture: between columns and branches, past the edge of a wall, round the sun behind a cloud's
edge. In the editor's scene views and in the game alike. On with an *Environment*, under *Light
Shafts*, off by default; without one, or off, the picture is as it was, to the bit.

- *Intensity*, *Length* (how far a shaft's light carries from its gap, as a part of the way to the
  sun), *Density* (how far towards the sun each pixel gathers light), *Depth Threshold* (how far
  away what stands before the sun starts to let its light through as the sky does, in metres),
  *Tint* (times the sun's colour, which the shafts take as it reaches the camera: golden at golden
  hour). All with undo, saved in scenes and sent to a running game.
- They fade out as the sun leaves the picture, goes behind the camera or sets: none at night, none
  from a sun behind you. They work the same from the ground and from a camera at cloud height.
- A blur along lines out from the sun's place on the picture, at half the view's size: cheap and
  stylised, but only what is on the picture makes them, and they lie over it rather than in the
  air ([Atmosphere](Design/atmosphere.md#light-shafts) has how, and its limits next to real
  volumetric fog).
- `Assets/Scenes/LightShaftsTest.nyxscene`: a colonnade against a low sun, with a time of day to
  scrub.
- They take about 0.055 ms of GPU time a 1280 × 720 view, and nothing while the sun gives none
  ([Atmosphere](Design/atmosphere.md#numbers)).
- Tests: `NyxLightShaftsTests` (the sun's place on the picture, from the ground and 2 km up; none
  behind the camera, beside it or far off the picture, some just past its edge; the fade as it
  leaves the picture and as it sets; the length's decay; values in range; the component saved),
  `NyxLightShaftsGpuTests` (a wall with a window, the sun behind it: the wall beyond the window
  brighter along the sun's line, the nearer the more, the rest of the wall unchanged; the same 1.5
  km up; a sun behind the camera, below the horizon or far to the side, no intensity, and no
  environment, to the bit; no shafts in a debug view of the materials; a view whose world goes
  away; their GPU time; the test scene), the other reference pictures unchanged.

## Cloud layer

A *Cloud Layer* component on any entity puts a thin layer of high cloud in the sky made by the air
(the *Environment*'s sky in its *Atmosphere* mode), in the editor's scene views and in the game
alike: a field of small cloudlets, a mackerel sky, or streaks of cirrus, receding to the horizon.
Without one, or off, or with another sky, the picture is as it was, to the bit.

- **Shapes from seeded noise**: *Coverage*, *Type* (cloudlets to cirrus, blended), *Cloudlet Size*,
  *Softness*, *Detail* (finer cells breaking up their edges), *Warp*, *Seed*; *Altitude* (4000 m)
  and *Thickness*. The noise is a small tiling texture made by a compute shader when the seed
  changes; the same seed at the same world time gives the same sky.
- **Wind**: the world's wind at the layer's altitude carries the clouds, their streaks along it
  ([Wind](#wind)); with *Own Wind*, or in a world without a wind, *Wind Direction* and *Wind Speed*
  do. With the world's time, a game's by its steps (they stand still while it is paused), not the
  frame rate; the clouds stay put over the ground as the camera moves.
- **Lit by the sun through the air at their height**: white at noon, gold and red at sunset (the
  layer keeps the sun a little after the ground has lost it), dark after it, the night sky's blue
  at night; brighter towards the sun, their thin edges glowing against it (*Rim*, a silver lining);
  the sky's own light on their shaded side (*Ambient*); *Brightness*. They fade into the haze
  towards the horizon.
- **From below, at and above its height**: from the ground it spreads overhead to the horizon; from
  above (a treetop or a hill over it) it lies below the horizon, lit from above, with the open sky
  above; crossing its height there is no sheet sweeping through the picture: the clouds near the
  camera fade, those further off stay. Seen from above, the ground under it is covered by it; what
  stands above it isn't.
- **It lights the world** (*Lights The World*, on by default, and *Light Strength*): the sky's light
  on the world has the clouds in it, so white clouds brighten and whiten the fill light, golden ones
  at sunset warm it, heavy cover softens it, and above the layer its tops light the world from
  below. Off, the world is lit as without the layer, to the bit. The sky's light is made again as
  the wind moves the clouds, a little at a time (about 0.12 ms every second or so).
- It holds back the light shafts where it covers the sun.
- All with undo, saved in scenes and sent to a running game. `Assets/Scenes/CloudLayerTest.nyxscene`:
  a field of cloudlets at dusk, graded gold-green, with a time of day to scrub.
- About 0.11 ms of GPU time a 1280 × 720 view and 0.2 ms at 1920 × 1080; nothing without it ([Atmosphere](Design/atmosphere.md#the-high-cloud-layer) has how it works, its limits
  and what the volumetric clouds will replace).
- Tests: `NyxCloudLayerTests` (where rays meet the layer from below, at and above it, against a ray
  marched in steps, none behind the ground; the clouds near the camera faded; values in range; the
  wind carrying the pattern with the world's time, anchored in the world, after hours still exact;
  when the sky's light is made again for the clouds; the component saved), `NyxCloudLayerGpuTests` (the picture unchanged to the bit without it, off,
  or under the cube map's sky, and no GPU time; clouds at noon, brighter towards the sun, warmer at
  sunset, dark at midnight; from 2 km above, the layer lit below the horizon and the open sky above
  it to the bit; crossing it without a pop, the clouds by the camera gone; the same seed and time
  the same picture, moved by the wind, another seed others; the ground under a low layer covered, a
  column above it not; a white wall lit by the sky alone brighter and whiter under a white layer,
  warmer at sunset, half way at half strength, the same to the bit with *Lights The World* off; a
  ceiling above the layer lit from below by it; the sky's light made again as the wind moves it, not
  every frame; the light shafts held back; its GPU time; the test scene; and again with MSAA), the other GPU tests' pictures unchanged to the bit.

## Volumetric clouds

A *Volumetric Clouds* component on any entity puts clouds with volume in the sky made by the air
(the *Environment*'s sky in its *Atmosphere* mode), in the editor's scene views and in the game
alike: a band of them between two altitudes round the planet's curve, seen from the ground, from
inside and from above. Without one, or off, or with another sky, the picture is as it was, to the
bit. A *Cloud Layer* can stay above them for thin cirrus.

- **At a game's scale**: *Bottom Altitude* and *Top Altitude* above the world's height 0 (400 and
  1800 m by default and in the presets), clouds a few hundred metres across (*Cloud Size* 700 m),
  low enough to reach from a hill or on something that rises; the whole range from 0 to 20 km stays.
  A hill rising into them is in their fog, more of it the further, not cut off.
- **A weather map** places them: tiling, seeded, carried by the wind with the world's time, it gives
  each place its coverage, type and density, so the sky has patches and bands of cloud and open
  blue between. *Coverage*, *Coverage Variation* (0 an even field, 1 patches), *Type* (0 flat
  stratus, 0.5 small puffs, 1 towering cumulus), *Type Variation*, *Density*, *Weather Size*.
- **Shapes from 3D noise**, made by compute shaders once per *Seed*: billowy Perlin-Worley shapes
  (*Cloud Size*), parted into separate cells as much as asked (*Cellularity*), swelled into round
  lobes from just above their base (*Lobes*, the heads of towering cumulus), worn at their edges by
  smaller billows and wisps at two sizes, many-lobed at the top, wispy at the base (*Detail*, *Detail
  Size*, *Billowiness*), swirled by curl noise into torn wisps (*Wispiness*), thinning out over a
  translucent fringe rather than cut off (*Edge Softness*); their height by their type, flat at the
  base.
- **Wind**: the world's wind at the middle of the band carries them ([Wind](#wind)); with *Own
  Wind*, or in a world without a wind, *Wind Direction* and *Wind Speed* do. With the world's time,
  a game's by its steps (they stand still while it is paused); they stay put over the ground as the
  camera moves.
- **Lighting**: the sun through the air (white at noon, gold at sunset, none after) and through the
  clouds themselves (Beer's law), scattered once and many times (*Multiple Scattering*) and carried
  through their bodies (*Translucency*), so their bellies and backlit cores glow rather than going
  black, with a silver lining on their thin edges against the sun (*Silver Lining*, *Silver Lining
  Spread*) and darker sunlit edges seen with the sun behind (*Powder*); the sky above and the ground
  below on their shaded sides and at night (*Ambient*), held back by the cloud just above, so their
  lobes show in their shade too (*Sky Occlusion*); painted colours: *Sunlit Tint* (cream tops)
  and *Shadow Tint* (grey-violet bellies, warm undersides); *Brightness*. They pale into the haze far
  off, more than the air alone would pale them (*Aerial Perspective*).
- **The sun's disk** is dimmed by what gets through the clouds themselves, the volumetric ones and
  the high layer: gone behind thick cloud, not a sharp circle through it; the bloom and the light
  shafts see it so.
- **Presets**: *Preset* sets the values to a look at once: *Small Puffs* (a field of small separate
  cells receding in rows), *Cumulus* (big many-lobed heaps, violet bellies, puffs round them, open
  blue), *Broken* (many small torn fragments glowing against a low sun), *Bright Day* (towering,
  many-lobed cumulus, brilliant white in the sun, sky-blue in their shade); the values can be changed
  after, and *Custom* leaves them.
- **Cheap**: *Resolution* (half the view's size, or its own) and *Update*: every pixel every frame,
  or one pixel of each 2 x 2 or 4 x 4 a frame, the others carried from the frames before where the
  clouds were (held to what this frame's marched pixels round them span, blended, so no pattern of
  the squares shows while the camera turns), more marched while the picture moves fast; every pixel
  at once when something changes the look. Older scenes' *Temporal* off loads as *Every Pixel*.
  Lightning's light in them is worked out apart, every pixel near a bolt every frame, so it never
  smears ([Lightning](#lightning)).
  *Steps*, *Light Steps*, *Max Distance*. In the default, one pixel of each 4 x 4: 1.3 ms of GPU time
  a frame at 1920 x 1080 with the camera still, 2.2 ms turning; 4.7 ms marching every pixel.
- They hold back the light shafts where they cover the sun, and cover what lies among and beyond
  them.
- *Debug View* shows the weather map, the sun's or the sky's light alone, or which pixels were
  marched this frame.
- All with undo, saved in scenes and sent to a running game. `Assets/Scenes/VolumetricCloudsTest.nyxscene`:
  cumulus over a town's wall and hills rising into them, in the afternoon, with a time of day to
  scrub, graded gold-green at dusk. `Assets/Scenes/BrightDayTest.nyxscene`: a bright midday over the
  sea, the clouds on *Bright Day*, a breeze carrying them, a high-key environment and a saturated
  cyan grade, a jetty and an islet for scale ([Atmosphere](Design/atmosphere.md#the-volumetric-clouds) has how
  they work, their cost and their limits).
- Tests: `NyxVolumetricCloudsTests` (where rays are inside the band from below, inside and above it,
  each end on a shell, none past the ground; every pixel of a 2 x 2 or 4 x 4 marched in 4 or 16
  frames, smaller squares while the picture moves fast, how far it moves; values in range; the wind
  carrying the noises with the world's time, anchored in the world, after hours still exact; when
  everything is marched again; the presets setting the values once, at a game's scale, the older
  ones without lobes or sky occlusion; older scenes' *Temporal*; the component saved), `NyxVolumetricCloudsGpuTests` (the picture unchanged to
  the bit without them, off, or under the cube map's sky, and no GPU time; clouds by day, thin ones
  glowing towards the sun, thick ones' sunlit sides brighter away from it, the silver lining, warmer
  at sunset, dark at midnight; from above, their tops lit and the open sky above to the bit; each
  preset; still pictures in each update mode the same to the bit as every pixel marched, and a
  turning camera's frames close to every pixel marched, every pixel marched turning fast; the sun's
  disk gone behind thick volumetric clouds and a thick high layer; among the clouds, a near wall
  less covered than a far one; the same seed and time the same picture, moved by the wind, another
  seed others; the ground under them covered from above; the light shafts held back; their GPU time
  in each mode; the test scene; the bright day scene, its sunlit tops near white and its shade blue;
  and again with MSAA), the other GPU tests' pictures unchanged to the bit.

## Cloud modifiers

A *Cloud Modifier* component changes the volumetric clouds round its entity by its *Shapes*, each
animated by the world's time: a *Clear Column* (a hole through them, *Radius* with a soft edge
*Width* wide, *Strength* how much thins out), a *Thicken Ring* (denser cloud round the axis, as if
piled up), a *Swirl* (the clouds turned round the axis, further the longer it goes on, *Strength*
turns a minute: a vortex). Each between *Bottom* and *Top* (the whole band by default), from *Start
Time* for *Duration* (0: for ever), fading in and out over *Fade In* and *Fade Out*. A selected one's
shapes are drawn as circles. Lightning parts the clouds the same way ([Lightning](#lightning)).

- Every sample of the clouds takes them: their shapes, their light and shadow, the lightning's glow.
  A view takes the 8 nearest; each pixel only those its ray comes near, so shapes elsewhere cost it
  nothing. Without one the picture is as it was, to the bit.
- All with undo, saved in scenes and sent to a running game.
- Tests: `NyxCloudModifierTests` (a shape's strength over its life; a column, a ring and a swirl at
  a point as the shader does them; heights; the nearest a view takes; a world's; a lightning
  entity's channel lingering and closing, a column's with its swirl; saved and loaded),
  `NyxLightningGpuTests` (a cleared column leaving the clear sky, a ring adding cloud, a swirl
  turning them as time goes on while without one a later time is the same picture, a column's
  channel parting them; their cost).

## Colour grading

A *Color Grading* component on any entity grades the picture, in the editor's scene views and in
the game alike, after the tonemap: what is shown, not the light. Without one, or with *Grading*
off, the picture is as it was, to the bit.

- A grade: *Temperature* and *Tint* (the white balance: warmer or cooler, greener or more
  magenta), *Contrast* (round mid grey, black and white kept), *Lift*, *Gamma* and *Gain* (the
  shadows, midtones and highlights, each red, green and blue), *Saturation* (the brightness kept),
  and split toning: *Shadow Tint* and *Highlight Tint*, each a hue with a strength, and *Tint
  Balance* (where the shadows' tint gives way to the highlights'). The defaults change nothing.
- **By the sun's height**: with *Use Keys*, *Keys* holds grades at heights of the primary light
  (e.g. night, a gold-green dusk, a warm golden hour, noon), blended by where the sun is: halfway
  between two keys, halfway; below the lowest or above the highest, that one. Without, the one
  *Grade*. Only the sun's height counts: a camera on the ground and one at cloud height grade
  alike.
- Baked into a 32 × 32 × 32 lookup table per view whenever the grade moves noticeably (a moving
  sun bakes it every few frames, a still one never); the tonemap looks each pixel up in it.
- All with undo, saved in scenes and sent to a running game: every edit shows at once.
  `Assets/Scenes/ColorGradingTest.nyxscene`: the atmosphere test scene graded by four keys for a
  painted golden hour.
- Costs: the lookup about 0.002 ms of GPU time a 1280 × 720 view, in the picture pass; a bake about
  0.006 ms, in the frames that bake ([Atmosphere](Design/atmosphere.md#numbers)).
- Tests: `NyxColorGradingTests` (the defaults' table the identity; each control's way, black and
  white kept where they should be; values out of range; the keys halfway and outside; the sun's
  height; baked again only when the grade moved; the component saved), `NyxColorGradingGpuTests`
  (the table read back against the CPU's grade; baked once for a change, not for one too small to
  show; graded pixels against the CPU's; the keys by the sun, the camera 1.5 km up graded alike;
  without it, or off, the picture to the bit; a reference picture; the test scene), the other
  reference pictures unchanged.

## Point lights

A *Point Light* lights what is round it from its entity's position, such as a lamp or a fire: in
every colour, fading to nothing at its range, on every lit material, decals included, in the
editor's scene views and in the game alike.

- *Color*, *Intensity* (what a white matte surface facing it shows from a metre away, as the sun's
  intensity is what it shows facing the sun) and *Range* (in metres; shorter is cheaper). All with
  undo, saved in scenes and sent to a running game.
- Its light falls off as the square of the distance, softened to reach 0 at the range without an
  edge. It is lit as the sun is (diffuse and a highlight), seen through the fog as everything is,
  and never dimmed by ambient occlusion. It casts no shadows yet.
- Each view lights its pixels with the lights whose range reaches into it, up to 256; a light whose
  light reaches nowhere in view costs nothing there. A light moving with a body follows its mesh.
- Emissive materials and decals glow but light nothing: a light beside them does
  ([Lights and particles](Design/lights-and-particles.md#emissive-surfaces-dont-light)).
- **In the editor**: each light shows as a star of lines in its colour, which a click selects; a
  selected light's range is drawn round it.
- `Assets/Scenes/LightsTest.nyxscene`: the atmosphere test scene at dusk with six coloured lights,
  the glowing cube and ring each with a light of its colour.
- 64 lights in view take 0.25 to 0.5 ms of GPU time a 1280 × 720 view
  ([Lights and particles](Design/lights-and-particles.md#numbers)).
- Tests: `NyxPointLightTests` (the falloff; the lights a frame draws, a moving one between its steps;
  a view's culling by its frustum, behind the camera, past the far plane, and past the most a view
  takes), `NyxPointLightGpuTests` (a light on a floor against the CPU's numbers out to its range and
  black past it; brighter, further, moved; a coloured light; two adding up; under fog; behind the
  camera; one out of view culled, changing nothing; never dimmed by ambient occlusion; its icon
  picked and its range drawn in the editor's views; a reference picture; the test scene), the other
  reference pictures unchanged.

## Wind

One wind for the world, which grass, trees, particles and a game's own air all take
([Design: Wind](Design/wind.md)).

- A *Wind* component (the world's first counts; without one the air is still): *Blowing*,
  *Direction*, *Speed*; *Gusts*: *Gustiness*, *Frequency*, *Spacing*, *Patch Width*; *Turbulence*
  and *Eddy Size*; *Height*: *Ground Height*, *Near Ground*, *Reference Height*, *Aloft*, *Aloft
  Height* (slower near the ground, faster high up); *Change Time*; *Local Air* (the texture of a
  game's air round the camera); *Debug View*. With undo, saved in scenes, sent to a running game.
- **Gust fronts travel over the land** along the wind, each strong in some places along it and weak
  in others, so a squall sweeps over the ground and neighbouring plants lean together.
- **Steerable and smooth**: its values are targets, set in the editor or by a game (a weather
  system later); it turns to them over *Change Time* without a jump in its gusts or eddies, and the
  same targets at the same times give the same wind whatever the frames or steps.
- **The same on the CPU and the GPU, to the bit**: one small contract (`WindState`,
  `SampleWind()`, `nyx::sampleWind()` in `nyx.wind`), so a game's simulation and what is drawn
  agree; a game's systems get it each step (`SimContext::Wind`).
- **A game's own air on top** (`IWindField`, given with `GameWorld::SetWindField()`): carries the
  particles, and is written into a coarse texture round each view's camera that grass and trees
  bend in (`nyx::windAt()` in `nyx.worldwind`), copied to the GPU only when it changes.
- **Clouds** take it at their altitude, faster high up by its profile (the high layer at its
  altitude, the volumetric clouds at the middle of their band), unless their *Own Wind* is on;
  old scenes, which have no wind of their own, keep their clouds' wind.
- Swaying materials keep only how they answer: `Materials/VertexColorWind.nyxmat` has *Response*
  (0 holds still), *Strength*, *Stiffness*, *Flutter*, *Flutter Frequency*, *Wind Height*.
- **Debug View**: arrows round the camera along the wind, as long as it is fast and warmer in a
  gust, and the crests of the gust fronts.
- Scenes saved with the environment's old particle *Wind* get it as a steady wind as they load.
- Tests: `NyxWindTests` (hand-made winds, the profile, fronts carried downwind and stronger in
  places, long winds, turning smoothly and the same whatever the steps, a driver ramping a game
  world's wind from calm to a storm, a game's air on a grid, the debug view, old scenes),
  `NyxWindGpuTests` (the GPU's numbers to the bit at 4096 points of four winds; the view's wind as
  materials read it, the editor's, a game's mid-turn, a game's local air near and away; grass bends
  in a gale and not at a *Response* of 0).

## Particles

A *Particle Emitter* gives off particles from its entity, such as sparks, embers, smoke or a fire's
flames: soft round quads facing the camera, or small meshes, glowing or lit, carried by the wind,
in the editor's scene views and in the game alike.

- *Emission*: *Emitting*, *Rate*, *Bursts* (many at once, once or again and again), *Lifetime*,
  *Spawn Radius*. *Motion*: *Direction* and *Cone Angle*, *Speed*, *Gravity*, *Buoyancy* (rising,
  as smoke does), *Drag* (how much the air carries them), *Spin*, *Local Space* (moving with the
  entity, or left where born). *Look*: *Shape* (Billboard or Mesh, a cube or the *Mesh* given),
  *Size*, *Size Over Life* and *Color Over Life* (a few keys each), *Blend* (Additive or Alpha),
  *Emissive Intensity* (above 1, they glow and bloom), *Lit* (by the sun, the sky and the point
  lights, as smoke by a fire), *Soft Distance* (fading where they meet the ground). *Budget*: *Max
  Particles*, *Seed*. All with undo, saved in scenes and sent to a running game.
- Carried by the world's wind ([Wind](#wind)) where each particle is, gusts and eddies included,
  and by a game's own air on top, which is asked where each particle is (`IWindField`): a game
  module's systems keep it, and Nyx's game hands it to the renderer (`GameWorld::SetWindField()`).
  The *Environment*'s *Particles*: *Max Particles*, the most in the world at once (50 000 without
  an environment). Scenes saved with the environment's old *Wind* get it as a steady wind.
- Only for the eye: nothing in the world feels them. They follow the world's time: in a game they
  stand still while it is paused and move on with Step; in the editor they play as you edit. The
  same seed plays the same particles; a new seed starts the emitter again.
- Hidden behind what is in front of them, fading softly where they meet it, seen through the fog.
  Alpha blended ones are drawn back to front; an emitter out of view isn't drawn, but moves on.
- **In the editor**: each emitter shows as a small box of lines, which a click selects, even with
  no particles; a selected emitter's particles' bounds are drawn round them. *Debug > Culling
  Bounds* shows them, and the lights' ranges.
- `Assets/Scenes/ParticlesTest.nyxscene`: the lights test scene with a fire: flames, embers and
  smoke drifting in the wind, a warm light at the flames.
- 10 emitters of 2 000 particles each take 1.2 ms of the CPU and 0.16 ms of the GPU in
  Release at 1280 × 720 ([Lights and particles](Design/lights-and-particles.md#numbers)).
- Tests: `NyxParticleTests` (the same particles from the same seed, others from another; a paused
  step changing nothing; lifetimes, the rate, bursts once and again; an emitter's budget and the
  world's; gravity, buoyancy, the drag to the wind, a game's air on top of it and alone, near the
  ground in a gusty wind; the entity's space; the keys;
  back to front; a world's emitters started, started again and forgotten; saved and loaded),
  `NyxParticleGpuTests` (one particle against the CPU's numbers: additive, alpha, its emissive
  intensity, lit by a red light or dark without one; hidden behind a box, soft where it meets the
  floor; paused, stepped, carried by a velocity field, culled out of view; its icon picked; a
  reference picture of sparks, smoke and embers; the test scene).

## Lightning

A *Lightning* component puts bolts of light between the sky and the ground over its entity, in the
editor's scene views and in the game alike: short, flickering strikes (*Mode* Strike), or a
sustained column, a braid of filaments twisting round one axis from the sky to the ground (*Mode*
Sustained). Without one, or off, the picture is as it was, to the bit.

- **When**: from *Start Time* (the world's time), again every *Interval* on average with *Repeat*,
  each start moved at random by *Interval Jitter*; a column stands for *Duration* (0: for as long
  as it is on). A game strikes now by setting *Start Time* to its world's time. *Seed*.
- **A strike** lands anywhere within *Strike Radius* of the entity, from *Height* up (moved by
  *Top Spread*): a faint leader comes down over *Leader Time* with its branches, then the return
  stroke flashes the channel at full brightness, then more strokes come down the same channel
  (*Min Strokes* to *Max Strokes*, *Stroke Interval* apart, each dying by *Stroke Decay*), then it
  glows on and fades over *Fade Time*: it flickers by itself.
- **A column** takes a new shape *Reform Rate* times a second (12: a drawn animation's "on twos", so
  it boils rather than flickers): *Min Filaments* to *Max Filaments* winding round the axis by
  *Twist*, within *Column Radius*, in big angular strokes (*Column Kinks*, *Column Jaggedness*) that
  meet at joins and split apart again (*Joins*), thick to thin along them (*Filament Width*, *Width
  Variation*), with hot blobs at the joins and a few kinks (*Knots*, *Knot Size*), their glow strong
  low down and patchy (*Glow Height*, *Glow Patchiness*); now and then a side arc whips outward (*Side Arc Rate*, *Side
  Arc Length*) or a lone bolt strikes beside it (*Stray Bolt Rate*, *Stray Bolt Distance*).
- **Shape**: paths split by midpoint displacement with forks: *Jaggedness*, *Detail*, *Branch
  Chance*, *Branch Length*, *Branch Depth*.
- **Look**: strips facing the camera, a narrow hot core, the only part past white (*Core Color*,
  *Brightness*, *Core Width*), a coloured body (*Body Color*, *Body Brightness*, *Width*) and a soft
  glow falling off to its edges (*Glow Color*, *Glow Strength*, *Glow Width*), the body's edge as
  hard as drawn by hand (*Sharpness*), round at the joins and ends, blooming; by default an orange
  body with red-orange edges; far bolts are kept at least *Min Pixel Width* pixels wide, so they don't
  flicker in and out; *Branch Brightness*, *Leader Brightness*. Hidden behind what is in front of
  it, through the fog and behind the clouds before it.
- **Light**: it lights the world round it through *Light Count* point lights up its lowest stretch
  (*Light Color*, *Light Intensity*, *Light Range*), the same lights every lit surface, decal and
  lit particle takes; none while no bolt is on.
- **The clouds lit by it**: the volumetric clouds round a bolt glow from within, as lit by a line of
  light along it (*Cloud Light Color*, *Cloud Light Intensity*), fading to nothing at *Cloud Light
  Reach*, dimmed by the cloud between, their far sides dark. Worked out every frame for every pixel
  near the bolt, apart from the clouds' own picture: whatever the clouds' *Update*, a flash leaves
  nothing behind, and the frame after it is the picture without it. 0.24 ms of GPU time at
  1920 × 1080 for a bolt whose reach covers half the clouds, about 1 ms over a whole dense storm
  (CSV `gpu_cloud_lightning_ms`). Its line goes into the cloud before it (*Cloud Hiding*), the
  cloud's glow taking over.
- `Assets/Scenes/LightningTest.nyxscene`: gentle hills at dusk under an overcast, a strike somewhere
  over them every few seconds and a sustained column off to one side, both lighting the clouds from
  within and parting them ([Lightning](Design/lightning.md#step-4-a-test-scene)).
- Kilometres off, past the camera's far plane, a bolt still shows against the sky.
- **Its channel**: a bolt parts the volumetric clouds where it comes out of them and piles them up
  round the hole, a column swirls them too, for *Channel Linger* after (*Channel*, *Channel Radius*,
  *Channel Clear*, *Channel Pile Up*, *Channel Swirl*, *Channel Open*), by the same shapes as a
  [Cloud Modifier](#cloud-modifiers)'s.
- Only for the eye, and worked out from its values and the world's time alone: the same seed and
  time give the same bolt to the bit; in a game it stands still while paused and moves on with
  Step; in the editor it plays as you edit.
- **In the editor**: each lightning entity shows as a small zig-zag of lines, which a click
  selects; a selected one shows where its strikes land (a circle on the ground) and a line up to
  where they start. *Debug > Culling Bounds* shows its box.
- At 1920 × 1080 its pass takes 0.03 ms of GPU time at a strike's flash and 0.05 ms on a column;
  the frame statistics show it (*lightning*, CSV `gpu_lightning_ms` and `lightning_bolts`)
  ([Lightning](Design/lightning.md#numbers)).
- All with undo, saved in scenes and sent to a running game.
- Tests: `NyxLightningTests` (the schedule, never overlapping, a day later as exact; a strike's
  leader, flashes, afterglow and branches; a column's reform frames, braid, joins, knots, its glow
  falling with height, fade, side arcs and stray bolts; paths' ends, band, branches, arrivals, budgets, the same seed the same path to
  the bit; the leader revealing them; strips facing the camera, round caps, mitres held, widened
  far off; the lights; a world's lightning; saved and loaded), `NyxLightningGpuTests` (unchanged to
  the bit off and between bolts; the channel near white where the CPU's path is, the bloom
  spreading it, the leader dimmer, hidden behind a wall; its lights lighting the floor; the same
  time the same picture, another seed another; a column boiling; its icon picked; its GPU time; under
  volumetric clouds, those round a column glowing and those far from it unchanged to the bit, the
  frame after it the picture without it to the bit in 4 x 4, 4 x 4 the same as every pixel marched
  while it shines, the column past the far plane shown below the cloud's base; the clouds' lightning
  pass's GPU time beside the march's; the test scene loaded, a flash brightening its clouds; and
  again with MSAA).

## Anti-aliasing

The edges of what the scene views draw are smoothed by multisample anti-aliasing: a stalk, a wire
or a branch thinner than a pixel in the distance shows as a fainter line instead of breaking up
into dots that crawl as the camera moves, and edges are no longer staircases.

- 4 samples a pixel by default; 1 (off), 2, 4 or 8 in *Window > Preferences > Anti-aliasing*,
  changed at once in every scene view, saved with the editor's preferences and given to the games
  Play starts. More than the GPU can do is the most it can.
- The game takes `--msaa <samples>` on its command line; F7 in every build but Release cycles 1, 2, 4, 8.
  A game sets it with `IRenderer::SetMsaaSamples()`.
- The sky, decals, fog, point lights, ambient occlusion, bloom, instanced meshes, two-sided
  materials, materials that cast no shadows and vertex functions all work with it; particles are
  drawn after it, soft against the smoothed scene; picking, the selection outline and the lights'
  and emitters' icons stay exact, a pixel each.
- What it costs, and why it isn't temporal anti-aliasing: [Anti-aliasing](Design/anti-aliasing.md).
- Tests: `NyxMultisamplingGpuTests` (thin bars with nothing between the sky and a bar at one sample,
  their edges in as many shades as the samples allow at 2, 4 and 8; the count clamped to the
  device's; changed live, views made again, the same picture to the bit when changed back; a view
  made later drawing with it; picking the same at every count; the targets' memory),
  `NyxAmbientOcclusionGpuTests`, `NyxBloomGpuTests`, `NyxSceneGpuTests`, `NyxShadowGpuTests`,
  `NyxPointLightGpuTests` and `NyxParticleGpuTests` run again at 4 samples (decals, picking, point
  lights, soft particles, materials that cast no shadows and the reference pictures, matched away
  from edges), the reference pictures unchanged at one sample.

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
  are around all of them. The details panel shows *Mesh Instances* greyed in its *Runtime* section:
  how many, in how many cells, the revision.
- **Culling**: every scene view, the editor's and the game's alike, leaves out what is outside its
  camera's frustum: an entity by its bounds where it is drawn, an instanced one cell by cell (a
  cell's bounds hold the mesh at each of its instances, even when the cell's own bounds are too
  small). Cells in view that follow on in the transforms are one draw.
- **Batching**: entities drawn once that draw alike, the same mesh with the same material in each
  section (many trees on one cached mesh, say), are drawn in one draw.
- **Seeing it**: *Debug > Culling Bounds* in the editor (F4 in the game, every build but Release) draws the
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

## Materials' own numbers

A game can hand its materials numbers of its own, for a sway, a growth, a tint for each copy
([Design: Vertex data](Design/vertex-data.md)).

- **For each vertex**: four numbers in a mesh made at run time (`Vertex::Custom`), such as how far
  each vertex bends in the wind or the ages at which it appears and is fully grown. Zero in every
  mesh Nyx makes or imports. On the GPU in half precision: about three digits, 0, 1 and small whole
  numbers exactly.
- **For each copy**: four numbers beside each transform of a `MeshInstancesComponent`
  (`MeshInstances::Custom`); a *Scatter* gives every copy a number of its own from 0 up to 1 in x.
  An entity drawn once has its own, *Custom Data* on its *Mesh Renderer*, which a system can change
  every frame for nothing more than its transform costs; the material gets it as an instance's, so
  one material serves a scatter's copies and a single entity alike.
- Every surface material reads them (`SurfaceInput.Custom`, `SurfaceInput.InstanceCustom`), and
  the time, `View.Time`, which stands still while the game is paused.
- **A vertex function**: a material can move its vertices (`ISurfaceVertex` in
  `nyx/surface.slang`), in the mesh's space or in the world, by its values, these numbers and the
  time; a project's material as well as the engine's. The mesh moves alike in the scene, its
  shadow, and the editor's picking and selection outline. The material's *Max Vertex Offset*
  (under *Material* in the material editor) says how far it moves a vertex at most, and what is
  culled is judged by bounds that much larger.
- **Wind**: `Materials/VertexColorWind.nyxmat` (and `VertexColorWindTwoSided.nyxmat` for single
  sheets) draws vertex colours that sway in the world's wind ([Wind](#wind)): each vertex as far
  as its custom x says (0 at a foot, 1 at a tip), each copy at its own phase, gust fronts crossing
  the ground. Its *Wind* values are only how it answers: response (0 holds still), strength,
  stiffness, flutter and its frequency, the height it feels the wind at.
  The built-in *Crossed Quads* mesh (`Meshes/CrossedQuads.nyxmesh`) carries the weights; the
  ground test scene has a scatter of them swaying round the flat place.
- Tests: `NyxMeshTests` (the vertex as the GPU reads it, the channels back from half precision,
  every attribute where it belongs; zero in every mesh Nyx makes or imports), `NyxScatterTests`
  (every copy's number from 0 up to 1, the same every time and cell by cell, spread evenly),
  `NyxVertexDataGpuTests` (a mesh's channels, each instance's and an entity's reach the material, an
  entity's changed every frame without an upload, two entities in one draw each with its own; the
  time stands still while paused and moves on by a step; a mesh moved by its material's vertex
  function drawn, shadowed, picked and outlined where an unmoved one put there is; bounds widened
  so a mesh moved into view isn't culled, and culled without; the function gets each number, in
  picking too; it stands still while paused), `NyxShaderCompilerTests` (a material with a vertex
  function gets its two entry points, valid, reading the numbers it uses; one without gets
  neither), `NyxMaterialTests` (the option read and inherited), `NyxMeshTests` (the crossed
  quads), `NyxGroundTests` (the test scene's grass).

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
  ground, crates on the flat place and up the valley's side, the character and an orbit camera,
  and grass round the flat place: crossed quads scattered over the ground, swaying in the wind
  ([Materials' own numbers](#materials-own-numbers)).
- Tests: `NyxGroundTests` (the same heights every time, to the bit and as when written; smooth;
  the mesh valid, sized as asked, at the heights, its normals up; steep ground stone, low flat
  ground grass; the post-load step makes the mesh, nothing again for the same values, a new
  revision for new ones, the collider follows, removing takes the mesh away, a loaded scene has
  it; a character stands on it and a crate slides down the valley's side; the sample scene),
  `NyxEditorPanelTests` (added, edited, undone, removed and undone again in the editor).

## SDF terrain

An *SDF Terrain* is terrain as a signed field over a volume rather than a height at each point, so
it can overhang, hollow out and float. It is drawn in chunks round the camera, in the editor and the
game, at one level of detail; nothing collides with it yet.

- Add an *SDF Terrain* and a *Mesh Renderer* with `Materials/VertexColor.nyxmat` (its *Mesh* left
  empty). Its chunks, 32 m cubes of 1 m cells, are made round every scene view's camera in the
  editor and round the primary camera in the game, within *View Distance*, on the job system's
  workers, nearest first; chunks the surface doesn't cross (all air or all solid) are never made.
  The first view of a 128 m terrain takes about a tenth of a second on 19 workers; once made, an
  update costs about 0.03 ms a frame. They are drawn by the entity's Mesh Renderer, one object
  each, culled, shadowed and picked as one entity. When a value changes, every chunk is made again,
  each drawn as it was until its new mesh is in; chunks a chunk past the view distance go. Never
  saved.
- *Chunks*: *View Distance*, *Mesher* (*Dual*, with sharp edges for the *Sharp Materials*, or
  *Marching Cubes*), *Sharp Materials* (material numbers), *Material Colours* (each material
  number's vertex colour).
- The editor's details panel shows *Terrain Chunks* in its *Runtime* section: chunks drawn, wanted
  and being made, skipped, triangles, and the last chunk's times.
- `Assets/Scenes/SdfTerrainTest.nyxscene` (written by `NyxTerrainChunksTests --write-scene`):
  terraced hills with tunnels under them, a floating landmass of sharp stone with a hanging
  underside and chambers inside, a sun and a camera that flies.

- *Layers*, evaluated in order, each joined to what the ones before made by *Combine*: *Add* (solid
  where it is), *Carve* (cut out of what came before) or *Intersect*, rounded over *Blend Radius*.
  Each gives the surfaces it makes its *Material* number; carving keeps what it cuts.
  - *Heightfield*: ground at *Height*, lifted and lowered by *Amplitude* metres of noise, with
    *Terraces*.
  - *Landmass*: a floating mass round *Center*: a top at *Height* (roughened by *Amplitude*, with
    terraces), an underside *Depth* below it hanging another *Underside Depth* in ridges, an edge at
    *Radius* going in and out by *Edge Roughness*, its rim rounded over *Rounding*.
  - *Tunnels*: about *Radius* wide, winding where two noises are both near 0; *Chambers*: open where
    a noise is high, *Openness* of the band; both between *Height* and *Depth* below it.
  - *Detail*: every surface before it pushed in and out by *Amplitude*.
  - *Noise*: *Seed* (added to the terrain's *Seed*), *Octaves*, *Frequency*, *Lacunarity*, *Gain*,
    *Ridged* for sharp crests.
- The field (`SdfTerrain.h`): `SampleTerrain()` gives the value at a point (negative inside, 0 on the
  surface) and its material, `SampleTerrainBatch()` many points, the same bits, with the 3D noise
  eight points at a time. `GetTerrainMaxSlope()` is a proven bound on how fast it changes,
  `GetTerrainValueRange()` what it can be over a box (all air, all solid, or neither), and
  `GetTerrainNormal()` and `RayCastTerrain()` the way out and where a ray meets the surface. The same
  on every machine.
- Its meshes (`FieldMesher.h`): `SampleTerrainGrid()` samples it on a grid, `MakeFieldMesh()` makes
  the surface of any sampled field by dual meshing: a vertex in each cell the surface crosses (one for
  each separate piece of it there), a quad for each grid edge it crosses. A vertex of a material
  marked sharp goes where its crossings' planes meet, so cliffs and cut faces keep their edges and
  corners, the others at the middle of their crossings; given the field itself (the settings'
  *Field*), the crossings are found on it and the planes come from its gradient, which keeps a box's
  corners to a few millimetres. A sharp vertex whose quads would face the wrong way goes back to the
  middle. Each vertex has the field's normal, how much of the sky it sees (rays through the field,
  past the grid where the field is given) and its material, in its custom numbers. Chunks side by
  side meet without a gap: each makes the quads of its own edges, from a grid two samples wider
  before it and one after. Where a cell face has all four edges crossed and both cells join its two
  curves, four triangles share an edge: dual meshing's known limit.
- `MakeMarchingFieldMesh()` makes the same surface by marching cubes, to compare the two: a vertex on
  each crossed grid edge, shared by the cells round it, and in each cell polygons through them, the
  loops its crossed edges make joined through its faces by the same rule, fanned into triangles so
  that no diagonal lies on a face of the cell. Closed and manifold everywhere, about as many
  triangles as the dual mesh, but every vertex smooth: no sharp edges or corners.
- Its 3D noise (`GradientNoise.h`): `SampleGradientNoise3D()`, fractal and ridged octaves, their
  batches, and proven bounds of value and slope; `SmoothMinimum.h`, the rounded minimum and
  maximum.
- Tests: `NyxGradientNoiseTests` (the noise within its bounds, batches the same bits as single
  points, golden values the same in Debug and Release), `NyxSdfTerrainTests` (flat ground exact,
  batches the same bits, golden values, never steeper than the bound, box ranges and rays against
  dense sampling, a thin slab never passed through, materials, blends, tunnels in their band, seeds),
  `NyxTerrainChunksTests` (chunks made on the workers the same bits as made directly, only where the
  surface is, within the view distance, let go of and made anew as the camera moves, made again
  for new values while the old stay, gone with the terrain; the sample scene),
  `NyxTerrainChunksGpuTests` (the chunks drawn, picked as the terrain, drawn where it is moved),
  `NyxFieldMesherTests` (a sphere closed with the right area, a box's corners kept sharp and rounded
  smooth, thin shapes closed and wound consistently, neighbouring chunks meeting, sky visibility,
  materials, a golden mesh the same in Debug and Release; marching cubes on the same shapes, closed
  and manifold; how long each takes on a chunk of terrain).

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

## Shapes drawn by hand

Which shape a player drew, for a game that lets them draw: `PointCloudRecognizer`, in `Core`
([Stroke recognition](Design/stroke-recognition.md)).

- Templates are named examples of shapes, several per name, each some strokes of points, +Y up:
  added in code (`AddTemplate()`), or read from a game's text file of `template` and `stroke` lines
  (`LoadStrokeTemplates()`; `WriteStrokeTemplates()` writes them, e.g. ones a player recorded).
- `Recognize()` gives the closest template, its score from 0 to 1 and the closest of another name,
  whatever the order and direction the strokes were drawn in, how many there were, how large and
  where. What scores high enough is the game's to say.
- By default a shape turned is the same shape, and the match says which way the template's up lay
  in the drawing, e.g. where an arrow points. Rotation sensitive, the strokes are matched with an
  up the game gives, such as the view's, and an arrow up and one to the right are two shapes.
- The same templates and strokes give the same result on every machine running the same build.
  50 templates take about half a millisecond, 0.12 ms when rotation sensitive (Release).
- Tests: `NyxPointCloudRecognizerTests` (circles, triangles, arrows and zigzags drawn again shakier,
  larger, elsewhere, in other stroke orders and directions and turned, recognised, with a runner-up
  of another name; where a turned arrow points; scribbles below 0.75; arrows told apart by the up
  given; the same results to the bit; the text read, written and read back exactly, its problems
  with their lines; 50 templates under 1 ms in Release).

## Fields over the world

A number or a vector in every cell of a grid over the world, for a game's own meanings (a
temperature, a concentration, a wind): `SparseField<float>` and `SparseField<glm::vec3>`, in `Sim`
([Sparse fields](Design/sparse-fields.md)).

- Cells (0.5 m by default) in bricks of 8 x 8 x 8, made where the game asks (`Allocate()`) and
  freed once they settle back (`FreeSettled()`); everywhere else the field is its ambient value.
- Sources spread an amount over a box's cells by a weight of the game's; diffusion, stable at any
  step; decay towards the ambient; advection along a velocity field (semi-Lagrangian), the field's
  own or another's; trilinear sampling anywhere.
- At most `MaxBricks` bricks (64 by default), so a step has a bound: 0.5 to 0.7 ms at the cap in
  Release. At the cap a new brick takes the place of the one nearest the ambient that wasn't asked
  for this step; with all asked for, it is refused and the game is told.
- The same steps give the same values to the bit on every machine running the same build; bricks
  are walked in the order of their coordinates, and neighbours read from before the step.
- Tests: `NyxSparseFieldTests` (a source in still air spreading into a bump the same mirrored and
  turned, decaying to the ambient, its bricks then freed; diffusion at 13 times the explicit limit
  never overshooting; a wind, as the ambient or on bricks, carrying the bump 4 m downwind in 2 s,
  keeping its mass; the wind carrying itself; the cap of 8 holding, the hot brick kept over the
  settled ones, new ones refused once all are asked for; the ambient outside the bricks and
  blended in at their sides; 600 steps at the cap the same to the bit, and under it the same with
  boxes asked for in another order; the cost of a step at the cap).
