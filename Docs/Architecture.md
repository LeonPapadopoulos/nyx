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
    and `ShaderProgram` and `MaterialValues`;
  - game to editor: `LogLine` and `Crash`.
- **A program and the shader compiler (`RemoteShaderCompileService`).** The program listens,
  starts `NyxShaderCompiler.exe --port`, and sends compile requests; the compiler answers one at a
  time and exits when the connection ends. A compiler crash costs one compile, not the program.
  A game started by the editor compiles nothing itself: it takes the editor's compiles
  (`IRenderer::FollowEditorShaders()`).

## Modules

The engine's source folders, under `Engine/Source`:

- `Runtime/Core`: logging, assertions, paths, the crash handler, child processes, TCP connections.
- `Reflection`: type and property metadata, and the `NYX_REFLECT` and `NYX_PROPERTY` macros.
- `Runtime/Input`: keyboard and mouse, gathered from backends into one `InputFrame` per frame.
- `Runtime/Engine`: the application and window, entities and components, serialization, the
  editor link and live edits.
- `Runtime/Renderer`: the Vulkan renderer, shaders and materials, the painter and the painted UI.
- `DevTools`: the painter's test sheet. Not in any library: the editor compiles it in Debug
  builds, and the painter tests compile it.

Which may depend on which:

- Core and Reflection depend on no other Nyx module; Input only on Core.
- Engine and Renderer use each other (the window owns the renderer, the renderer reads the
  world) and are built as one library.
- Nothing under `Engine/Source` includes editor or game code. Only the editor knows the editor.
- Code the engine has only for the editor sits behind `NYX_UI_INSPECTION` (ImGui source
  inspection); the editor's prototype and diagnostics tools behind `NYX_DEV_TOOLS`, which is
  off in Release builds.

The CMake targets:

- `NyxEngineCommon`, an object library with nearly all engine code, is linked into two static
  libraries: `NyxEngine` (`NYX_UI_INSPECTION=0`, for the game and the tools) and
  `NyxEditorEngine` (`=1`, for the editor).
- `NyxStartup`: the startup banner, drawn with GDI+ on a thread of its own.
- `NyxGeneratedReflection`: runs `NyxHeaderTool` over `Runtime/Engine/include` and
  `Runtime/Renderer/include`, into `Build/Windows/Generated`.
- `NyxEditor` compiles its sources straight into the executable rather than a library: the asset
  type registrations are global objects that a linker would drop from a library.
- Tests are in `Engine/Source/tests` and `Editor/Source/tests`; `ctest -C Debug` in
  `Build/Windows` runs them all.

## One frame

`main()` (`EntryPoint.h`) starts the logger, asks the program for its application
(`CreateApplication()`) and calls `Application::Run()`, which loops until the window closes:

1. `IWindow::WaitForNextFrame()`: the frame rate cap.
2. `PollEvents()`, then `InputSystem::BeginFrame()`: this frame's input.
3. Every layer's `OnUpdate(deltaTime)`, in the order they were pushed.
4. `IWindow::DrawFrame()`. A minimized window draws nothing and waits 50 ms, so the frame loop
   (and the editor link) keeps running. Otherwise `VulkanRenderer::DrawFrameOnDevice()`:
   1. waits for the last frame's fence (one frame in flight), then reads back what that frame
      left: timestamps, the painter's binning, picks;
   2. acquires the swapchain image, and takes shader updates;
   3. renders every scene view: the editor's passes (picking only after a click, selection
      masks only when they changed), then the scene in HDR, tonemapped, with the editor's
      overlays; then blurs each view's picture;
   4. builds the UI: an ImGui frame in which every layer's `OnUI()` runs, then the painted window
      chrome, the painter canvases, and the window painters' segments, binned on the GPU;
   5. records the UI pass, submits, renders ImGui's OS windows, signals the fence and presents.
5. `InputSystem::EndFrame()`: sets the cursor the layers asked for.

If the GPU device is lost, the renderer stops drawing and the window closes.

## Who owns what

- **`Application`**: the window, the `InputSystem` and the layers. At shutdown it waits for the
  GPU, then detaches the layers in reverse order while the renderer still exists.
- **`WindowsWindow`** (`IWindow`): the GLFW window, the renderer (`IRenderer`), the frame rate
  limiter, and the startup banner until `FinishStartup()`.
- **The renderer**: everything on the GPU: scene views, shaders, materials, the painter. It
  holds worlds only by pointer (`SetWorld()`, `SetSceneViewWorld()`): their owner keeps them alive,
  and calls `SetWorld()` again after replacing what's in one.
- **Layers**: `EditorLayer` in the editor; `GameLayer` and, when the editor started the game,
  `EditorLinkLayer` in the game.
- **Worlds** (`Engine::Registry`): the scene being edited is in `SceneDocument`, inside
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

1. A header in `Engine/Source/Runtime/Engine/include` (or `Runtime/Renderer/include`): a struct
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

1. `Editor/Source/src/<Name>AssetTypeRegistration.cpp` with a global `AssetTypeRegistrar` for
   an `AssetTypeDescriptor`: its type id, display name, extensions, and what opening it does
   (`Activate`). List it in `Editor/Source/CMakeLists.txt`. The asset browser lists it from then
   on.
2. For scenes to use it: an `AssetReference` property on a component, resolved in its post-load
   step through `IAssetResolver`. `AssetResolver` resolves materials from their `.nyxmat` files;
   meshes are only the built-in cube and sphere so far.

### A link message

1. A number in `EEditorLinkMessage`; never reuse one.
2. A struct with `Type`, `Write()` and `Read()` in `EditorLinkMessages.h` and `.cpp`.
3. A case in `DescribeEditorLinkMessage()`, so the Game Link window and NyxDump can show it.
4. Send it with `EditorLink::Send()`; handle it in the game's `EditorLinkLayer` (or in
   `ApplyLiveEdit()` and `ApplyRendererEdit()`, `LiveEdits.cpp`), or in the editor's
   `GameInstance`.
5. Test it in `NyxEditorLinkTests`.

### A scene view feature

- A per-view setting: a setter by view id on `IRenderer` (`Renderer.h`), a field on
  `SceneViewInstance` (`SceneView.h`), and its use in `VulkanRenderer`: `RenderSceneView()`
  (the scene pass, then the picture pass) or `UpdateSceneUniforms()`.
- Per-view GPU resources: made in `CreateSceneResourcesForView()`, made again where
  `DrawFrameOnDevice()` resizes a view, released in `DestroySceneViewNow()`.
- Anything only the editor should see goes behind the view's `bShowEditorOverlays`, which a view
  showing the game turns off.
- The editor's side is `SceneViewPanel`; `NyxSceneGpuTests` renders views on a real GPU.

## Global registries and singletons

| What | Where it is filled |
| --- | --- |
| `Core::Logger::Get()` | `Init()` in `main()` (`EntryPoint.h`); the game adds a sink for the editor link (`EditorLinkLogSink::Install()`) |
| `ComponentTypeRegistry::Get()` | `RegisterComponentTypes()`, in `Application`'s constructor |
| `AssetTypeRegistry::Get()` (editor) | the global `AssetTypeRegistrar` objects in `*AssetTypeRegistration.cpp`, before `main()` |
| `PropertyWidgetRegistry::Get()` (editor) | `RegisterDefaultPropertyWidgets()`, in `EditorLayer::OnAttach()` |
| `ReflectionSourceRegistry::Get()` (editor) | `RegisterRuntimeReflectedSources()`, generated, in `EditorLayer::OnAttach()` |
| `Nyx::UI::GetTheme()` | the defaults in `UITheme.h`; the editor loads its theme file in `ThemeEditorWindow::LoadAtStartup()` |
| The painted UI's state: chrome, animations, window painters, diagnostics | `UIChrome.cpp`, `UIAnimation.cpp`, `WindowPainter.cpp`, `UIDiagnostics.cpp`, while each frame's UI is built |
| `WindowPainter::SetSegmentDrawer()` | the renderer, in `Initialize()`; cleared in `Shutdown()` |
| The crash handler | `InstallCrashHandler()`, in the game's `CreateApplication()` |
| The project root (`Paths`) | found on first use and kept |

## Design notes

The plans some systems were built from, with what was decided and why. They were written before
the code; most record what turned out differently in *As built* sections. Where they and the
code disagree, the code is right.

- [Shaders and materials](Design/shader-materials.md): one compiler with live reload for every
  shader, bindless resources, physically based materials, material instances.
- [UI painter](Design/ui-painter.md): signed distance field shapes, tile binning on the GPU,
  painting inside ImGui windows, the painted widgets and window chrome.
- [Input system](Design/input-system.md): devices, input frames, and the actions and bindings
  planned on top. Only the first part, the `Runtime/Input` module, is built.
- [Undo by guid](Design/undo-by-guid.md): why undo names entities by guid, and how components
  are kept whole in undo steps.
