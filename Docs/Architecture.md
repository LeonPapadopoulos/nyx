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
| `NyxPadCheck.exe` | Lists gamepads, prints what they report and tries their rumble, trigger effects and light, for checking pads by hand. |
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
    (`ApplySimulationControl()`, `LiveEdits.cpp`), and the input asset as it is edited,
    `SetInputAsset`, and whether the pads drive the game without the focus, `SetInputBackground`
    (`ApplyLiveInput()`, `LiveInput.cpp`), and `InputSubscribe`, which starts or stops the game's
    input stream, and `InspectEntity`, a question about one entity's run-time components;
  - game to editor: `LogLine` and `Crash`, and while subscribed the game's input as
    `InputDefinitions` and `InputFrame` records (`InputStream.h`), and the answer to
    `InspectEntity`, `InspectedEntity` ([Runtime inspection](Design/runtime-inspection.md)).
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
| `Core` | Logging, assertions, paths, text helpers, child processes, the crash handler, file watching, the frame rate limiter, the fixed step clock, `BinaryArchive`, the profiler macros (`Profiler.h`), debug drawing (`DebugDraw`), meshes as plain data (`MeshData`: vertices with four custom channels for the materials, indices and sections, `CheckMeshData()`, `ComputeMeshBounds()`), shapes drawn by hand told apart (`PointCloudRecognizer`, the $P point-cloud recogniser, and its template text) | — |
| `Reflection` | Type, property and enum metadata, and the `NYX_REFLECT`, `NYX_PROPERTY` and `NYX_ENUM` macros; not under `Runtime` | — |
| `Net` | TCP connections and the messages they carry | Core |
| `Input` | Keyboard, mouse and gamepads (through SDL3), gathered from backends into one `InputFrame` per frame; the router: context stacks, actions, the value pipeline, triggers and their traces; the input's history and its view (`InputDebugView`), painted with the UI's widgets; input assets (`.nyxinput`), saved through reflection, and their tuning layers; the checks and recipes for bindings; the input's stream and recordings (`.nyxinputrec`); the game's fly cameras, for entities with a `FlyCameraComponent` (`UpdateFlyCameras()`), and its orbit cameras' input (`OrbitCameraInput`, `Input::UpdateOrbitCameras()`) | Core, Reflection, Scene, UI |
| `Scene` | Entities and components (the physics components, `ConstraintComponent` with its runtime `ConstraintStateComponent`, `CharacterComponent` and `OrbitCameraComponent` among them, and the moves along the ground relative to a camera, `GetGroundMove()`), the component registry, scene files, the tagged binary format, surface anchors (`SurfaceAnchor`: a point, normal and tangent on an entity that move with it), overridable values (`ValueOverride`, `OverridableValue`: a component's overrides of values it inherits from elsewhere, and what its type's provider lists, `SetOverridableValues<T>()`), surface decals (`SurfaceDecalComponent`), point lights (`PointLightComponent`), particle emitters (`ParticleEmitterComponent`) and the air a game carries them by (`IParticleVelocityField`), meshes made at run time (`RuntimeMeshComponent`, not saved), a mesh drawn at many transforms (`MeshInstancesComponent`, not saved, each instance with custom numbers for the materials), the ground (`GroundComponent`, its heights `SampleGroundHeight()` and its mesh `MakeGroundMesh()`, made in its post-load step), scattering (`ScatterComponent`: copies of an entity's mesh over another entity's mesh, `ScatterOnMesh()`, into the entity's `MeshInstancesComponent`, made in its post-load step and following its surface and the blockers each frame, `UpdateScatters()`, cell by cell so an edit makes only the cells it reaches again; `ScatterBlockerComponent` keeps copies out of an entity's collider or a circle round it; a noise mask), gradient noise shared by the ground and the scatter (`SampleGradientNoise()`), the world's air (`EnvironmentComponent`: height fog, the sky following the sun, made by the air's scattering or a tinted cube map, bloom, ambient occlusion, light shafts, the wind and the most particles), the high cloud layer (`CloudLayerComponent`), the volumetric clouds (`VolumetricCloudsComponent`: a band between two altitudes, its weather, shapes, lighting and quality, how its pixels are marched each frame, and presets at a game's scale that set its values once, `ApplyVolumetricCloudsPreset()`, its post-load step, which also folds older scenes' Temporal into the update, `FoldLegacyVolumetricCloudsUpdate()`), the time of day (`TimeOfDayComponent`: the time, the sun's path and which entity is the sun; `TimeOfDay.h`: the sun's direction and rotation at an hour, its light fading below the horizon, the world's sun turned to the time each frame, `ApplyTimeOfDay()`), colour grading (`ColorGradingComponent`: a grade, or grades at heights of the sun blended by it), a camera's view and projection and the ray back through a point of its picture (`CameraProjection.h`), run-time component types, made while the world runs and never saved (`EComponentTypeFlags::Runtime`), and what an entity's are (`InspectRuntimeComponents()`) | Core, Reflection |
| `Physics` | Rigid bodies, their shapes (boxes, spheres, capsules, static triangle meshes), collisions and the physics step, constraints (each end in its body's own space, limits, motors, the load they held, switched off and on, breaking), and characters, on Jolt (`PhysicsWorld`); Jolt's headers stay inside | Core |
| `Sim` | The game world: its entities, their physics bodies and characters kept in step with the physics components (`PhysicsBodies`), their constraints with their `ConstraintComponent`s (`PhysicsConstraints`), the physics materials their colliders name, read once, kept and changed live (`PhysicsMaterials`, `GetPhysicsMaterial()`), and the simulation systems each fixed step runs, in order (`GameWorld`, `ISimSystem`), the players' commands each step applies (`CommandQueue`), the time of day moving on with the steps while it runs, the same whatever the frames (`TimeOfDayClock`), the entity a ray hits first (`PickPhysicsBody()`), the orbit cameras following their targets as drawn (`UpdateOrbitCameras()`), the air a game gives its world's particles (`GameWorld::SetParticleVelocityField()`), fields over the world kept only where something happens (`SparseField`: bricks of cells under a cap, sources, diffusion, decay, advection, sampling, deterministic) | Core, Reflection, Scene, Physics, Assets |
| `Assets` | Asset files as plain data: images, materials and their options (`MaterialOptions`, reflected, e.g. two-sided), reflected assets (`ReflectedAssetText`: a reflected struct's values as `Name = value` lines, the file's other lines kept; `ReflectedAssetTypes`: the asset types a game or Nyx registers with their struct and check, and who listens for their live text), physics materials (`.nyxphysmat`, one of them: `PhysicsMaterialData`, reflected, with a game's own extras), glTF meshes, shader compile requests and results; `Scene` for `AssetReference` | Core, Reflection, Scene |
| `UI` | The painter, and the painted UI drawn with it on top of ImGui, the dock space among it (`Nyx::UI::DockSpace()`, docked windows down to `DockedWindowMinWidth`) | Core, Reflection |
| `Link` | The editor link: its messages, live edits, a reflected asset's live text to whoever listens for its type (`ApplyAssetValues()`), live tuning of the input (`LiveInput`), the answer to the editor's question about an entity's run-time components (`MakeInspectedEntityMessage()`), recordings, the log sink | Core, Net, Input, Scene, Assets |
| `LinkTools` | Link messages, recordings, scene files and input files as readable text, for the editor and NyxDump; not under `Runtime`, and not in the game | Core, Reflection, Scene, Input, Link |
| `Render` | The Vulkan renderer, scene views, shaders and materials, meshes made at run time and mesh instances on the GPU (`RuntimeMeshCache`, `MeshInstanceCache`), culling by the views' cameras (`SceneCulling.h`), the primary light's cascaded shadow maps (`ShadowCascades.h`, `VulkanShadows`), the world's air as plain math (`Atmosphere.h`: the sun through the air, the sky's tint, height fog along a ray), the sky made by the air's scattering (`SkyAtmosphere.h`: the air in range, its transmittance, the horizon for the camera's height, what is made again; `VulkanSkyAtmosphere`, its tables per view by compute shaders), bloom (`Bloom.h`, `VulkanBloom`), ambient occlusion (`AmbientOcclusion.h`, `VulkanAmbientOcclusion`), light shafts (`LightShafts.h`: the sun's place on the picture and their fade; `VulkanLightShafts`, a radial blur per view at half size), the high cloud layer (`CloudLayer.h`: its shell met from below, at and above it, the near fade, the wind's origin at the world's time; `VulkanCloudLayer`, its noise by a compute shader per seed and the clouds per pixel of each view; drawn into the sky's cube too while it lights the world, made again as it moves, `IsCloudCaptureChanged()`), the volumetric clouds (`VolumetricClouds.h`: their band entered and left from below, inside and above it, the noises carried by the wind at the world's time, which pixel of each 2 x 2 or 4 x 4 a frame marches, smaller squares while the picture moves fast, when everything is marched again; `VulkanVolumetricClouds`, their noises by compute shaders per seed, a march at half or the view's size and the frame before's picture carried), colour grading (`ColorGrading.h`: a grade by the sun's height, what it makes of a colour; `VulkanColorGrading`, each view's lookup table, baked when its grade moved), multisample anti-aliasing of the scene views (`Multisampling.h`, resolved in `VulkanViewportTarget`), point lights as plain math (`PointLights.h`: their falloff, a world's lights, a view's culling of them), particles (`Particles.h`: emitters stepped on the CPU by the world's time, their budgets, sorting; a game's air asked where each particle is; `VulkanParticles`, their pass), each frame's numbers once its GPU work is done (`RenderFrameStats`, `SceneViewFrameStats`: CPU times, GPU times by timestamps, what each view drew and lit), and applying the link's shaders and material values | Core, Net, Scene, Assets, UI, Link |
| `Platform` | The window, and where it is kept on the monitors (`FitWindowOnScreen()`) | Core, Input, UI |
| `App` | The application and its layers, `main()`, the options the editor starts a game with, what a game module brings (`GameModule`) and its player (`IPlayerController`, turning input into commands each frame), the player Nyx's game has without one (`ThirdPersonPlayer`), frame statistics (`FrameStats.h`: a frame's numbers, windows of a second's average and worst, `Application::GetFrameStats()`) and benchmarks (`FrameBenchmark.h`: the summary, the CSV, the camera path) | Core, Input, Scene, Sim, Assets, Render, Platform |

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
- `NyxGeneratedReflection`: runs `NyxHeaderTool` over `Runtime/Scene/include` and
  `Runtime/Input/include`, where every reflected type is, into `Build/Windows/Generated`.
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
| `Common` | Native file dialogs and showing a file in File Explorer, the UI source inspector, where reflected properties are declared, property values read and written through reflection, dragging entities, the precompiled header | — |
| `Assets` | The asset database, asset types, dragging assets, the asset browser | Common |
| `Materials` | The material open for editing, the Parameters and preview windows, New Material, the Shader Log | — |
| `Transactions` | Undo and redo: transactions, their changes, snapshots of objects | Common |
| `Preferences` | The editor's preferences, the Preferences and Theme windows, opening source in the chosen editor | Common |
| `DevTools` | The prototype and diagnostics tools, in Debug builds only (`NYX_DEV_TOOLS`): Painter Test, Mockup Overlay, ImGui's debug windows | Common |
| `Properties` | Drawing and editing reflected properties: a widget per property kind (an entity reference's picker among them), a component's overridable values as one row each, inherited or overridden (`OverrideRows`), problems under the properties they are about, and the edit in progress | Common, Transactions, Assets |
| `Scene` | The scene being edited: its document and undo, the outliner, details, gizmo, scene views (`SceneViewSet`: any number of `SceneViewPanel`s) and their cameras, scene files, copy and paste | Common, Transactions, Assets, Preferences, Properties |
| `Play` | Play and Play All: the games, their windows and crash cards, the Game Link window, edits sent to the games | Transactions, Preferences, Scene |
| `ReflectedAssets` | The assets of reflected asset types open for editing, physics materials and a game's own (`ReflectedAssetDocument`), with their undo, files read again and values for the games (`ReflectedAssetEditors`), and their windows (`ReflectedAssetWindows`) | Transactions, Properties |
| `Input` | The Input window (*Window > Input*): the editor's or a running game's input, its history and traces; the input asset being edited, with its undo (`InputAssetDocument`), and the Tuning and Checks tabs | Common, Transactions, Properties, Play |
| `App` | `EditorLayer`, which owns and wires up everything else; `EditorFrame`, the titlebar with its menus and the status bar; `MaterialWorkspace`, which feeds the material windows and makes new materials; the workspaces and their layout files (`Workspaces`, in the project's `Saved/Layouts`, with the window's placement), the asset types the editor knows | Common, Assets, Materials, ReflectedAssets, Preferences, DevTools, Properties, Scene, Play, Input |

## One frame

`main()` (`EntryPoint.h`) starts the logger, asks the program for its application
(`CreateApplication()`) and calls `Application::Run()`, which loops until the window closes:

1. `IWindow::WaitForNextFrame()`: the frame rate cap.
2. `PollEvents()`, then `InputSystem::BeginFrame()`: this frame's input.
3. Every layer's `OnUpdate(deltaTime)`, in the order they were pushed. `Application` times this
   step, the next two and the steps' count, for the frame's numbers (`ApplicationFrameTimes`).
4. The fixed simulation steps: `Application`'s `FixedStepClock` turns the frame's real time into
   steps of 1/60 s (at most 4 a frame, none while paused), and each step runs every layer's
   `OnFixedUpdate(step)`, in push order. When it has to drop time, `Application` warns once, and
   again only after it caught up.
5. Every layer's `OnLateUpdate(deltaTime)`, in push order: for what follows the world as it is
   drawn this frame, e.g. the game's orbit cameras on their targets, by the clock's
   `GetDrawFactor()`, the factor the renderer draws the world by.
6. `Application::DrawFrame()`. A minimized window draws nothing and waits 50 ms
   (`IWindow::WaitIfMinimized()`), so the frame loop (and the editor link) keeps running.
   Otherwise `VulkanRenderer::DrawFrameOnDevice()`, which leaves the UI to its `VulkanUIBackend`:
   1. waits for the last frame's fence (one frame in flight), then reads back what that frame
      left: timestamps, the painter's binning, picks; completes that frame's numbers
      (`CompleteFrameStats()`: its CPU times, its views' counts and its GPU times, all of one
      frame); and destroys the GPU meshes made at run time that were replaced or no longer drawn
      since;
   2. acquires the swapchain image, and takes shader updates;
   3. extracts what each shown world draws this frame: its objects (a mesh made at run time
      uploaded here, before any draw is recorded, when it is new or its revision changed:
      `RuntimeMeshCache`; mesh instances so too: `MeshInstanceCache`), each with its bounds in the
      world, and those drawn instanced with their cells', widened as far as its materials' vertex
      functions move it (`Material::MaxVertexOffset`), its surface decals
      (`ExtractSurfaceDecals()`, every anchor resolved through one guid lookup) and its point
      lights (`ExtractPointLights()`), all between their last two steps, its particles moved on
      by its time and carried by its air (`ExtractParticles()`, `UpdateParticles()`) by one factor for the world when it is drawn so, its time for what
      moves by itself (its clock's, else the renderer's: `GetDrawnWorldTime()`), and its debug
      lines;
      then, per scene view, takes a size asked for (or a new sample count,
      `SetMsaaSamples()`: every view's targets, then the scene pass's pipelines, made again); a
      view drawn only when shown (`SetSceneViewDrawnWhenShown()`) that the last frame's UI didn't
      show stops there, keeping its last picture; for the others, its camera, its world's sun and its air
      (`ExtractSceneEnvironment()`, [Atmosphere](Design/atmosphere.md)), and culls
      (`CullSceneView()`): the objects out of its camera's frustum, and the cells of instances, are
      left out, and what draws alike is batched into this frame's instance stream
      ([Instancing](Design/instancing.md)), and the point lights whose light reaches no part of
      the view and the particle emitters out of it, the alpha blended particles in view sorted back
      to front into this frame's particle stream ([Lights and particles](Design/lights-and-particles.md)); its shadow cascades are fitted and each culled by its
      own frustum the same way (`PrepareSceneViewShadows()`, [Shadows](Design/shadows.md)), and the
      shadow map made as big as the largest view asks for;
      then renders every scene view drawn this frame: the editor's passes (picking only after a click, selection
      masks only when they changed), then per view its sky's tables when what they depend on changed
      (`VulkanSkyAtmosphere`, compute: the air's transmittance and multiple scattering, the sky view
      round its camera, the sky's cube and its light, the cloud layer drawn into the cube while it
      lights the world, its noise made first when its seed changed), its cloud layer
      (`VulkanCloudLayer`, from those tables: the clouds along each pixel's ray, which the sky, the
      surfaces beyond its shell and the light shafts read), its volumetric clouds
      (`VulkanVolumetricClouds`, from those tables: their noises when new, the march at half or the
      view's size of one pixel of each 2 x 2 or 4 x 4 or all of them, and this frame's picture of
      them from it and the frame before's, which the same readers read), its shadow cascades into the
      shadow map, the
      scene in HDR (the objects, the sky, then the decals blended over them, each through the
      height fog; the sky's light apart while it has ambient occlusion; multisampled and resolved
      at the pass's end, [Anti-aliasing](Design/anti-aliasing.md)), its ambient occlusion
      (`VulkanAmbientOcclusion`, the occluded sky's light added), its particles (`VulkanParticles`,
      after the resolve, tested against the scene's resolved depth, which they read, and soft
      where they meet it), its light shafts (`VulkanLightShafts`, from the scene and its depth, in
      the frames its sun gives some), its bloom (`VulkanBloom`), its colour grading's table baked
      again if its grade moved (`VulkanColorGrading`), tonemapped with its light shafts and bloom
      and graded, with the editor's
      overlays (`VulkanEditorOverlays`, in the views that show them) and the world's debug drawing
      (`VulkanDebugLines`) over it; then blurs each drawn view's picture;
   4. builds the UI: an ImGui frame in which the window's dock area and strips (`IWindow::DrawUI()`)
      hold every layer's `OnUI()`, then the painted window
      chrome, the painter canvases, and the window painters' segments, binned on the GPU;
   5. records the UI pass, submits, renders ImGui's OS windows, signals the fence and presents.
6. `InputSystem::SetImGuiState()` with what ImGui wants (`ReadImGuiState()`), for the `ImGui`
   context next frame, then `InputSystem::EndFrame()`: sets the cursor the context stack decides.

After `DrawFrame()`, `Application` adds the frame the renderer completed, with that frame's own
times, to its `FrameStatsWindow` ([Frame statistics](Design/frame-stats.md)).

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
  `DrawFrameOnDevice()` and `VulkanUIBackend`, a shadow cascade's recording each its own
  (*Shadow cascade 0* to *3*). Each frame ends with a frame mark; `main()` names
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
  frame rate limiter, its placement (`GetPlacement()`, `SetPlacement()`: the editor keeps it in its
  layout files), and the startup banner until `FinishStartup()`. It knows nothing of the
  renderer: `Application` hands the renderer the window (`GetNativeWindow()`) and is told when its
  framebuffer changes size.
- **The renderer**: everything on the GPU: scene views, shaders, materials; in its
  `VulkanUIBackend` ImGui, the painter, glass, UI images and screenshots; in its
  `VulkanEditorOverlays` the grid, the objects' bounds, picking and the selection outline; in its
  `VulkanDebugLines` every view's debug lines, the overlays' and the worlds' debug drawing
  (`SetWorldDebugDraw()`). It holds worlds and their drawings only by pointer (`SetWorld()`,
  `SetSceneViewWorld()`): their owner keeps them alive, and calls `SetWorld()` again after
  replacing what's in one. It makes a scene view of its own first, whose render passes its pipelines are
  made with, so the editor's views (any number, `SceneViewSet`) can come and go; nobody shows it,
  so it is never drawn. A view destroyed while a frame is recorded goes once the GPU is done with
  it. The editor's views and the material preview are drawn only when shown
  (`SetSceneViewDrawnWhenShown()`): in the frames after one whose UI asked for their picture, so
  one behind another tab, or in a workspace not shown, costs nothing; the game's view, always
  shown, and tests' views are drawn every frame.
- **Layers**: `EditorLayer` in the editor; `GameLayer` and, when the editor started the game,
  `EditorLinkLayer` in the game, and `BenchmarkLayer` in benchmark mode (`--benchmark`).
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
- number kinds (`Int32`, `UInt32`, `Float`, `Enum`) convert into each other, also an array's
  elements; an enum number no enumerator has keeps the value;
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
   `ComponentRegistration.cpp`. The step runs whenever the component is loaded from a scene
   file (also a pasted entity, or a new one sent live), added, edited, undone or redone in the
   editor, and changed by a live edit in a game. Its `ScenePostLoadContext` has the asset resolver,
   the world and the entity. It may add or change **derived, unsaved** components on that entity,
   such as a run-time mesh made from the component's values, and nothing else: no other entity,
   and no saved component, which undo, scene files and the editor link would not know had
   changed. What it added can be taken away in a `SetOnRemove()` step, which runs when the
   component is removed through its type's ops (the editor's Remove Component, undoing an Add).
5. A component the world makes while it runs and never saves, such as a system's own state, is a
   **run-time** type: a reflected one made so after it is registered (`SetRuntime<T>()`, in
   `ComponentRegistration.cpp` or a game module's `RegisterTypes()`), one that isn't reflected
   registered with `RegisterRuntime<T>(name, displayName, describe)`. Scene files, copies, undo and
   Add Component leave it out; the Details panel shows it greyed in its Runtime section, its
   `Serialize` properties and what its describe step says, from a running game while Play runs
   ([Runtime inspection](Design/runtime-inspection.md)).
6. A component that inherits values from somewhere else, such as a game's data file, and overrides
   some of them holds a `std::vector<ValueOverride>` property with `UI = Overrides`, and its type
   lists what an instance can override with `SetOverridableValues<T>()`, in
   `ComponentRegistration.cpp` or a game module's `RegisterTypes()`. The Details panel then shows
   every value, inherited or overridden ([Overridable values](Design/overridable-values.md)).

### Generated content

A mesh, or anything else made from a component's values, such as a ground or a tree, is derived
data: made from the values wherever they are, in the editor and in the game alike, and never saved.

1. A reflected component holds the values (`GroundComponent`): they are saved, edited, undone and
   sent to a running game as any component's are.
2. Pure functions make the data from the values (`SampleGroundHeight()`, `MakeGroundMesh()`), with
   no state of their own, so tests and tools call them directly. Content that must come out the
   same on every machine uses only correctly rounded arithmetic (no `sin`, `pow` or `exp` of a C
   library, no random distributions) and its own hashes for randomness.
3. The component's post-load step (`SetPostLoad()`) makes the data on its entity, e.g. a
   `RuntimeMeshComponent`, or a `MeshInstancesComponent` of the transforms a mesh is drawn at,
   and raises its revision. It runs on load, on every edit, undo and redo
   in the editor and on every live edit in a game, so the content shows in the editor and follows
   its details panel. It keeps a hash of what it made the data from in an unreflected field
   (`GroundComponent::MadeFrom`) and makes nothing again for the same values: the step also runs
   when another component of the entity changes, and a copied or restored component has no hash,
   so it is made again. Its `SetOnRemove()` step takes the data away with the component.
4. Whatever follows the data goes by its revision: the renderer uploads the mesh again, a Mesh
   Collider makes its body again once the mesh holds still (`PhysicsBodies::MeshSettleSteps`).
5. Nothing else assumes how the data was made: what stands on a ground uses its mesh or rays, not
   its height function.
6. Content made from another entity's data, such as a scatter on a ground's mesh, can't count on
   that entity's step having run first (a scene loads in file order; an edit runs the edited
   entity's steps only), and may only read it. Its step makes what it can, and it follows the other
   entity by its revision and transform once a frame, in the editor and the game, as the renderer
   follows a mesh (`UpdateScatters()`, [Scattering](Design/scattering.md)); the hash includes them.
   Content that is costly to make again whole, such as a scatter of tens of thousands, can keep
   what it made in parts in an unreflected field (`ScatterComponent::Cells`) and make again only
   the parts an edit reaches, as long as each part comes out exactly as a full make gives it.

A simulation system is for what changes as the game runs (a tree growing a stage), and makes its
data the same way, through the same pure functions.

### An asset type

A file of a reflected struct's values, such as a game's data: register it with
`ReflectedAssetTypes::Register()` (a game module in its `RegisterTypes()`, the engine in
`RegisterEngineAssetTypes()`), and the editor lists, opens, edits, makes and sends it with nothing
more ([Reflected assets](Design/reflected-assets.md)). Anything else:

1. Register an `AssetTypeDescriptor` in `RegisterEditorAssetTypes()`
   (`Editor/Source/App/src/EditorAssetTypes.cpp`): its type id, display name, extensions, and
   what opening it does (`Activate`, a callback that captures what it needs). `EditorLayer`
   owns the registry and fills it in `OnAttach()`; the asset browser lists the type from then
   on.
2. For scenes to use it: an `AssetReference` property on a component, resolved in its post-load
   step through `IAssetResolver`. `AssetResolver` resolves materials from their `.nyxmat` files;
   meshes are only the built-in cube and sphere so far. What the simulation reads, the renderer
   needn't see: a collider's physics material is resolved by the game world's
   `PhysicsMaterials` (Sim), by path, when its body is made. A mesh made by code while the game runs
   is no asset: it is a `RuntimeMeshComponent`, made again by its maker after loading.

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
  (the scene pass, then the picture pass) or `UpdateSceneUniforms()` (every drawn view's, each
  frame, with a world or without, so none reads a slot of the bindless array let go of since).
- Per-view GPU resources: made in `CreateSceneResourcesForView()`, made again where
  `PrepareSceneViews()` resizes a view, released in `DestroySceneViewNow()`. What keeps a
  framebuffer or view of the view's targets makes it again when they are new, at the view's next
  draw (`bTargetsNotDrawn`), not in the frame they were made: a view not shown isn't drawn
  (`SetSceneViewDrawnWhenShown()`), and its work each frame belongs after `PrepareSceneViews()`
  skips it.
- What a view draws is its culled draw list (`SceneViewInstance::Draws`, made by
  `CullSceneView()`); a pass that draws the scene from elsewhere, as a shadow cascade does
  (`SceneViewInstance::Shadows`), culls by its own camera with the same function (`CullDraws()`).
- A pipeline that draws into the scene pass is made for the scene views' sample count
  (`GetSceneSamples()`, `VulkanMaterials::ScenePass`) and made again when it changes
  (`RebuildScenePipelines()`); a pass after the scene pass reads the resolved, single-sampled
  targets.
- A pipeline that draws meshes has `MeshDynamicStates` and sets each section's faces before its
  draw (`SetMeshFaces()`: the back culled unless the material is two-sided, the front clockwise for
  a mirrored entity).
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
| `ReflectedAssetTypes::Get()` | `RegisterEngineAssetTypes()`, then the game module's `RegisterTypes()`, in `Application`'s constructor; whoever reads assets of a type listens for their live text while the game runs (`ReflectedAssetListener`) |
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
  and live reload, bindless resources, material files, instances and options, faces and two-sided
  materials, the light materials are judged under.
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
- [Physics constraints and materials](Design/physics-constraints-and-materials.md): each end in
  its body's own space, limits, springs and motors, the load read back from Jolt's lambdas, off and
  on, breaking, and the components a game world keeps them in step with; physics material assets
  with a game's own numbers, how they reach the bodies, and how the editor edits them live.
- [Reflected assets](Design/reflected-assets.md): a game's own data files registered with a
  reflected struct, as `Name = value` text that keeps its comments, named entries and the rest of
  the shapes, problems at their properties; the editor's windows, undo, New, files read again, and
  the live values a game listens for; a creature's stats as the worked example.
- [Instancing and culling](Design/instancing.md): a mesh drawn at many transforms as a second
  vertex stream, frustum culling per view and per cell, entities that draw alike batched, the
  numbers.
- [Vertex data](Design/vertex-data.md): numbers of a game's own for the materials, a mesh's for
  each vertex and an instance's or an entity's for each draw, their sizes on the GPU, and the time;
  a material's vertex function, which moves its meshes in the scene, the shadows, picking and the
  outline alike, compiled as two more entry points of its domain; the wind as the example.
- [Wind](Design/wind.md): one wind for a world that grass, trees, particles, clouds and a game's own
  air read, through a small plain-data contract (`WindState`, `SampleWind()`, the same to the bit
  on the GPU); gust fronts travelling over the land, a height profile; driven by its values and
  turning to new ones smoothly, the phases integrated exactly and the patterns faded; a game's
  local air on top, in a texture round the camera for the grass; who depends on whom; what is left
  for the weather to come.
- [Shadows](Design/shadows.md): the primary light's cascaded shadow maps, stable as the camera
  moves, one map for every view, culled per cascade, filtering and biases, the numbers.
- [Atmosphere](Design/atmosphere.md): the world's air on one component, height fog integrated
  exactly along each ray in the surface and sky shaders, decals over fog, a sky and a light that
  follow the sun's height through the air; the sky made by the air's scattering (Hillaire's
  tables, made again as the sun or the camera's height changes), lighting the world and colouring
  the fog, right from a camera kilometres up; bloom, a chain of halvings and back per view between
  the scene pass and the tonemap; ground truth ambient occlusion at half size on the sky's light
  alone, written apart by the scene pass; light shafts, a radial blur from the sun's place on the
  picture added with the bloom, and their limits next to volumetric fog; the high cloud layer, a
  shell round the planet met per pixel from below, at and above it, shaped by seeded noise carried
  by the wind with the world's time, lit by the sun through the air at its height; the volumetric
  clouds, a band marched along each ray at half size, one pixel of each 4 x 4 a frame and the rest
  carried, placed by a weather map, shaped by 3D noise, lit through themselves and scattered many
  times; colour grading after the tonemap through a lookup table
  per view, baked from grades blended by the sun's height; the numbers.
- [Lights and particles](Design/lights-and-particles.md): point lights, each view's list of those
  that reach it, culled by its frustum, gone through by every lit pixel, their falloff to nothing
  at the range, when tiles or clusters of lights would be needed and how they would slot in, how
  an emissive surface is paired with a light; particles moved on the CPU by the world's time, only
  for the eye, the same from the same seed, carried by a game's air, budgets, drawn instanced in a
  pass of their own after the ambient occlusion, sorted where needed, soft against the scene's
  depth; the numbers.
- [Anti-aliasing](Design/anti-aliasing.md): multisample anti-aliasing in the scene pass, one
  sample count for the renderer, the colours and the nearest depth resolved before anything reads
  them, picking left exact; why not alpha to coverage yet, what TAA would add and why it was left;
  the numbers.
- [Undo by guid](Design/undo-by-guid.md): why undo names entities by guid, and how it keeps
  components whole.
- [Frame statistics](Design/frame-stats.md): each frame's CPU and GPU times and draw counts, made
  whole one frame late after its fence, windows of a second's average and worst without
  allocating, the game's overlay, benchmark mode, its CSV and camera path, what doesn't repeat
  between runs, the numbers.
- [Scattering](Design/scattering.md): copies of a mesh over another entity's mesh, sampled by
  triangle area cell by cell, the same on every machine, made in the post-load step and following
  the surface and the blockers each frame, an edit making only the cells it reaches again;
  blockers and a noise mask, painted masks later.
- [Stroke recognition](Design/stroke-recognition.md): shapes drawn by hand as point clouds ($P),
  whatever their stroke order and direction, turned or as drawn, the same result on every machine,
  the cost, the template text.
- [Sparse fields](Design/sparse-fields.md): a number or a vector over the world in bricks of cells
  where something happens, a hard cap on bricks and who is dropped at it, diffusion, decay and
  advection, deterministic and double buffered, the cost at the cap.
- [Runtime inspection](Design/runtime-inspection.md): run-time component types, never saved; the
  editor asking a running game about an entity a few times a second, the game answering at once
  and marking the entity while asked, and the Details panel's Runtime section.
