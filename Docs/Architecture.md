# Nyx architecture

How the code is put together today. Read this first; [Features.md](Features.md) says what the
editor and the game do, and the [README](../README.md) how to build them.

## Programs and how they talk

| Program | What it is |
| --- | --- |
| `NyxEditor.exe` | The editor. It owns the world you edit. |
| `NyxGame.exe` | Runs one scene: on its own, or started by the editor's Play. |
| `NyxShaderCompiler.exe` | Compiles shaders with Slang: the engine's at build time (target `NyxShaders`), and on request at run time, for live shader reload. |
| `NyxDump.exe` | Prints `.nyxscene` and `.nyxlinklog` files as text. Only reads files. |
| `NyxHeaderTool.exe` | Runs during the build: generates the reflection code from the engine's headers. |
| `NyxStartupPreview.exe` | Previews the startup banner ([Startup/README.md](../Startup/README.md)). |

All connections are TCP on `127.0.0.1` (`Net::Connection`, `Net::Listener` in
`NetConnection.h`). Nothing blocks and there are no threads: the owner polls once per frame. A
message on the wire is `payload size (u32) | type (u16) | payload`, little-endian, at most 64 MB.

- **Editor and game, the editor link (`EditorLink`).** Play saves the open scene, unsaved
  changes included, to `PlaySession.nyxscene` next to the executables, and starts `NyxGame.exe`
  with `--editor-port` (`ChildProcess`, `GameInstance`). The editor listens; the game connects
  back. Both send `Hello` first; the editor only accepts the process it started. The messages
  are in `EditorLinkMessages.h`:
  - editor to game: `Quit`, the live edits `SetProperties`, `CreateEntity` and `DeleteEntity`,
    `ShaderProgram` and `MaterialValues`, and the toolbar's Pause and Step,
    `SetSimulationPaused` and `StepSimulation`, which the game applies to its simulation clock
    (`ApplySimulationControl()`, `LiveEdits.cpp`);
  - game to editor: `LogLine` and `Crash`.
- **A program and the shader compiler (`RemoteShaderCompileService`).** The program listens,
  starts `NyxShaderCompiler.exe --port`, and sends compile requests; the compiler answers one at a
  time and exits when the connection ends. A compiler crash costs one compile, not the program.
  A game started by the editor compiles nothing itself: it takes the editor's compiles
  (`IRenderer::FollowEditorShaders()`).

## Modules

The engine's source folders are under `Engine/Source`: `Reflection`, `LinkTools`, `DevTools`, and
the folders of `Runtime`, each with an `include` and a `src` folder. A folder includes only from
the folders its row lists. `NyxRuntimeFolderDependencies`
(`Engine/Source/tests/FolderDependencies.cmake`) reads this table and fails on any `#include` it
doesn't allow, and on a table that goes round in a circle, so a new dependency starts here.

| Folder | What it holds | Includes from |
| --- | --- | --- |
| `Core` | Logging, assertions, paths, text helpers, child processes, the crash handler, file watching, the frame rate limiter, the fixed step clock, `BinaryArchive`, the profiler macros (`Profiler.h`), debug drawing (`DebugDraw`) | — |
| `Reflection` | Type and property metadata, and the `NYX_REFLECT` and `NYX_PROPERTY` macros; not under `Runtime` | — |
| `Net` | TCP connections and the messages they carry | Core |
| `Input` | Keyboard and mouse, gathered from backends into one `InputFrame` per frame | Core |
| `Scene` | Entities and components (the physics components among them), the component registry, scene files, the tagged binary format, surface anchors (`SurfaceAnchor`: a point, normal and tangent on an entity that move with it) | Core, Reflection |
| `Physics` | Rigid bodies, their shapes, collisions and the physics step, on Jolt (`PhysicsWorld`); Jolt's headers stay inside | Core |
| `Sim` | The game world: its entities, their physics bodies kept in step with the physics components (`PhysicsBodies`), and the simulation systems each fixed step runs, in order (`GameWorld`, `ISimSystem`) | Core, Scene, Physics |
| `Assets` | Asset files as plain data: images, materials, glTF meshes, shader compile requests and results | Core |
| `UI` | The painter, and the painted UI drawn with it on top of ImGui | Core, Reflection |
| `Link` | The editor link: its messages, live edits, recordings, the log sink | Core, Net, Scene, Assets |
| `LinkTools` | Link messages, recordings and scene files as readable text, for the editor and NyxDump; not under `Runtime`, and not in the game | Core, Reflection, Scene, Link |
| `Render` | The Vulkan renderer, scene views, shaders and materials, and applying the link's shaders and material values | Core, Net, Scene, Assets, UI, Link |
| `Platform` | The window | Core, Input, UI |
| `App` | The application and its layers, `main()`, the options the editor starts a game with | Core, Input, Scene, Render, Platform |

Each folder of `Runtime` has one namespace: `Nyx`, except `Net` (`Nyx::Net`), `Input`
(`Nyx::Input`) and `UI` (`Nyx::UI`).

Known issue: `Platform` includes `UI` because `WindowsWindow` paints the title bar and the dock
space. Both belong in the editor; once they move there, `Platform` no longer needs `UI`.

`DevTools` holds the painter's test sheet. It is in no library: the editor compiles it in Debug
builds, and the painter tests compile it.

Beyond the table:

- Nothing under `Engine/Source` includes editor or game code. Only the editor knows the editor.
- Code the engine has only for the editor sits behind `NYX_UI_INSPECTION` (ImGui source
  inspection); the editor's prototype and diagnostics tools behind `NYX_DEV_TOOLS`, which is
  off in Release builds.

The CMake targets:

- `NyxEngineCommon`, an object library with nearly all engine code, is linked into two static
  libraries: `NyxEngine` (`NYX_UI_INSPECTION=0`, for the game and the tools) and
  `NyxEditorEngine` (`=1`, for the editor).
- `NyxStartup`: the startup banner, drawn with GDI+ on a thread of its own.
- `NyxGeneratedReflection`: runs `NyxHeaderTool` over `Runtime/Scene/include`, where every
  reflected type is, into `Build/Windows/Generated`.
- `NyxLinkTools` (`Engine/Source/LinkTools`): an object library that the editor, NyxDump and the
  link tests link, and the game doesn't.
- `NyxEditor` and `NyxGame` are made by `nyx_add_game_programs()` (top `CMakeLists.txt`), with
  a game's module or without; a game's repository calls it with its own
  ([Game modules](Design/game-modules.md)). `Engine/Source/GameModules.cmake` has
  `nyx_add_game_module()` and `nyx_link_game_module()`, and `NyxNoGameModule`
  (`Engine/Source/NoGameModule`), the empty module the programs link without one. Tests and tools
  link no module.
- Nyx's CMake files name Nyx's folders by `PROJECT_SOURCE_DIR`, not `CMAKE_SOURCE_DIR`: in a
  game's build the top folder is the game's.
- `NyxEditorLib`: the editor without its `main()`, a static library that `NyxEditor` and the
  editor's tests link, so the editor compiles once.
- Tests are in `Engine/Source/tests` and `Editor/Source/tests`; `ctest -C Debug` in
  `Build/Windows` runs them all.

## Editor folders

The editor's source folders are under `Editor/Source`, each with an `include` and a `src` folder;
its tests are in `Editor/Source/tests`. A file used by one folder lives in that folder. `Common`
holds only pieces used by two or more folders, and includes no other editor folder. As for the
engine, a folder includes only from the folders its row lists, and `NyxEditorFolderDependencies`
checks every `#include` against this table.

| Folder | What it holds | Includes from |
| --- | --- | --- |
| `Common` | Native file dialogs, the UI source inspector, where reflected properties are declared, property values read and written through reflection, the precompiled header | — |
| `Assets` | The asset database, asset types, dragging assets, the asset browser | — |
| `Materials` | The material open for editing, the Parameters and preview windows, New Material, the Shader Log | — |
| `Transactions` | Undo and redo: transactions, their changes, snapshots of objects | Common |
| `Preferences` | The editor's preferences, the Preferences and Theme windows, opening source in the chosen editor | Common |
| `DevTools` | The prototype and diagnostics tools, in Debug builds only (`NYX_DEV_TOOLS`): Painter Test, Mockup Overlay, ImGui's debug windows | Common |
| `Properties` | Drawing and editing reflected properties: a widget per property kind, and the edit in progress | Common, Transactions, Assets |
| `Scene` | The scene being edited: its document and undo, the outliner, details, gizmo, scene views and their cameras, scene files, copy and paste | Common, Transactions, Assets, Preferences, Properties |
| `Play` | Play and Play All: the games, their windows and crash cards, the Game Link window, edits sent to the games | Transactions, Preferences, Scene |
| `App` | `EditorLayer`, which owns and wires up everything else; `EditorFrame`, the titlebar with its menus and the status bar; `MaterialWorkspace`, which feeds the material windows and makes new materials; the workspaces, the asset types the editor knows | Common, Assets, Materials, Preferences, DevTools, Properties, Scene, Play |

## One frame

`main()` (`EntryPoint.h`) starts the logger, asks the program for its application
(`CreateApplication()`) and calls `Application::Run()`, which loops until the window closes:

1. `IWindow::WaitForNextFrame()`: the frame rate cap.
2. `PollEvents()`, then `InputSystem::BeginFrame()`: this frame's input.
3. Every layer's `OnUpdate(deltaTime)`, in the order they were pushed.
4. The fixed simulation steps: `Application`'s `FixedStepClock` turns the frame's real time into
   steps of 1/60 s (at most 4 a frame, none while paused), and each step runs every layer's
   `OnFixedUpdate(step)`, in push order. When it has to drop time, `Application` warns once, and
   again only after it caught up.
5. `Application::DrawFrame()`. A minimized window draws nothing and waits 50 ms
   (`IWindow::WaitIfMinimized()`), so the frame loop (and the editor link) keeps running.
   Otherwise `VulkanRenderer::DrawFrameOnDevice()`, which leaves the UI to its `VulkanUIBackend`:
   1. waits for the last frame's fence (one frame in flight), then reads back what that frame
      left: timestamps, the painter's binning, picks;
   2. acquires the swapchain image, and takes shader updates;
   3. renders every scene view: the editor's passes (picking only after a click, selection
      masks only when they changed), then the scene in HDR, tonemapped, with the editor's
      overlays (`VulkanEditorOverlays`, in the views that show them) and the world's debug
      drawing (`VulkanDebugLines`) over it; then blurs each view's picture;
   4. builds the UI: an ImGui frame in which the window's dock area and strips (`IWindow::DrawUI()`)
      hold every layer's `OnUI()`, then the painted window
      chrome, the painter canvases, and the window painters' segments, binned on the GPU;
   5. records the UI pass, submits, renders ImGui's OS windows, signals the fence and presents.
6. `InputSystem::EndFrame()`: sets the cursor the layers asked for.

If the GPU device is lost, the renderer stops drawing and `Application` closes the window.

### Profiling

`Profiler.h` (`Core`) has Nyx's profiler macros: `NYX_PROFILE_SCOPE(name)`,
`NYX_PROFILE_FUNCTION()`, `NYX_PROFILE_FRAME()`, `NYX_PROFILE_THREAD(name)`, `NYX_PROFILE_PLOT()`
and `NYX_PROFILE_MESSAGE()`. They wrap Tracy, and only `Profiler.h` includes Tracy's headers.

- The CMake option `NYX_PROFILER` (on by default) builds Tracy's client into `NyxEngine` and
  `NyxEditorEngine`, so every program has it; off, the macros are nothing and Tracy isn't built.
- Tracy collects only once a viewer connects (`TRACY_ON_DEMAND`), listens only on `127.0.0.1`,
  from port 8086 up (each program running takes the next free one), and leaves crashes to Nyx's
  crash handler.
- Zones: the steps of `Application::Run()` above, one per layer's `OnUpdate` and `OnFixedUpdate`
  (named by the layer's type), one per fixed step, and the renderer's steps in
  `DrawFrameOnDevice()` and `VulkanUIBackend`. Each frame ends with a frame mark; `main()` names
  the main thread.

## Who owns what

- **`Application`**: the window, the renderer, the `InputSystem`, the simulation clock
  (`GetSimulationClock()`, for pausing and stepping) and the layers. It makes the
  renderer once the window exists, to draw into it. `GetRenderer()` gives the renderer's scene part
  (`IRenderer`), what a game needs; `GetEditorRenderer()` all of it (`IEditorRenderer`), with the
  editor's overlays and picking, material editing, and what ImGui shows. At shutdown it waits for
  the GPU, detaches the layers in reverse order while the renderer still exists, then shuts the
  renderer down before the window goes.
- **The game module** (`GameModule`): a game's own component types, registered next to the
  engine's. The engine never knows a game: each program passes the module it was built with
  (`GetLinkedGameModule()`) in `ApplicationSpecs::Module`, and `Application` registers its types
  after the engine's, before any layer is attached. `GetGameModule()` gives it to the layers; the
  editor registers the module's source locations from it. A module type named like a type already
  registered is refused with an error, so the engine's types always keep their names.
- **`WindowsWindow`** (`IWindow`): the GLFW window, its input, its dock area and frame strips, the
  frame rate limiter, and the startup banner until `FinishStartup()`. It knows nothing of the
  renderer: `Application` hands the renderer the window (`GetNativeWindow()`) and is told when its
  framebuffer changes size.
- **The renderer**: everything on the GPU: scene views, shaders, materials; in its
  `VulkanUIBackend` ImGui, the painter, glass, UI images and screenshots; in its
  `VulkanEditorOverlays` the grid, the objects' bounds, picking and the selection outline; in its
  `VulkanDebugLines` every view's debug lines, the overlays' and the worlds' debug drawing
  (`SetWorldDebugDraw()`). It holds worlds and their drawings only by pointer (`SetWorld()`,
  `SetSceneViewWorld()`): their owner keeps them alive, and calls `SetWorld()` again after
  replacing what's in one.
- **Layers**: `EditorLayer` in the editor; `GameLayer` and, when the editor started the game,
  `EditorLinkLayer` in the game.
- **Worlds** (`Registry`): the scene being edited is in `SceneDocument`, inside
  `SceneEditor`, inside `EditorLayer`. The material preview has a world of its own
  (`MaterialPreviewPanel`). The game's is in `GameLayer`. The editor owns the world you edit; a
  game only exists while you play, and gets the editor's edits over the link.

## Rules across files and processes

- **Never write an `Entity` handle into a file or a message.** A handle is a slot in one
  `Registry` (index and generation), reused after a delete and different in every program. Use
  the entity's `EntityGuid` (its `GuidComponent`): a random 64-bit number, unique in its scene.
  Loading a scene gives entities without one, or with a duplicate, a new one.
- **Name components by type name and properties by name hash**, never by index or offset. A
  property's hash is the FNV-1a hash of its name, generated by the header tool, which checks it
  is unique in its type. Renaming a property loses its saved values.
- **Live edits send state, not changes**: the editor sends what an entity is after each edit,
  undo or redo, so the game converges with the editor whatever was missed.

The tagged binary format (`ReflectedArchiveSerializer`, `BinaryArchive`) holds scene files and
the link's entity messages. Every block carries its size, and every property its name hash and
kind, so:

- a property the data doesn't have keeps its value (a new object's default);
- a property or component type this build doesn't know is skipped with a warning, and saving
  drops it;
- number kinds (`Int32`, `UInt32`, `Float`) convert into each other;
- a kind this build doesn't know ends the block, since its size is unknown;
- damaged data fails cleanly: every length is checked against the bytes left, and nesting is
  limited.

Versions: scene files are version 2 (`SceneFileVersion`); raise it only for a change these rules
can't absorb. Raise `EditorLinkProtocolVersion` when a message changes in a way the other side
would misread; a new message type needs no new version, as a side that doesn't know a type skips
it with a warning. `Hello`'s layout never changes.

## How to add...

### A component

1. A header in `Engine/Source/Runtime/Scene/include`, the folder the header tool reads: a struct
   marked `NYX_REFLECT(Component, ...)`, its properties `NYX_PROPERTY(Edit, Undo, Serialize, ...)`,
   and `#include "Generated/Runtime/<Header>.reflect.h"` at the end. `NameComponent.h` is the
   smallest example.
2. A `.cpp` that includes `NyxPCH.h` and the header; list both in `Engine/Source/CMakeLists.txt`.
3. Build. The header tool registers the type, and `RegisterComponentTypes()` puts it into the
   `ComponentTypeRegistry`: saving, loading, the details panel, undo, Add Component, live edits
   and NyxDump all work from that.
4. If it needs work after loading, such as resolving asset paths, add a `SetPostLoad()` in
   `ComponentRegistration.cpp`.

### An asset type

1. Register an `AssetTypeDescriptor` in `RegisterEditorAssetTypes()`
   (`Editor/Source/App/src/EditorAssetTypes.cpp`): its type id, display name, extensions, and
   what opening it does (`Activate`, a callback that captures what it needs). `EditorLayer`
   owns the registry and fills it in `OnAttach()`; the asset browser lists the type from then
   on.
2. For scenes to use it: an `AssetReference` property on a component, resolved in its post-load
   step through `IAssetResolver`. `AssetResolver` resolves materials from their `.nyxmat` files;
   meshes are only the built-in cube and sphere so far.

### A link message

1. A number in `EEditorLinkMessage`; never reuse one.
2. A struct with `Type`, `Write()` and `Read()` in `EditorLinkMessages.h` and `.cpp`.
3. A case in `DescribeEditorLinkMessage()` (`EditorLinkMessageText.cpp`, in `LinkTools`), so the
   Game Link window and NyxDump can show it.
4. Send it with `EditorLink::Send()`; handle it in the game's `EditorLinkLayer` (or in
   `ApplyLiveEdit()`, `LiveEdits.cpp`, and `ApplyRendererEdit()`, `RendererLiveEdits.cpp`), or in the editor's
   `GameInstance`.
5. Test it in `NyxEditorLinkTests`.

### A scene view feature

- A per-view setting: a setter by view id on `IRenderer` (`Renderer.h`), or on `IEditorRenderer`
  (`EditorRenderer.h`) when only the editor uses it, a field on
  `SceneViewInstance` (`SceneView.h`), and its use in `VulkanRenderer`: `RenderSceneView()`
  (the scene pass, then the picture pass) or `UpdateSceneUniforms()`.
- Per-view GPU resources: made in `CreateSceneResourcesForView()`, made again where
  `DrawEditorPasses()` resizes a view, released in `DestroySceneViewNow()`.
- Anything only the editor should see goes in `VulkanEditorOverlays`, with its per-view resources
  in `SceneViewOverlays`. The renderer calls it for the views whose `bShowEditorOverlays` is on:
  off until the editor turns it on (`IEditorRenderer::SetSceneViewShowEditorOverlays()`), and off
  again while a view shows the game.
- The editor's side is `SceneViewPanel`; `NyxSceneGpuTests` renders views on a real GPU.

## Global registries and singletons

| What | Where it is filled |
| --- | --- |
| `Logger::Get()` | `Init()` in `main()` (`EntryPoint.h`); the game adds a sink for the editor link (`EditorLinkLogSink::Install()`) |
| `ComponentTypeRegistry::Get()` | `RegisterComponentTypes()`, then the game module's `RegisterTypes()`, in `Application`'s constructor |
| `PropertyWidgetRegistry::Get()` (editor) | `RegisterDefaultPropertyWidgets()`, in `EditorLayer::OnAttach()` |
| `ReflectionSourceRegistry::Get()` (editor) | `RegisterRuntimeReflectedSources()`, generated, in `EditorLayer::OnAttach()` |
| `Nyx::UI::GetTheme()` | the defaults in `UITheme.h`; the editor loads its theme file in `ThemeEditorWindow::LoadAtStartup()` |
| The painted UI's state: chrome, animations, window painters, diagnostics | `UIChrome.cpp`, `UIAnimation.cpp`, `WindowPainter.cpp`, `UIDiagnostics.cpp`, while each frame's UI is built |
| `WindowPainter::SetSegmentDrawer()` | the renderer's `VulkanUIBackend`, in `InitializePasses()`; cleared in `Shutdown()` |
| The crash handler | `InstallCrashHandler()`, in the game's `CreateApplication()` |
| The engine and project roots (`Paths`) | read from `NyxRoots.ini` next to the program on first use, or found above it, and kept |

## Design notes

How some systems work, and why they work that way:

- [Shaders and materials](Design/shader-materials.md): Slang for every shader, the shader cache
  and live reload, bindless resources, material files and instances, the light materials are
  judged under.
- [UI painter](Design/ui-painter.md): signed distance field shapes, tile binning on the GPU,
  painting in order with ImGui's drawing, the painted widgets, chrome, glass and ambilight.
- [Input system](Design/input-system.md): backends, input frames, keys by position, the cursor
  asked for each frame.
- [Game modules](Design/game-modules.md): how a game outside Nyx brings its own component types
  into the editor and the game, and what the CMake functions do.
- [Simulation loop](Design/simulation-loop.md): the fixed steps, and the plan for commands,
  systems and determinism.
- [Physics](Design/physics.md): Jolt behind `PhysicsWorld`, bodies, layers, forces, the build, the
  numbers.
- [Undo by guid](Design/undo-by-guid.md): why undo names entities by guid, and how it keeps
  components whole.
