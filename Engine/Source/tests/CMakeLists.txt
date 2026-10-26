# Enum and Array properties: the header tool's enum_and_array fixture compiled with its generated
# code (the golden file), through PropertyValue, the archive serializer and the printer.
add_executable(NyxReflectionKindsTests ReflectionKindsTests.cpp)
target_include_directories(NyxReflectionKindsTests PRIVATE "${PROJECT_SOURCE_DIR}/Tools/NyxHeaderTool/tests/fixtures/enum_and_array")
target_link_libraries(NyxReflectionKindsTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxReflectionKindsTests NyxGeneratedReflection)
set_property(TARGET NyxReflectionKindsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxReflectionKindsTests COMMAND NyxReflectionKindsTests)

# Headless file tests: they exercise the same writer used by scene saving.
add_executable(NyxArchiveTests
    ArchiveTests.cpp
)

target_link_libraries(NyxArchiveTests PRIVATE NyxEngine)
add_dependencies(NyxArchiveTests NyxGeneratedReflection)
set_property(TARGET NyxArchiveTests PROPERTY FOLDER "Tests")

add_test(NAME NyxArchiveTests COMMAND NyxArchiveTests)

add_executable(NyxRuntimeInspectionTests RuntimeInspectionTests.cpp)
target_link_libraries(NyxRuntimeInspectionTests PRIVATE NyxEngine)
add_dependencies(NyxRuntimeInspectionTests NyxGeneratedReflection)
set_property(TARGET NyxRuntimeInspectionTests PROPERTY FOLDER "Tests")
add_test(NAME NyxRuntimeInspectionTests COMMAND NyxRuntimeInspectionTests)

# The editor link over real sockets on 127.0.0.1, with both ends in one program.
add_executable(NyxEditorLinkTests EditorLinkTests.cpp)
target_link_libraries(NyxEditorLinkTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxEditorLinkTests NyxGeneratedReflection)
set_property(TARGET NyxEditorLinkTests PROPERTY FOLDER "Tests")
add_test(NAME NyxEditorLinkTests COMMAND NyxEditorLinkTests)

# Live edits: messages made from one world and applied to another, as the editor and the game do.
add_executable(NyxLiveEditTests LiveEditTests.cpp)
target_link_libraries(NyxLiveEditTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxLiveEditTests NyxGeneratedReflection)
set_property(TARGET NyxLiveEditTests PROPERTY FOLDER "Tests")
add_test(NAME NyxLiveEditTests COMMAND NyxLiveEditTests)

# Live tuning, both ends in one program: the editor's input asset applied in a game while a
# button is held, pads driving a game without the focus
add_executable(NyxLiveInputTests LiveInputTests.cpp)
target_link_libraries(NyxLiveInputTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxLiveInputTests NyxGeneratedReflection)
set_property(TARGET NyxLiveInputTests PROPERTY FOLDER "Tests")
add_test(NAME NyxLiveInputTests COMMAND NyxLiveInputTests)

# The game's command line, as the editor writes it and the game reads it.
add_executable(NyxGameLaunchOptionsTests GameLaunchOptionsTests.cpp)
target_link_libraries(NyxGameLaunchOptionsTests PRIVATE NyxEngine)
add_dependencies(NyxGameLaunchOptionsTests NyxGeneratedReflection)
set_property(TARGET NyxGameLaunchOptionsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxGameLaunchOptionsTests COMMAND NyxGameLaunchOptionsTests)

# Frame statistics without a GPU: a frame's numbers, their windows' averages and worst, the graph's
# history; a benchmark's summary, its CSV and its camera path.
add_executable(NyxFrameStatsTests FrameStatsTests.cpp)
target_link_libraries(NyxFrameStatsTests PRIVATE NyxEngine)
add_dependencies(NyxFrameStatsTests NyxGeneratedReflection)
set_property(TARGET NyxFrameStatsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxFrameStatsTests COMMAND NyxFrameStatsTests)

# The frame rate cap's pace, and its waiting on the real clock.
add_executable(NyxFrameRateLimiterTests FrameRateLimiterTests.cpp)
target_link_libraries(NyxFrameRateLimiterTests PRIVATE NyxEngine)
add_dependencies(NyxFrameRateLimiterTests NyxGeneratedReflection)
set_property(TARGET NyxFrameRateLimiterTests PROPERTY FOLDER "Tests")
add_test(NAME NyxFrameRateLimiterTests COMMAND NyxFrameRateLimiterTests)

# The fixed simulation tick: steps per frame at any frame rate, the cap, pause and step, time scale.
add_executable(NyxFixedStepClockTests FixedStepClockTests.cpp)
target_link_libraries(NyxFixedStepClockTests PRIVATE NyxEngine)
add_dependencies(NyxFixedStepClockTests NyxGeneratedReflection)
set_property(TARGET NyxFixedStepClockTests PROPERTY FOLDER "Tests")
add_test(NAME NyxFixedStepClockTests COMMAND NyxFixedStepClockTests)

# The component type registry: types register once; a second type of a name already taken, such as
# a game module's type named like one of the engine's, is refused.
add_executable(NyxComponentTypeRegistryTests ComponentTypeRegistryTests.cpp)
target_link_libraries(NyxComponentTypeRegistryTests PRIVATE NyxEngine)
add_dependencies(NyxComponentTypeRegistryTests NyxGeneratedReflection)
set_property(TARGET NyxComponentTypeRegistryTests PROPERTY FOLDER "Tests")
add_test(NAME NyxComponentTypeRegistryTests COMMAND NyxComponentTypeRegistryTests)

# Where things are: the roots file next to the programs, assets in the project's folder first
add_executable(NyxPathsTests PathsTests.cpp)
target_link_libraries(NyxPathsTests PRIVATE NyxEngine)
add_dependencies(NyxPathsTests NyxGeneratedReflection)
set_property(TARGET NyxPathsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPathsTests COMMAND NyxPathsTests)

# A game module made and linked the way a game makes and links its own, through the same functions:
# NyxTestModule's component registers next to the engine's and saves in scenes. The editor's tests
# link the module too.
nyx_add_game_module(NyxTestModule
    REFLECT_DIRS TestGameModule/Components
    SOURCES TestGameModule/TestGameModule.cpp
    REGISTER NyxTestModule::RegisterModule
    SYSTEMS NyxTestModule::CreateSystems
    PLAYER NyxTestModule::CreatePlayer)
# Reflected assets on the test module's creature: every shape written and read back, hand-written
# files kept through edits, problems at their properties, the registry, live text over a real link
add_executable(NyxReflectedAssetTests ReflectedAssetTests.cpp)
target_link_libraries(NyxReflectedAssetTests PRIVATE NyxEngine NyxLinkTools)
nyx_link_game_module(NyxReflectedAssetTests MODULE NyxTestModule)
set_property(TARGET NyxReflectedAssetTests PROPERTY FOLDER "Tests")
add_test(NAME NyxReflectedAssetTests COMMAND NyxReflectedAssetTests)

add_executable(NyxGameModuleTests GameModuleTests.cpp)
target_link_libraries(NyxGameModuleTests PRIVATE NyxEngine)
nyx_link_game_module(NyxGameModuleTests MODULE NyxTestModule)
set_property(TARGET NyxGameModuleTests PROPERTY FOLDER "Tests")
add_test(NAME NyxGameModuleTests COMMAND NyxGameModuleTests)

# Surface anchors: following a host that moves, turns and is scaled, made and resolved back, without
# a host, the tangent from world X
add_executable(NyxSurfaceAnchorTests SurfaceAnchorTests.cpp)
target_link_libraries(NyxSurfaceAnchorTests PRIVATE NyxEngine)
add_dependencies(NyxSurfaceAnchorTests NyxGeneratedReflection)
set_property(TARGET NyxSurfaceAnchorTests PROPERTY FOLDER "Tests")
add_test(NAME NyxSurfaceAnchorTests COMMAND NyxSurfaceAnchorTests)

# Surface decals: the quad following its host, what the renderer extracts of a world's decals, a
# decal saved and loaded, the time a game's world's decals pulse by
add_executable(NyxSurfaceDecalTests SurfaceDecalTests.cpp)
target_link_libraries(NyxSurfaceDecalTests PRIVATE NyxEngine)
add_dependencies(NyxSurfaceDecalTests NyxGeneratedReflection)
set_property(TARGET NyxSurfaceDecalTests PROPERTY FOLDER "Tests")
add_test(NAME NyxSurfaceDecalTests COMMAND NyxSurfaceDecalTests)

# The cursor ray: a pixel back to a ray through the world, by the projection the renderer draws
# with, the physics pick along it, an anchor where it hit
add_executable(NyxCursorRayTests CursorRayTests.cpp)
target_link_libraries(NyxCursorRayTests PRIVATE NyxEngine)
add_dependencies(NyxCursorRayTests NyxGeneratedReflection)
set_property(TARGET NyxCursorRayTests PROPERTY FOLDER "Tests")
add_test(NAME NyxCursorRayTests COMMAND NyxCursorRayTests)

# Debug drawing: shapes as lines, how long they stay, the cap
add_executable(NyxDebugDrawTests DebugDrawTests.cpp)
target_link_libraries(NyxDebugDrawTests PRIVATE NyxEngine)
add_dependencies(NyxDebugDrawTests NyxGeneratedReflection)
set_property(TARGET NyxDebugDrawTests PROPERTY FOLDER "Tests")
add_test(NAME NyxDebugDrawTests COMMAND NyxDebugDrawTests)

# The point-cloud recogniser: known shapes drawn again in other stroke orders, turned, a scribble
# rejected, the same result every time, the template text, 50 templates under 1 ms in Release
add_executable(NyxPointCloudRecognizerTests PointCloudRecognizerTests.cpp)
target_link_libraries(NyxPointCloudRecognizerTests PRIVATE NyxEngine)
add_dependencies(NyxPointCloudRecognizerTests NyxGeneratedReflection)
set_property(TARGET NyxPointCloudRecognizerTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPointCloudRecognizerTests COMMAND NyxPointCloudRecognizerTests)

# The sparse field: a bump spreading and decaying, carried downwind, stable at long steps, the cap
# on bricks, the same values over 600 steps, the ambient outside the bricks, the cost at the cap
add_executable(NyxSparseFieldTests SparseFieldTests.cpp)
target_link_libraries(NyxSparseFieldTests PRIVATE NyxEngine)
add_dependencies(NyxSparseFieldTests NyxGeneratedReflection)
set_property(TARGET NyxSparseFieldTests PROPERTY FOLDER "Tests")
add_test(NAME NyxSparseFieldTests COMMAND NyxSparseFieldTests)

# Physics: bodies falling and resting, mass, forces at a point, teleports, kinematic moves, the
# same results from the same inputs, queries, sensors, contact events, constraints, the debug view
add_executable(NyxPhysicsTests PhysicsTests.cpp)
target_link_libraries(NyxPhysicsTests PRIVATE NyxEngine)
add_dependencies(NyxPhysicsTests NyxGeneratedReflection)
set_property(TARGET NyxPhysicsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPhysicsTests COMMAND NyxPhysicsTests)

# Physics in the game world: bodies made, moved and removed with their components, transforms
# written back, live edits teleporting, kinematic bodies, systems around the physics step, the
# previous poses moving bodies, dynamic and kinematic, are drawn between steps from
add_executable(NyxPhysicsBodiesTests PhysicsBodiesTests.cpp)
target_link_libraries(NyxPhysicsBodiesTests PRIVATE NyxEngine)
add_dependencies(NyxPhysicsBodiesTests NyxGeneratedReflection)
set_property(TARGET NyxPhysicsBodiesTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPhysicsBodiesTests COMMAND NyxPhysicsBodiesTests)

# Constraints in the game world: held at their rest, their load read back, switched off and on,
# breaking, two bodies joined by a third entity, made again when they or their bodies change, motors
# driven each step, scene files, live edits and the same steps from the same scene
add_executable(NyxConstraintTests ConstraintTests.cpp)
target_link_libraries(NyxConstraintTests PRIVATE NyxEngine)
add_dependencies(NyxConstraintTests NyxGeneratedReflection)
set_property(TARGET NyxConstraintTests PROPERTY FOLDER "Tests")
add_test(NAME NyxConstraintTests COMMAND NyxConstraintTests)

# Physics materials: .nyxphysmat files read and written with their unknown lines and extras kept,
# friction deciding a slide down a slope and changed live, density giving the mass, a game's own
# numbers served, scene files
add_executable(NyxPhysicsMaterialTests PhysicsMaterialTests.cpp)
target_link_libraries(NyxPhysicsMaterialTests PRIVATE NyxEngine)
add_dependencies(NyxPhysicsMaterialTests NyxGeneratedReflection)
set_property(TARGET NyxPhysicsMaterialTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPhysicsMaterialTests COMMAND NyxPhysicsMaterialTests)

# Characters in the game world: falling, walking, steps, slopes, jumping, pushing, rays, live edits,
# drawing between steps, the debug view, scene files and the same path from the same inputs
add_executable(NyxCharacterTests CharacterTests.cpp)
target_link_libraries(NyxCharacterTests PRIVATE NyxEngine)
add_dependencies(NyxCharacterTests NyxGeneratedReflection)
set_property(TARGET NyxCharacterTests PROPERTY FOLDER "Tests")
add_test(NAME NyxCharacterTests COMMAND NyxCharacterTests)

# Mesh colliders in the game world: boxes, rays and characters on meshes made at run time, the body
# made again only when the mesh changes, the debug view's edges.
add_executable(NyxMeshColliderTests MeshColliderTests.cpp)
target_link_libraries(NyxMeshColliderTests PRIVATE NyxEngine)
add_dependencies(NyxMeshColliderTests NyxGeneratedReflection)
set_property(TARGET NyxMeshColliderTests PROPERTY FOLDER "Tests")
add_test(NAME NyxMeshColliderTests COMMAND NyxMeshColliderTests)

# The ground (GroundComponent) without a GPU: its heights the same every time and smooth, its mesh,
# colours by slope and height, made in its post-load step as the editor and the game run it, a
# character standing and a crate rolling on it, the sample scene. --write-scene writes
# Assets/Scenes/GroundTest.nyxscene.
add_executable(NyxGroundTests GroundTests.cpp)
target_link_libraries(NyxGroundTests PRIVATE NyxEngine)
add_dependencies(NyxGroundTests NyxGeneratedReflection)
set_property(TARGET NyxGroundTests PROPERTY FOLDER "Tests")
add_test(NAME NyxGroundTests COMMAND NyxGroundTests)

# Scattering (ScatterComponent) without a GPU: the same transforms every time, the density, spacing,
# slope and height limits, instances on their surface, the cells, the post-load step and a surface
# loaded after its scatter; blockers and their falloff, edits making only their cells again, the
# same as a full scatter, the noise mask, saving and loading; scattering about 50,000 instances on
# the default ground, and dragging a blocker over 40,000, are timed.
add_executable(NyxScatterTests ScatterTests.cpp)
target_link_libraries(NyxScatterTests PRIVATE NyxEngine)
add_dependencies(NyxScatterTests NyxGeneratedReflection)
set_property(TARGET NyxScatterTests PROPERTY FOLDER "Tests")
add_test(NAME NyxScatterTests COMMAND NyxScatterTests)

# How long a physics step takes with 1000 bodies; not run by ctest, timing depends on the machine
add_executable(NyxPhysicsBenchmark PhysicsBenchmark.cpp)
target_link_libraries(NyxPhysicsBenchmark PRIVATE NyxEngine)
add_dependencies(NyxPhysicsBenchmark NyxGeneratedReflection)
set_property(TARGET NyxPhysicsBenchmark PROPERTY FOLDER "Tests")

# The game's fly camera: only entities with a FlyCameraComponent fly, its contexts only while one
# exists, and the component is saved with the scene. No window needed.
add_executable(NyxFlyCameraTests FlyCameraTests.cpp)
target_link_libraries(NyxFlyCameraTests PRIVATE NyxEngine)
add_dependencies(NyxFlyCameraTests NyxGeneratedReflection)
set_property(TARGET NyxFlyCameraTests PROPERTY FOLDER "Tests")
add_test(NAME NyxFlyCameraTests COMMAND NyxFlyCameraTests)

# The orbit camera: following a moving target where it is drawn, smoothing, pitch and zoom limits,
# yaw wrapping, the ground helpers, and the component in scene files. No window needed.
add_executable(NyxOrbitCameraTests OrbitCameraTests.cpp)
target_link_libraries(NyxOrbitCameraTests PRIVATE NyxEngine)
add_dependencies(NyxOrbitCameraTests NyxGeneratedReflection)
set_property(TARGET NyxOrbitCameraTests PROPERTY FOLDER "Tests")
add_test(NAME NyxOrbitCameraTests COMMAND NyxOrbitCameraTests)

# The orbit camera's input: its contexts only with an orbit camera, holding the right mouse button
# or free look asked for by a context, the stick and the wheel, the default asset. No window needed.
add_executable(NyxOrbitCameraInputTests OrbitCameraInputTests.cpp)
target_link_libraries(NyxOrbitCameraInputTests PRIVATE NyxEngine)
add_dependencies(NyxOrbitCameraInputTests NyxGeneratedReflection)
set_property(TARGET NyxOrbitCameraInputTests PROPERTY FOLDER "Tests")
add_test(NAME NyxOrbitCameraInputTests COMMAND NyxOrbitCameraInputTests)

# Walking a character through commands: a walk relative to the camera, the command in the next
# step, the last move and one jump of several, the built-in player on the sample scene. No window.
add_executable(NyxThirdPersonTests ThirdPersonTests.cpp)
target_link_libraries(NyxThirdPersonTests PRIVATE NyxEngine)
add_dependencies(NyxThirdPersonTests NyxGeneratedReflection)
set_property(TARGET NyxThirdPersonTests PROPERTY FOLDER "Tests")
add_test(NAME NyxThirdPersonTests COMMAND NyxThirdPersonTests)

# How long the guid lookup a game world makes each step takes with 3000 guids; not run by ctest,
# timing depends on the machine
add_executable(NyxGuidLookupBenchmark GuidLookupBenchmark.cpp)
target_link_libraries(NyxGuidLookupBenchmark PRIVATE NyxEngine)
add_dependencies(NyxGuidLookupBenchmark NyxGeneratedReflection)
set_property(TARGET NyxGuidLookupBenchmark PROPERTY FOLDER "Tests")

# The game world: systems run in their order, every step or every nth one; the guid lookup made
# each step
add_executable(NyxGameWorldTests GameWorldTests.cpp)
target_link_libraries(NyxGameWorldTests PRIVATE NyxEngine)
add_dependencies(NyxGameWorldTests NyxGeneratedReflection)
set_property(TARGET NyxGameWorldTests PROPERTY FOLDER "Tests")
add_test(NAME NyxGameWorldTests COMMAND NyxGameWorldTests)

# The input system with virtual devices: frames, taps shorter than a frame, devices coming and
# going, focus, the cursor. No window needed.
add_executable(NyxInputTests InputTests.cpp)
target_link_libraries(NyxInputTests PRIVATE NyxEngine)
add_dependencies(NyxInputTests NyxGeneratedReflection)
set_property(TARGET NyxInputTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputTests COMMAND NyxInputTests)

# Contexts, actions, the value pipeline, triggers and traces, with virtual devices and frames at
# set times. No window needed.
add_executable(NyxInputRouterTests InputRouterTests.cpp)
target_link_libraries(NyxInputRouterTests PRIVATE NyxEngine)
add_dependencies(NyxInputRouterTests NyxGeneratedReflection)
set_property(TARGET NyxInputRouterTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputRouterTests COMMAND NyxInputRouterTests)

# Gamepads with virtual pads: routing, output and what pads can do, the DualSense's trigger effect
# bytes, the fly camera's bindings. No pad needed.
add_executable(NyxInputGamepadTests InputGamepadTests.cpp)
target_link_libraries(NyxInputGamepadTests PRIVATE NyxEngine)
add_dependencies(NyxInputGamepadTests NyxGeneratedReflection)
set_property(TARGET NyxInputGamepadTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputGamepadTests COMMAND NyxInputGamepadTests)

# What the Input window asks: who gets a control with a stack, every binding of a control, stick
# calibration, the history of the last frames. No window needed.
add_executable(NyxInputInspectionTests InputInspectionTests.cpp)
target_link_libraries(NyxInputInspectionTests PRIVATE NyxEngine)
add_dependencies(NyxInputInspectionTests NyxGeneratedReflection)
set_property(TARGET NyxInputInspectionTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputInspectionTests COMMAND NyxInputInspectionTests)

# Input assets: saving and reading back, problems with where they are, tuning layers and the
# router using a pad model's variant. No window needed.
add_executable(NyxInputAssetTests InputAssetTests.cpp)
target_link_libraries(NyxInputAssetTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxInputAssetTests NyxGeneratedReflection)
set_property(TARGET NyxInputAssetTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputAssetTests COMMAND NyxInputAssetTests)

# Finding the right setup: each check with and without its mistake, every recipe passing every
# check, tuning explained in units. No window needed.
add_executable(NyxInputChecksTests InputChecksTests.cpp)
target_link_libraries(NyxInputChecksTests PRIVATE NyxEngine)
add_dependencies(NyxInputChecksTests NyxGeneratedReflection)
set_property(TARGET NyxInputChecksTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputChecksTests COMMAND NyxInputChecksTests)

# An input system's frames as records, and a replica deciding every frame as the original did,
# as the editor shows a running game's input. No window needed.
add_executable(NyxInputStreamTests InputStreamTests.cpp)
target_link_libraries(NyxInputStreamTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxInputStreamTests NyxGeneratedReflection)
set_property(TARGET NyxInputStreamTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputStreamTests COMMAND NyxInputStreamTests)

# Input recordings: a session recorded and replayed from its file decides every frame as the
# original did; cut off files; the printout. No window needed.
add_executable(NyxInputRecordingTests InputRecordingTests.cpp)
target_link_libraries(NyxInputRecordingTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxInputRecordingTests NyxGeneratedReflection)
set_property(TARGET NyxInputRecordingTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputRecordingTests COMMAND NyxInputRecordingTests)

# The painter's CPU side: draw commands, groups, clip and transform stacks, tile binning. No GPU needed.
# The painter's test sheet is a dev tool, not part of the engine library, so the painter tests
# compile it themselves.
add_executable(NyxPainterTests PainterTests.cpp ${NYX_ENGINE_ROOT}/DevTools/src/PainterTestSheet.cpp)
target_include_directories(NyxPainterTests PRIVATE ${NYX_ENGINE_ROOT}/DevTools/include)
target_link_libraries(NyxPainterTests PRIVATE NyxEngine)
add_dependencies(NyxPainterTests NyxGeneratedReflection)
set_property(TARGET NyxPainterTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPainterTests COMMAND NyxPainterTests)

# The painted window chrome: cards and splitters in a headless ImGui context with a dock space.
add_executable(NyxUIChromeTests UIChromeTests.cpp)
target_link_libraries(NyxUIChromeTests PRIVATE NyxEngine)
add_dependencies(NyxUIChromeTests NyxGeneratedReflection)
set_property(TARGET NyxUIChromeTests PROPERTY FOLDER "Tests")
add_test(NAME NyxUIChromeTests COMMAND NyxUIChromeTests)

# The theme's values by name and the theme file: every member listed, reading back what was written.
add_executable(NyxUIThemeTests UIThemeTests.cpp)
target_link_libraries(NyxUIThemeTests PRIVATE NyxEngine)
add_dependencies(NyxUIThemeTests NyxGeneratedReflection)
set_property(TARGET NyxUIThemeTests PROPERTY FOLDER "Tests")
add_test(NAME NyxUIThemeTests COMMAND NyxUIThemeTests)

# The painter shader on a real GPU: paints small frames and checks pixels read back. Needs a
# Vulkan device; its window stays hidden.
add_executable(NyxPainterGpuTests PainterGpuTests.cpp ${NYX_ENGINE_ROOT}/DevTools/src/PainterTestSheet.cpp)
target_link_libraries(NyxPainterGpuTests PRIVATE NyxEngine)
# stb_image_write.h, which GLFW ships, saves painted images when NYX_PAINTER_TEST_OUTPUT is set.
target_include_directories(NyxPainterGpuTests PRIVATE "${PROJECT_SOURCE_DIR}/Dependencies/glfw/deps")
target_include_directories(NyxPainterGpuTests PRIVATE ${NYX_ENGINE_ROOT}/DevTools/include)
add_dependencies(NyxPainterGpuTests NyxGeneratedReflection)
set_property(TARGET NyxPainterGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxPainterGpuTests)
nyx_copy_shader_compiler_next_to(NyxPainterGpuTests)
add_test(NAME NyxPainterGpuTests COMMAND NyxPainterGpuTests)

# SPIRV-Tools' validator, for the shader compiler tests. Its release and debug builds both come with the SDK.
find_library(NYX_SPIRV_TOOLS_LIBRARY SPIRV-Tools HINTS "${NYX_VULKAN_LIB_DIR}" REQUIRED)
find_library(NYX_SPIRV_TOOLS_DEBUG_LIBRARY SPIRV-Toolsd HINTS "${NYX_VULKAN_LIB_DIR}" REQUIRED)

# glslc's SPIR-V of the engine's shaders, for comparing with Slang's in NyxShaderCompilerTests: the
# build compiles with Slang, and this is the only use of glslc left.
find_program(NYX_GLSLC glslc HINTS "${NYX_VULKAN_LIB_DIR}/../Bin" REQUIRED)
set(NYX_GLSLC_REFERENCE_DIR "${CMAKE_CURRENT_BINARY_DIR}/GlslcReference")
file(GLOB NYX_ENGINE_GLSL CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/Engine/Shaders/*.vert"
    "${PROJECT_SOURCE_DIR}/Engine/Shaders/*.frag"
    "${PROJECT_SOURCE_DIR}/Engine/Shaders/*.comp"
)
set(NYX_GLSLC_REFERENCES)
foreach(NYX_SHADER ${NYX_ENGINE_GLSL})
    get_filename_component(NYX_SHADER_NAME "${NYX_SHADER}" NAME)
    add_custom_command(
        OUTPUT "${NYX_GLSLC_REFERENCE_DIR}/${NYX_SHADER_NAME}.spv"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NYX_GLSLC_REFERENCE_DIR}"
        COMMAND "${NYX_GLSLC}" "${NYX_SHADER}" -o "${NYX_GLSLC_REFERENCE_DIR}/${NYX_SHADER_NAME}.spv"
        DEPENDS "${NYX_SHADER}"
        VERBATIM
    )
    list(APPEND NYX_GLSLC_REFERENCES "${NYX_GLSLC_REFERENCE_DIR}/${NYX_SHADER_NAME}.spv")
endforeach()
add_custom_target(NyxGlslcReference DEPENDS ${NYX_GLSLC_REFERENCES})
set_property(TARGET NyxGlslcReference PROPERTY FOLDER "Tests")

# ShaderCompiler without a GPU: materials, layouts, diagnostics, dependencies, and the engine's GLSL
# compiled by Slang with the same layouts as glslc's.
add_executable(NyxShaderCompilerTests ShaderCompilerTests.cpp)
target_link_libraries(NyxShaderCompilerTests PRIVATE NyxEngine optimized "${NYX_SPIRV_TOOLS_LIBRARY}" debug "${NYX_SPIRV_TOOLS_DEBUG_LIBRARY}")
# The SDK ships SPIRV-Toolsd.lib without its PDB
target_link_options(NyxShaderCompilerTests PRIVATE "/IGNORE:4099")
target_compile_definitions(NyxShaderCompilerTests PRIVATE
    NYX_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    NYX_ENGINE_SHADERS_DIR="${PROJECT_SOURCE_DIR}/Engine/Shaders"
    NYX_GLSLC_SHADERS_DIR="${NYX_GLSLC_REFERENCE_DIR}"
)
add_dependencies(NyxShaderCompilerTests NyxGeneratedReflection NyxGlslcReference)
set_property(TARGET NyxShaderCompilerTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxShaderCompilerTests)
add_test(NAME NyxShaderCompilerTests COMMAND NyxShaderCompilerTests)

# ShaderCompiler on a real GPU: material layouts as the GPU loads and stores them, the model matrix,
# and defaults from the generated writer. Needs a Vulkan device; its window stays hidden.
add_executable(NyxShaderCompilerGpuTests ShaderCompilerGpuTests.cpp)
target_link_libraries(NyxShaderCompilerGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxShaderCompilerGpuTests PRIVATE NYX_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
add_dependencies(NyxShaderCompilerGpuTests NyxGeneratedReflection)
set_property(TARGET NyxShaderCompilerGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxShaderCompilerGpuTests)
add_test(NAME NyxShaderCompilerGpuTests COMMAND NyxShaderCompilerGpuTests)

# FileWatcher on a real folder in the temp folder.
add_executable(NyxFileWatcherTests FileWatcherTests.cpp)
target_link_libraries(NyxFileWatcherTests PRIVATE NyxEngine)
add_dependencies(NyxFileWatcherTests NyxGeneratedReflection)
set_property(TARGET NyxFileWatcherTests PROPERTY FOLDER "Tests")
add_test(NAME NyxFileWatcherTests COMMAND NyxFileWatcherTests)

# ShaderCache on real files, and NyxShaderCompiler's build mode filling one.
add_executable(NyxShaderCacheTests ShaderCacheTests.cpp)
target_link_libraries(NyxShaderCacheTests PRIVATE NyxEngine)
add_dependencies(NyxShaderCacheTests NyxGeneratedReflection NyxShaderCompiler)
set_property(TARGET NyxShaderCacheTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxShaderCacheTests)
add_test(NAME NyxShaderCacheTests COMMAND NyxShaderCacheTests)

# ShaderPrograms on real files, compiling in this process: settling, last good code, includes.
add_executable(NyxShaderProgramsTests ShaderProgramsTests.cpp)
target_link_libraries(NyxShaderProgramsTests PRIVATE NyxEngine)
add_dependencies(NyxShaderProgramsTests NyxGeneratedReflection)
set_property(TARGET NyxShaderProgramsTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxShaderProgramsTests)
add_test(NAME NyxShaderProgramsTests COMMAND NyxShaderProgramsTests)

# Compiling in NyxShaderCompiler.exe, crashes of it included.
add_executable(NyxShaderCompileServiceTests ShaderCompileServiceTests.cpp)
target_link_libraries(NyxShaderCompileServiceTests PRIVATE NyxEngine)
target_compile_definitions(NyxShaderCompileServiceTests PRIVATE NYX_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
add_dependencies(NyxShaderCompileServiceTests NyxGeneratedReflection NyxShaderCompiler)
set_property(TARGET NyxShaderCompileServiceTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxShaderCompileServiceTests)
add_test(NAME NyxShaderCompileServiceTests COMMAND NyxShaderCompileServiceTests)

# The built-in meshes' tangents, and meshes as data (validity, sections, bounds), without a GPU.
add_executable(NyxMeshTests MeshTests.cpp)
target_link_libraries(NyxMeshTests PRIVATE NyxEngine)
add_dependencies(NyxMeshTests NyxGeneratedReflection)
set_property(TARGET NyxMeshTests PROPERTY FOLDER "Tests")
add_test(NAME NyxMeshTests COMMAND NyxMeshTests)

# Culling without a GPU: a camera's frustum, boxes inside, outside and across it, the cells of mesh
# instances and the runs of instances drawn of them.
add_executable(NyxCullingTests CullingTests.cpp)
target_link_libraries(NyxCullingTests PRIVATE NyxEngine)
add_dependencies(NyxCullingTests NyxGeneratedReflection)
set_property(TARGET NyxCullingTests PROPERTY FOLDER "Tests")
add_test(NAME NyxCullingTests COMMAND NyxCullingTests)

# Point lights without a GPU: the falloff to 0 at the range, a world's lights as a frame draws them,
# and a view's culling of them by its frustum and past the most it is lit by.
add_executable(NyxPointLightTests PointLightTests.cpp)
target_link_libraries(NyxPointLightTests PRIVATE NyxEngine)
add_dependencies(NyxPointLightTests NyxGeneratedReflection)
set_property(TARGET NyxPointLightTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPointLightTests COMMAND NyxPointLightTests)

# Particles without a GPU: an emitter stepped as the renderer steps it, the same with the same seed,
# lifetimes, the rate and bursts, budgets, the air and a game's velocity field, the keys, the order
# alpha particles are drawn in, a world's emitters.
add_executable(NyxParticleTests ParticleTests.cpp)
target_link_libraries(NyxParticleTests PRIVATE NyxEngine)
add_dependencies(NyxParticleTests NyxGeneratedReflection)
set_property(TARGET NyxParticleTests PROPERTY FOLDER "Tests")
add_test(NAME NyxParticleTests COMMAND NyxParticleTests)

# The directional light's cascades without a GPU: splits, the spheres round the view's slices, the
# light's view of them, snapped to their texels.
add_executable(NyxShadowCascadeTests ShadowCascadeTests.cpp)
target_link_libraries(NyxShadowCascadeTests PRIVATE NyxEngine)
add_dependencies(NyxShadowCascadeTests NyxGeneratedReflection)
set_property(TARGET NyxShadowCascadeTests PROPERTY FOLDER "Tests")
add_test(NAME NyxShadowCascadeTests COMMAND NyxShadowCascadeTests)

# The world's air without a GPU: the sun through the air, the daylight, the view's air from an
# environment, the height fog along rays against the fog summed in steps, bloom's chain and
# threshold, the component saved.
add_executable(NyxAtmosphereTests AtmosphereTests.cpp)
target_link_libraries(NyxAtmosphereTests PRIVATE NyxEngine)
add_dependencies(NyxAtmosphereTests NyxGeneratedReflection)
set_property(TARGET NyxAtmosphereTests PROPERTY FOLDER "Tests")
add_test(NAME NyxAtmosphereTests COMMAND NyxAtmosphereTests)

# The time of day: the sun's path, its light below the horizon, and the time moving on with a game
# world's steps, the same whatever the frames, set live from the editor
add_executable(NyxTimeOfDayTests TimeOfDayTests.cpp)
target_link_libraries(NyxTimeOfDayTests PRIVATE NyxEngine NyxLinkTools)
add_dependencies(NyxTimeOfDayTests NyxGeneratedReflection)
set_property(TARGET NyxTimeOfDayTests PROPERTY FOLDER "Tests")
add_test(NAME NyxTimeOfDayTests COMMAND NyxTimeOfDayTests)

# Colour grading without a GPU: the identity's table, each control's way, values in range, the keys
# blended by the sun's height, baked again only when the grade moved, the component saved.
add_executable(NyxColorGradingTests ColorGradingTests.cpp)
target_link_libraries(NyxColorGradingTests PRIVATE NyxEngine)
add_dependencies(NyxColorGradingTests NyxGeneratedReflection)
set_property(TARGET NyxColorGradingTests PROPERTY FOLDER "Tests")
add_test(NAME NyxColorGradingTests COMMAND NyxColorGradingTests)

# Light shafts without a GPU: the sun's place on the picture, their fade as the sun leaves it, goes
# behind the camera or sets, the decay by their length, values in range, the component saved.
add_executable(NyxLightShaftsTests LightShaftsTests.cpp)
target_link_libraries(NyxLightShaftsTests PRIVATE NyxEngine)
add_dependencies(NyxLightShaftsTests NyxGeneratedReflection)
set_property(TARGET NyxLightShaftsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxLightShaftsTests COMMAND NyxLightShaftsTests)

# The high cloud layer without a GPU: where rays meet its shell from below, at and above it, the
# clouds near the camera faded, the wind carrying the pattern with the world's time, values in range,
# the component saved.
add_executable(NyxCloudLayerTests CloudLayerTests.cpp)
target_link_libraries(NyxCloudLayerTests PRIVATE NyxEngine)
add_dependencies(NyxCloudLayerTests NyxGeneratedReflection)
set_property(TARGET NyxCloudLayerTests PROPERTY FOLDER "Tests")
add_test(NAME NyxCloudLayerTests COMMAND NyxCloudLayerTests)

# Material files and their values without a GPU: reading and writing, offsets, layouts that change.
add_executable(NyxMaterialTests MaterialTests.cpp)
target_link_libraries(NyxMaterialTests PRIVATE NyxEngine)
target_compile_definitions(NyxMaterialTests PRIVATE NYX_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders")
add_dependencies(NyxMaterialTests NyxGeneratedReflection)
set_property(TARGET NyxMaterialTests PROPERTY FOLDER "Tests")
nyx_copy_shader_compiler_next_to(NyxMaterialTests)
add_test(NAME NyxMaterialTests COMMAND NyxMaterialTests)

# The environment's light on a real GPU, from skies whose light is known. Needs a Vulkan device;
# its window stays hidden.
add_executable(NyxEnvironmentGpuTests EnvironmentGpuTests.cpp)
target_link_libraries(NyxEnvironmentGpuTests PRIVATE NyxEngine)
add_dependencies(NyxEnvironmentGpuTests NyxGeneratedReflection)
set_property(TARGET NyxEnvironmentGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxEnvironmentGpuTests)
nyx_copy_shader_compiler_next_to(NyxEnvironmentGpuTests)
add_test(NAME NyxEnvironmentGpuTests COMMAND NyxEnvironmentGpuTests)

# The material test scene on a real GPU: the sphere grid against its reference, spheres against the
# CPU, the furnace test. Needs a Vulkan device; its window stays hidden. --write-scene writes the
# scene and its materials.
add_executable(NyxMaterialGpuTests MaterialGpuTests.cpp)
target_link_libraries(NyxMaterialGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxMaterialGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxMaterialGpuTests NyxGeneratedReflection)
set_property(TARGET NyxMaterialGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxMaterialGpuTests)
nyx_copy_shader_compiler_next_to(NyxMaterialGpuTests)
add_test(NAME NyxMaterialGpuTests COMMAND NyxMaterialGpuTests)

# The renderer drawing the editor's default scene on a real GPU, compared with a reference image,
# and picking in it. Needs a Vulkan device; its window stays hidden.
add_executable(NyxSceneGpuTests SceneGpuTests.cpp)
target_link_libraries(NyxSceneGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxSceneGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxSceneGpuTests NyxGeneratedReflection)
set_property(TARGET NyxSceneGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxSceneGpuTests)
nyx_copy_shader_compiler_next_to(NyxSceneGpuTests)
add_test(NAME NyxSceneGpuTests COMMAND NyxSceneGpuTests)
# Again with 4 samples a pixel: the reference images matched away from edges
add_test(NAME NyxSceneGpuTestsMsaa COMMAND NyxSceneGpuTests --msaa 4)

# Meshes made at run time on a real GPU: drawn as the same mesh loaded, redrawn when changed, picked,
# outlined, two sections with two materials, a decal on one. Needs a Vulkan device; its window stays
# hidden.
add_executable(NyxRuntimeMeshGpuTests RuntimeMeshGpuTests.cpp)
target_link_libraries(NyxRuntimeMeshGpuTests PRIVATE NyxEngine)
add_dependencies(NyxRuntimeMeshGpuTests NyxGeneratedReflection)
set_property(TARGET NyxRuntimeMeshGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxRuntimeMeshGpuTests)
nyx_copy_shader_compiler_next_to(NyxRuntimeMeshGpuTests)
add_test(NAME NyxRuntimeMeshGpuTests COMMAND NyxRuntimeMeshGpuTests)

# Instanced drawing and culling on a real GPU: a mesh drawn at every transform of its instances as
# it would be on an entity for each, uploaded again only for a new revision, picked, culled by cell
# and by entity, a view's culling frozen and shown. Needs a Vulkan device; its window stays hidden.
add_executable(NyxInstancingGpuTests InstancingGpuTests.cpp)
target_link_libraries(NyxInstancingGpuTests PRIVATE NyxEngine)
add_dependencies(NyxInstancingGpuTests NyxGeneratedReflection)
set_property(TARGET NyxInstancingGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxInstancingGpuTests)
nyx_copy_shader_compiler_next_to(NyxInstancingGpuTests)
add_test(NAME NyxInstancingGpuTests COMMAND NyxInstancingGpuTests)

# The materials' own numbers on a real GPU: a mesh's custom channels, each instance's and an entity's
# drawn once reach the material, the view's time stands still while the world's clock is paused.
# Needs a Vulkan device; its window stays hidden.
add_executable(NyxVertexDataGpuTests VertexDataGpuTests.cpp)
target_link_libraries(NyxVertexDataGpuTests PRIVATE NyxEngine)
add_dependencies(NyxVertexDataGpuTests NyxGeneratedReflection)
set_property(TARGET NyxVertexDataGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxVertexDataGpuTests)
nyx_copy_shader_compiler_next_to(NyxVertexDataGpuTests)
add_test(NAME NyxVertexDataGpuTests COMMAND NyxVertexDataGpuTests)

# The primary light's shadows on a real GPU: a box shadowing a plane and an instanced field shadowing
# it too, compared with a reference image; shadows switched off on the light and on a mesh; a
# two-sided quad casting from both sides; each cascade culled by its own frustum; the debug view.
# Needs a Vulkan device; its window stays hidden.
add_executable(NyxShadowGpuTests ShadowGpuTests.cpp)
target_link_libraries(NyxShadowGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxShadowGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxShadowGpuTests NyxGeneratedReflection)
set_property(TARGET NyxShadowGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxShadowGpuTests)
nyx_copy_shader_compiler_next_to(NyxShadowGpuTests)
add_test(NAME NyxShadowGpuTests COMMAND NyxShadowGpuTests)
add_test(NAME NyxShadowGpuTestsMsaa COMMAND NyxShadowGpuTests --msaa 4)

# The world's air on a real GPU: height fog on surfaces, decals and the sky against the CPU's,
# the sky following the sun, against a reference image. Needs a Vulkan device; its window stays
# hidden.
add_executable(NyxAtmosphereGpuTests AtmosphereGpuTests.cpp)
target_link_libraries(NyxAtmosphereGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxAtmosphereGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxAtmosphereGpuTests NyxGeneratedReflection)
set_property(TARGET NyxAtmosphereGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxAtmosphereGpuTests)
nyx_copy_shader_compiler_next_to(NyxAtmosphereGpuTests)
add_test(NAME NyxAtmosphereGpuTests COMMAND NyxAtmosphereGpuTests)

# The time of day on a GPU: the sky darker at sunset and at night, no light from a sun that has set
add_executable(NyxTimeOfDayGpuTests TimeOfDayGpuTests.cpp)
target_link_libraries(NyxTimeOfDayGpuTests PRIVATE NyxEngine)
add_dependencies(NyxTimeOfDayGpuTests NyxGeneratedReflection)
set_property(TARGET NyxTimeOfDayGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxTimeOfDayGpuTests)
nyx_copy_shader_compiler_next_to(NyxTimeOfDayGpuTests)
add_test(NAME NyxTimeOfDayGpuTests COMMAND NyxTimeOfDayGpuTests)
# The sky made by the air's scattering on a real GPU: its tables against the CPU's, the sky at noon
# and at sunset, from 2 m and 2 km up, its light on a wall and in the fog, made again only as it
# must be, unchanged to the bit without it, its GPU times. Needs a Vulkan device; its window stays
# hidden.
add_executable(NyxSkyAtmosphereGpuTests SkyAtmosphereGpuTests.cpp)
target_link_libraries(NyxSkyAtmosphereGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxSkyAtmosphereGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxSkyAtmosphereGpuTests NyxGeneratedReflection)
set_property(TARGET NyxSkyAtmosphereGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxSkyAtmosphereGpuTests)
nyx_copy_shader_compiler_next_to(NyxSkyAtmosphereGpuTests)
add_test(NAME NyxSkyAtmosphereGpuTests COMMAND NyxSkyAtmosphereGpuTests)

# Bloom on a real GPU: an emissive cube and decal spreading their light, the threshold, exposure,
# radius and levels, a view's own chain, the debug view, against a reference image. Needs a Vulkan
# device; its window stays hidden.
add_executable(NyxBloomGpuTests BloomGpuTests.cpp)
target_link_libraries(NyxBloomGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxBloomGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxBloomGpuTests NyxGeneratedReflection)
set_property(TARGET NyxBloomGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxBloomGpuTests)
nyx_copy_shader_compiler_next_to(NyxBloomGpuTests)
add_test(NAME NyxBloomGpuTests COMMAND NyxBloomGpuTests)
add_test(NAME NyxBloomGpuTestsMsaa COMMAND NyxBloomGpuTests --msaa 4)

# Colour grading on a real GPU: the lookup table against the CPU's grade, baked only when the grade
# changes, graded pixels, the keys by the sun's height, the picture unchanged to the bit without
# it, against a reference image. Needs a Vulkan device; its window stays hidden.
add_executable(NyxColorGradingGpuTests ColorGradingGpuTests.cpp)
target_link_libraries(NyxColorGradingGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxColorGradingGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxColorGradingGpuTests NyxGeneratedReflection)
set_property(TARGET NyxColorGradingGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxColorGradingGpuTests)
nyx_copy_shader_compiler_next_to(NyxColorGradingGpuTests)
add_test(NAME NyxColorGradingGpuTests COMMAND NyxColorGradingGpuTests)

# Light shafts on a real GPU: a sun behind a wall streaming through a window in it, none from a sun
# behind the camera or below the horizon, the picture unchanged to the bit without them, the same
# from a camera at cloud height, their GPU time, the test scene. Needs a Vulkan device; its window
# stays hidden.
add_executable(NyxLightShaftsGpuTests LightShaftsGpuTests.cpp)
target_link_libraries(NyxLightShaftsGpuTests PRIVATE NyxEngine)
add_dependencies(NyxLightShaftsGpuTests NyxGeneratedReflection)
set_property(TARGET NyxLightShaftsGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxLightShaftsGpuTests)
nyx_copy_shader_compiler_next_to(NyxLightShaftsGpuTests)
add_test(NAME NyxLightShaftsGpuTests COMMAND NyxLightShaftsGpuTests)
add_test(NAME NyxLightShaftsGpuTestsMsaa COMMAND NyxLightShaftsGpuTests --msaa 4)

# The high cloud layer on a real GPU: the picture unchanged to the bit without it, clouds from 10 m
# at noon, at sunset and at midnight, from above the layer, crossing it, the same sky at the same
# seed and time and moved by the wind, the ground under a low layer covered, the light shafts held
# back, its GPU time, the test scene. Needs a Vulkan device; its window stays hidden.
add_executable(NyxCloudLayerGpuTests CloudLayerGpuTests.cpp)
target_link_libraries(NyxCloudLayerGpuTests PRIVATE NyxEngine)
add_dependencies(NyxCloudLayerGpuTests NyxGeneratedReflection)
set_property(TARGET NyxCloudLayerGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxCloudLayerGpuTests)
nyx_copy_shader_compiler_next_to(NyxCloudLayerGpuTests)
add_test(NAME NyxCloudLayerGpuTests COMMAND NyxCloudLayerGpuTests)
add_test(NAME NyxCloudLayerGpuTestsMsaa COMMAND NyxCloudLayerGpuTests --msaa 4)

# Ambient occlusion on a real GPU: a box's foot occluded and the open floor not, never the sun's
# light, the radius, intensity and qualities, fog and decals, a dense instanced field, against a
# reference image. Needs a Vulkan device; its window stays hidden.
add_executable(NyxAmbientOcclusionGpuTests AmbientOcclusionGpuTests.cpp)
target_link_libraries(NyxAmbientOcclusionGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxAmbientOcclusionGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxAmbientOcclusionGpuTests NyxGeneratedReflection)
set_property(TARGET NyxAmbientOcclusionGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxAmbientOcclusionGpuTests)
nyx_copy_shader_compiler_next_to(NyxAmbientOcclusionGpuTests)
add_test(NAME NyxAmbientOcclusionGpuTests COMMAND NyxAmbientOcclusionGpuTests)
add_test(NAME NyxAmbientOcclusionGpuTestsMsaa COMMAND NyxAmbientOcclusionGpuTests --msaa 4)

# Multisample anti-aliasing on a real GPU: thin bars without shades between at one sample, with them
# at 2, 4 and 8; the count clamped and changed live; picking the same at every count; the targets'
# memory. Needs a Vulkan device; its window stays hidden.
add_executable(NyxMultisamplingGpuTests MultisamplingGpuTests.cpp)
target_link_libraries(NyxMultisamplingGpuTests PRIVATE NyxEngine)
add_dependencies(NyxMultisamplingGpuTests NyxGeneratedReflection)
set_property(TARGET NyxMultisamplingGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxMultisamplingGpuTests)
nyx_copy_shader_compiler_next_to(NyxMultisamplingGpuTests)
add_test(NAME NyxMultisamplingGpuTests COMMAND NyxMultisamplingGpuTests)

# Point lights on a real GPU: a light on a floor fading to nothing at its range, a coloured light,
# two adding up, under fog, behind the camera, culled out of view, never dimmed by ambient
# occlusion, against a reference image. Needs a Vulkan device; its window stays hidden.
add_executable(NyxPointLightGpuTests PointLightGpuTests.cpp)
target_link_libraries(NyxPointLightGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxPointLightGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxPointLightGpuTests NyxGeneratedReflection)
set_property(TARGET NyxPointLightGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxPointLightGpuTests)
nyx_copy_shader_compiler_next_to(NyxPointLightGpuTests)
add_test(NAME NyxPointLightGpuTests COMMAND NyxPointLightGpuTests)
add_test(NAME NyxPointLightGpuTestsMsaa COMMAND NyxPointLightGpuTests --msaa 4)

# Particles on a real GPU, stepped by a clock: one particle against the CPU's numbers, additive and
# alpha, lit, hidden behind a box, soft where it meets the floor; paused and stepped, carried by a
# velocity field, culled out of view, its icon picked; against a reference image. Needs a Vulkan
# device; its window stays hidden.
add_executable(NyxParticleGpuTests ParticleGpuTests.cpp)
target_link_libraries(NyxParticleGpuTests PRIVATE NyxEngine)
target_compile_definitions(NyxParticleGpuTests PRIVATE NYX_TEST_REFERENCES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/References")
add_dependencies(NyxParticleGpuTests NyxGeneratedReflection)
set_property(TARGET NyxParticleGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxParticleGpuTests)
nyx_copy_shader_compiler_next_to(NyxParticleGpuTests)
add_test(NAME NyxParticleGpuTests COMMAND NyxParticleGpuTests)
add_test(NAME NyxParticleGpuTestsMsaa COMMAND NyxParticleGpuTests --msaa 4)

# How long the renderer takes for 50 000 small meshes in 200 cells and 300 more, drawn one by one
# and instanced; not run by ctest, timing depends on the machine. Needs a Vulkan device.
add_executable(NyxInstancingBenchmark InstancingBenchmark.cpp)
target_link_libraries(NyxInstancingBenchmark PRIVATE NyxEngine)
add_dependencies(NyxInstancingBenchmark NyxGeneratedReflection)
set_property(TARGET NyxInstancingBenchmark PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxInstancingBenchmark)
nyx_copy_shader_compiler_next_to(NyxInstancingBenchmark)

# Every #include between the engine's folders against their table in Docs/Architecture.md
add_test(NAME NyxRuntimeFolderDependencies
    COMMAND ${CMAKE_COMMAND}
        "-DFOLDERS_DIR=${PROJECT_SOURCE_DIR}/Engine/Source/Runtime"
        "-DEXTRA_FOLDERS=Reflection=${PROJECT_SOURCE_DIR}/Engine/Source/Reflection$<SEMICOLON>LinkTools=${PROJECT_SOURCE_DIR}/Engine/Source/LinkTools"
        "-DTABLE_HEADING=## Modules"
        "-DARCHITECTURE_DOC=${PROJECT_SOURCE_DIR}/Docs/Architecture.md"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/FolderDependencies.cmake")

if(MSVC)
    add_test(NAME NyxRuntimeInspectionSymbols
        COMMAND ${CMAKE_COMMAND}
            "-DLINKER=${CMAKE_LINKER}"
            "-DRUNTIME_LIBRARY=$<TARGET_FILE:NyxEngine>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeInspectionSymbols.cmake")
endif()
