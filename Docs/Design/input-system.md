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

## Who uses it

`Application::GetInput()` gives it to the layers; `Application` owns the SDL backend.
`FlyCameraInput` gives a fly camera its actions (Move, MoveVertical, Look, Speed, LookButton) and
two contexts, for flying and for looking with the mouse (cursor captured), with keyboard, mouse
and pad. The editor's fly camera (`EditorCameraController`) uses it with the contexts
`EditorFlyCamera` (100) and `EditorMouseLook` (110); the game flies its primary camera with it
(`GameFlyCamera`, contexts `FlyCamera` and `FlyCameraMouseLook`), and leaves the camera alone
without input. The editor's UI source
inspector turns on a `SourceInspector` context (950) that takes the keyboard and the mouse. The
game's Ctrl+Alt+Shift+C (crash on purpose) and F3 (the physics debug view) are bindings in a
`Debug` context (800, Debug builds).
ImGui gets its input from GLFW's callbacks, which it shares with the GLFW backend; the editor's
shortcuts stay ImGui's.

`NyxInputTests` drives the system with virtual devices: frames, taps shorter than a frame,
devices coming and going, focus and the cursor. `NyxInputRouterTests` checks the dead zones,
curves and pipeline steps against known values, each trigger's phases over set times, taking
between contexts, the cursor, hysteresis and the device used last, all on traces.
`NyxInputGamepadTests` drives virtual pads: hot-plug, analog values, focus, swizzles, composites
and output. No test machine has pads: `NyxPadCheck.exe` lists the pads, prints what they report
and tries their output, for checking them by hand.

## Not built yet

A window that shows the input and its traces.
