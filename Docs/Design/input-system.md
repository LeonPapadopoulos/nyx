# Input system

How keyboard and mouse input reaches the editor and the game, and why. The code is in
`Engine/Source/Runtime/Input`.

## The shape of it

- **Backends** (`IInputBackend`) collect what happens and hand it over once per frame as
  events: devices added and removed, controls changing, focus changing. `GlfwInputBackend` reads
  the keyboard and mouse; `VirtualInputBackend` stands in for them in tests.
- **`InputSystem`** takes every backend's events, in the order they happened, into one
  `InputFrame` per frame (`BeginFrame()`), and at the end of the frame makes the cursor what the
  frame asked for (`EndFrame()`). The application owns it and runs it around the layers.
- **`InputFrame`** is the state of every device's controls in that frame, plus the events it was
  made from.

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
- **Focus.** Without the focus, no button is down, so a key held while switching windows doesn't
  stick.
- **The cursor is asked for, each frame.** Code asks for the cursor free, hidden or captured for
  this frame only, and asks again while it should stay so; the strongest request wins. A cursor
  can't stay captured because something forgot to give it back.
- **Every window counts.** The GLFW backend reads the main window and every OS window ImGui makes
  for floating windows, so a scene view dragged out of the editor gets its keys too.

## Who uses it

`Application::GetInput()` gives it to the layers. The editor's fly camera
(`EditorCameraController`) reads the frame and captures the cursor while it flies. ImGui gets
its input from GLFW's callbacks, which it shares with the GLFW backend.

`NyxInputTests` drives the system with virtual devices: frames, taps shorter than a frame,
devices coming and going, focus and the cursor.

## Not built yet

Gamepads, and a layer of actions (Move, Look, Jump) bound to controls, so that a game never
reads keys or buttons directly.
