# Input system

How keyboard, mouse and gamepad input reaches the editor and the game, and why. The code is in
`Engine/Source/Runtime/Input`.

## The shape of it

- **Backends** (`IInputBackend`) collect what happens and hand it over once per frame as
  events: devices added and removed, controls changing, focus changing. `GlfwInputBackend` reads
  the keyboard and mouse, `SdlGamepadBackend` the gamepads; `VirtualInputBackend` stands in for
  them in tests.
- **`InputSystem`** takes every backend's events, in the order they happened, into one
  `InputFrame` per frame (`BeginFrame()`), and at the end of the frame makes the cursor what the
  context stack says (`EndFrame()`). The application owns it and runs it around the layers.
- **`InputFrame`** is the state of every device's controls in that frame, plus the events it was
  made from.
- **The router** (`InputUser`) goes down the user's stack of *contexts* each frame and decides
  every *action* from the frame: what each binding's control gives, through the value pipeline
  and the trigger. It writes down every decision as a trace.

## Why it works this way

- **Frames, not callbacks.** Code reads the frame wherever it runs, so input is the same for
  every layer in a frame.
- **No presses lost between frames.** A frame counts how often each button went down and up. A
  tap shorter than a frame counts as one press and one release, while the button ends the frame
  up.
- **Real time, not frame time.** Each event carries its time (`NowUs()`), and a button remembers
  when it last went down, so a hold lasts as long at 30 fps as at 240.
- **Keys by position, not by letter.** Keys are named after their place on a US keyboard:
  `Keyboard/Z` is the key right of the left Shift on any layout, so WASD sits under the left hand
  on QWERTZ and AZERTY too.
- **Controls by name.** A control has a path (`Keyboard/W`, `Mouse/Delta`), for data, logs and
  debugging. Relative controls, such as mouse movement and the wheel, are summed over the frame.
- **Devices come and go.** A device keeps its id while it is connected; one that comes back gets
  a new one. A device removed in a frame releases its held buttons in that frame.
- **Focus.** Without the focus, no keyboard or mouse button is down, so a key held while
  switching windows doesn't stick. Pads still report; the `Unfocused` context takes them.
- **One router.** The context stack is the only thing that decides who gets a control. Window
  focus and ImGui are contexts on it too, with a priority, not flags somewhere else.
- **The cursor follows from the stack.** A context can say the cursor is free, hidden or
  captured; the topmost one that says anything decides. Nothing sets the cursor from code, so it
  can't stay captured because something forgot to give it back.
- **The trace is the truth.** The router records, per binding, what it got, what each step made
  of it and where it stopped and why. Whatever shows input only reads these records.
- **Every window counts.** The GLFW backend reads the main window and every OS window ImGui makes
  for floating windows, so a scene view dragged out of the editor gets its keys too.

## Contexts and actions

- **Actions** (`InputActionDef`): a name and a value type, Button or an axis of one to three
  dimensions. Code holds a handle and reads the action's `ActionState`: its value and phase
  (Started, Ongoing, Triggered, Completed, Canceled). Only polling, no callbacks;
  `ConsumeTriggered()` reads a press once.
- **Contexts** (`InputContextDef`): a named set of bindings with a priority, on or off as a whole
  (`SetContextActive()`, with a reason). The highest context that binds a control takes it from
  those below, unless the binding lets it through. Of equal priority, the one turned on later
  goes first. A context can also take whole devices, and say what the cursor is.
- **Bindings** (`InputBinding`): a control, or keys making up an axis, with modifier keys, a
  value pipeline and a trigger.
- **The value pipeline** (`ValuePipeline`) has a fixed order: dead zone (Axial, Radial, Scaled
  radial, Hybrid), response curve, sensitivity, look acceleration, smoothing, delta time. Each
  step can be off, none can move, so the trace always has the same columns.
- **Triggers** (`TriggerSettings`): Down, Pressed, Released, Toggle, Hold, Tap, DoubleTap, Chord,
  Repeat, with a buffer, and a press and a release threshold for analog controls (hysteresis).
- **System contexts**: `Unfocused` (1000) while none of the program's windows has the focus,
  taking everything; `ImGui` (900) while ImGui has a text field active (the keyboard) or the
  mouse is over UI that isn't a view (the mouse). Views, such as the scene views and the game's
  window, say so with `MarkAsView()`. ImGui's state of a frame decides the `ImGui` context of the
  next one (`SetImGuiState(ReadImGuiState())`).
- The user also keeps the device used last, for prompts: a nudged mouse doesn't count until it
  has moved a few pixels.

## Gamepads

- **SDL3, its gamepad parts only** (`Dependencies/SDL`, built static without video, audio or
  rendering): Xbox pads, DualShock 4, DualSense and what else SDL knows. GLFW keeps the window.
  SDL runs without windows of its own, so it reports while another program has the focus
  (`SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS`); Nyx decides about focus itself.
- Buttons come as events, so a tap within a frame isn't lost; sticks and triggers as values, with
  y up. Face buttons are named by position (`Gamepad/FaceSouth` is A on an Xbox pad and Cross on a
  DualSense). Gyro, accelerometer and two touchpad fingers where a pad has them.
- A pad's model, connection, battery, capabilities and a stable key come with the device, and
  change with `DeviceChanged` events. Of several pads, the stick pushed furthest counts; a removed
  pad's sticks and triggers drop to 0.
- **Output**: rumble, trigger rumble, DualSense trigger effects (Off, Resistance, Weapon,
  Vibration), light colour and player LEDs, asked for on the user and merged per frame
  (`InputOutput`). `EndFrame()` sends each pad what it can do; what wasn't sent is recorded with
  the reason. The DualSense's trigger effects are bytes SDL passes through as they are.
- Bindings take a swizzle (a stick's up as forward), and composites take analog controls
  (triggers as up and down). Sticks and triggers count as the device used last only past 0.3.

## Input as data

Actions, contexts and bindings live in an input asset, `Input/Default.nyxinput`, found like any
asset (`Paths::FindAsset()`: the project's first, then the engine's). It is saved through
reflection like scenes are, with controls by path and actions by name, so the input's settings
are reflected types (`NYX_REFLECT`, `NYX_ENUM`) and the header tool also reads
`Runtime/Input/include`.

- The editor writes the asset from the built-in defaults when there is none; the editor and the
  game load it at start, and code finds its contexts by name. `FlyCameraInput` adds its defaults
  only for what the asset doesn't have.
- **Tuning comes in layers**, the later one wins: the asset, a pad model's profile, and the
  player's own settings (`%LOCALAPPDATA%\Nyx\PlayerInput.nyxinput`, read by the game only). An
  override sets one value by its path, such as `Pipeline.DeadZone.Inner`, for the bindings it
  matches. `FindTuning()` says which layer a value comes from.
- Each context gets a variant of its bindings per profiled model; while a pad of that model drives
  a binding, the router uses the variant's pipeline, thresholds and trigger.
- Applying an asset (`ApplyInputAsset()`) keeps the handles code holds and whether each context is
  on. A binding that can't be used is left out and reported once, with where it is.
- `NyxDump` prints `.nyxinput` files (`PrintInputFile()`, in `LinkTools`).

## Finding the right setup

- **Editing with undo.** The editor holds the asset as a document (`InputAssetDocument`, in the
  editor's `Input` folder) in its undo, as `EObjectDomain::InputAsset`. Every edit, undo, redo and
  drag applies the asset again, so it is felt at once; a held button keeps its state, since
  `ReplaceContext()` keeps what bindings remember when only their tuning changed. The details
  panel edits arrays for it: elements in place, added, removed and moved, each one undo step.
- **Checks** (`InputChecks`) find what is easy to get wrong and hard to feel: two actions on one
  control in one context, a binding a higher context always takes, an action no keyboard and mouse
  or no pad can do (Coverage), Down on an action the code consumes once, delta time on mouse
  movement or none on a stick that turns, a composite not normalized, dead zones that don't fit,
  a hold shorter than the tap beside it, smoothing's latency, a threshold without hysteresis, a
  control only some pads have. Each says where it is; the obvious ones carry a fix. Loading the
  asset logs what they find.
- **Recipes** (`InputRecipes`): *Add Action* asks what the action is for (once per press, while
  held, charge and release, tap or hold, move, look, an analog amount, stepping through a menu)
  and writes plain bindings that pass every check, edited like any others afterwards.
- **What a value means**: `ExplainBinding()` says a binding's values in units (percent of a stick's
  travel, degrees per second, frames at 60 fps); the Tuning tab draws the response curve with the
  stick on it and the trigger as a timeline with the last press.

## Running games

- **Live tuning.** The editor sends the whole asset to every running game whenever it changes,
  every frame of a drag included (`SetInputAsset`, `LiveInput`). A game puts it into its input
  with its player's settings over it; held buttons stay held, as when the editor applies it. A
  game started while the asset has unsaved changes gets them once linked.
- **Pads in the background.** `--input-background` (and `SetInputBackground` for a running game)
  lets the pads drive a game whose window doesn't have the focus: `Unfocused` then takes only the
  keyboard and the mouse (`SetGamepadsInBackground()`). *Gamepads Drive All Games* in Play's menu
  starts the games so and switches the running ones.
- **The Input window shows a running game.** While a game is shown, it streams its input
  (`InputStreamWriter`, a frame observer of its input system): first a snapshot of its devices,
  what they hold and its stack, then each frame's events, frame time, ImGui's state, its stack when
  it changed, and its actions and contexts when they changed. The editor replays the stream in a
  replica of the game's input system (`InputReplica`, over a virtual backend), which decides every
  frame as the game did: traces, phases, the stack and the timeline are the game's, not worked out
  again. The stream can start at any time, and only the game shown streams.

## Comparing A against B

- *Keep as A* in the Tuning tab remembers the tuning; edits after it are B. *Showing A/B* swaps
  them in the editor and in every running game (they get the asset shown,
  `InputAssetDocument::GetAppliedAsset()`), so the difference is felt in a second; so does holding
  a pad's Back and pressing its top face button while playing, read from the devices whatever the
  contexts do. An edit shows B again.
- A recording replayed *Through A and B* runs twice in step, once through each tuning
  (`InputPlayback::SetTuning()`), and the Timeline draws both: the same hands, two tunings.

## Recording

- A recording (`.nyxinputrec`, `InputRecorder`) is an input system's stream in a file: the
  definitions, then frame by frame the events, ImGui's state and the stack when it changed. Each
  frame is flushed, so after a crash a recording is whole up to its last frame, and the first
  frame is a snapshot, so a recording can start at any time. Several frame observers can watch one
  input system, a recording and the editor link's stream at once.
- *Record* in the Input window records the input shown, the editor's or a running game's (which
  then starts its stream again, so the file starts with a snapshot). `NyxGame.exe --input-record`
  records a game from its start (`InputRecordingLayer`). `NyxDump` prints recordings
  (`PrintInputRecording()`, in `LinkTools`).
- *Replay...* opens a recording as the Input window's source (`InputPlayback`, a replica): at the
  recorded pace, paused, a frame at a time, or moved to any frame by replaying from the start.
- `NyxGame.exe --input-replay` plays a recording into the game's own input instead of its devices
  (`InputSystem::StartReplay()`): the recorded times, events, focus and ImGui state go through the
  game's own actions and contexts, so a replay can try other tuning. While it plays, the devices
  are ignored and what they held lets go; the recorded devices get ids of their own and go at the
  end, when the devices count again.

## Seeing it

*Window > Input* in the editor, and in Debug builds the same over a game (Ctrl+Alt+Shift+I, a
binding in the game's `Debug` context), answer "why did or didn't this happen?" from the traces.
The view is in the engine (`InputDebugView`, painted with the UI's widgets), so both show the
same:

- `InputHistory` keeps the last 300 frames (actions, traces, the stack) and the last 2000 events
  and outputs, with pause; the editor records it every frame, open or not.
- `InputInspection` answers questions about a stack without changing it: who gets a control
  (every binding that sees it, down to the context that takes it), every binding of a control,
  a made-up stack for *What if*, and stick calibration (drift over 3 s, range over 5 s), which
  suggests dead zones without applying them.
- The tabs: Devices (the pad painted live, sticks with their dead zones, the stick plot,
  calibration, trying the pads' output), Actions (value, phase, source, and the selected one's
  traces), Contexts (the stack and who decides the cursor), Control map (who gets each control,
  now or with other contexts, and *Press to find*), Events (with pause, filter and copy), Timeline
  (chosen actions with each frame's traces).

## Who uses it

`Application::GetInput()` gives it to the layers; `Application` owns the SDL backend.
`FlyCameraInput` gives a fly camera its actions (Move, MoveVertical, Look, Speed, LookButton) and
two contexts, for flying and for looking with the mouse (cursor captured), with keyboard, mouse
and pad. The editor's fly camera (`EditorCameraController`) uses it with the contexts
`EditorFlyCamera` (100) and `EditorMouseLook` (110). In the game the fly camera is opt-in per
scene: `UpdateFlyCameras()`, which the game layer calls each frame, flies every entity with a
`FlyCameraComponent` (its speed and look sensitivity, saved with the scene; what the wheel and the
look button made of them while the game runs) with the contexts `FlyCamera` and
`FlyCameraMouseLook`. The contexts are on only while the world has such an entity, so a scene
without one leaves the keys and the pads to the game's own camera. Without input it leaves the
entities alone. The editor's UI source
inspector turns on a `SourceInspector` context (950) that takes the keyboard and the mouse. The
game's Ctrl+Alt+Shift+C (crash on purpose) and F3 (the physics debug view) are bindings in a
`Debug` context (800, Debug builds).

`OrbitCameraInput` gives the game's orbit cameras (`OrbitCameraComponent`) their actions
(OrbitLook, OrbitZoom, OrbitLookButton) and three contexts, in the default asset with the fly
camera's:
- `OrbitCamera` (100): OrbitLook on the right stick (as the fly camera's: dead zone, curve, rate,
  look acceleration), OrbitZoom on the wheel, OrbitLookButton on the right mouse button;
- `OrbitCameraMouseLook` (110, cursor captured): OrbitLook on the mouse;
- `OrbitCameraFreeLook` (105, no bindings): a request.

`Input::UpdateOrbitCameras()`, which the game layer calls each frame before the fly cameras and the
player, adds the look and zoom to every orbit camera's `LookDelta` and `ZoomDelta` (the camera
uses them up after the frame's steps, `Nyx::UpdateOrbitCameras()`). `OrbitCamera` is on only while
the world has an orbit camera. `OrbitCameraMouseLook` is on while the request is on (free look:
the mouse turns the camera without a button, the cursor captured), or else while the right mouse
button is held after a click (hold to look: the cursor is free to point with otherwise). Nothing in
the engine turns the request on: a game's player does, with its user, e.g. while it explores and
off while it points at surfaces:

```cpp
const ContextHandle freeLook = frame.Input.FindContext("OrbitCameraFreeLook");
frame.Input.GetUser().SetContextActive(freeLook, bExploring, { .Reason = "Exploring" });
```

So the mode is input state, not the world's: the player switches it without writing a component,
and it counts from the next frame, as all context changes do. The right stick turns the camera in
both modes. While the world has an orbit camera, the fly camera's contexts are off.

In Debug builds the game also has a stand-in character driver, for trying characters without a
game module's player (`IPlayerController`), which would send a command, e.g. `MoveCharacter`, for a
system to apply in the step. It is the one code that writes a character's simulation input from
outside a step. While the world has a `CharacterComponent` and an `OrbitCameraComponent`, the
context `DebugCharacter` (100) is on, with the actions CharacterWalk and CharacterJump; the game
layer writes them into the character's `DesiredMove` (through `GetGroundMove()` and the camera's
yaw) and `bJumpRequested`.
ImGui gets its input from GLFW's callbacks, which it shares with the GLFW backend; the editor's
shortcuts stay ImGui's.

`NyxInputTests` drives the system with virtual devices: frames, taps shorter than a frame,
devices coming and going, focus and the cursor. `NyxInputRouterTests` checks the dead zones,
curves and pipeline steps against known values, each trigger's phases over set times, taking
between contexts, the cursor, hysteresis and the device used last, all on traces.
`NyxInputAssetTests` checks the asset's round trip and printout, the problems it reports, the
layers and overrides, and that applying keeps handles and on/off.
`NyxInputChecksTests` runs each check with and without its mistake, every recipe, the
explanations and the curve; `NyxEditorInputTuningTests` edits inside the asset's arrays with undo
and redo, and draws the Tuning and Checks tabs headless.
`NyxLiveInputTests` has both ends in one program: the asset from the editor while a button is
held, a binding the game can't use, damaged messages, and the pad without the focus.
`NyxInputStreamTests` streams presses, holds, taps, the wheel, mouse look, ImGui, focus, pads in
the background, contexts switched by code, tuning changed mid-hold and a pad unplugged, and
checks that the replica decides every frame as the original did.
`NyxInputRecordingTests` records presses, holds, a tap, sticks, mouse look and the focus, and
replays the file with the same phases, firings and values every frame; a file cut off mid-frame,
the printout, two watchers at once.
`NyxFlyCameraTests` checks that only entities with a `FlyCameraComponent` fly, that the fly
camera's contexts are on only while one exists, and that the component is saved with the scene.
`NyxOrbitCameraInputTests` checks that the orbit camera's contexts are on only while one exists,
that the mouse turns it only while the right button is held, with the cursor free, until free look
is asked for, which turns it without a button and captures the cursor, that the right stick turns
it in both modes and the wheel zooms, and that the default asset has its actions and contexts, a
tuned sensitivity counting.
`NyxOrbitCameraTests` checks that the orbit camera starts from where the camera looks, keeps its
distance and offset from a moving target, follows it where it is drawn, smooths alike at 30 and 240
frames a second, clamps its pitch and zoom, wraps its yaw, leaves the camera alone without a target,
and that `GetGroundMove()` walks where the camera looks.
`NyxInputInspectionTests` checks who gets a control, What if stacks and the calibration;
`NyxEditorPanelTests` that the Input window leaves no shapes to ImGui.
`NyxInputGamepadTests` drives virtual pads: hot-plug, analog values, focus, swizzles, composites
and output. No test machine has pads: `NyxPadCheck.exe` lists the pads, prints what they report
and tries their output, for checking them by hand.

## Not built yet

Rebinding in a player settings screen, button prompts for the device used last, local multiplayer,
gamepad navigation for in-game UI (as a context), and keeping the full traces only while someone
looks.
