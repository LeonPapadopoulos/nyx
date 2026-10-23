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

# The game's command line, as the editor writes it and the game reads it.
add_executable(NyxGameLaunchOptionsTests GameLaunchOptionsTests.cpp)
target_link_libraries(NyxGameLaunchOptionsTests PRIVATE NyxEngine)
add_dependencies(NyxGameLaunchOptionsTests NyxGeneratedReflection)
set_property(TARGET NyxGameLaunchOptionsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxGameLaunchOptionsTests COMMAND NyxGameLaunchOptionsTests)

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
    SYSTEMS NyxTestModule::CreateSystems)
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
# decal saved and loaded
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

# Physics: bodies falling and resting, mass, forces at a point, teleports, kinematic moves, the
# same results from the same inputs, queries, sensors, contact events, constraints, the debug view
add_executable(NyxPhysicsTests PhysicsTests.cpp)
target_link_libraries(NyxPhysicsTests PRIVATE NyxEngine)
add_dependencies(NyxPhysicsTests NyxGeneratedReflection)
set_property(TARGET NyxPhysicsTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPhysicsTests COMMAND NyxPhysicsTests)

# Physics in the game world: bodies made, moved and removed with their components, transforms
# written back, live edits teleporting, kinematic bodies, systems around the physics step, the
# previous poses moving bodies are drawn between steps from
add_executable(NyxPhysicsBodiesTests PhysicsBodiesTests.cpp)
target_link_libraries(NyxPhysicsBodiesTests PRIVATE NyxEngine)
add_dependencies(NyxPhysicsBodiesTests NyxGeneratedReflection)
set_property(TARGET NyxPhysicsBodiesTests PROPERTY FOLDER "Tests")
add_test(NAME NyxPhysicsBodiesTests COMMAND NyxPhysicsBodiesTests)

# How long a physics step takes with 1000 bodies; not run by ctest, timing depends on the machine
add_executable(NyxPhysicsBenchmark PhysicsBenchmark.cpp)
target_link_libraries(NyxPhysicsBenchmark PRIVATE NyxEngine)
add_dependencies(NyxPhysicsBenchmark NyxGeneratedReflection)
set_property(TARGET NyxPhysicsBenchmark PROPERTY FOLDER "Tests")

# The game world: systems run in their order, every step or every nth one
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

# The built-in meshes' tangents, without a GPU.
add_executable(NyxMeshTests MeshTests.cpp)
target_link_libraries(NyxMeshTests PRIVATE NyxEngine)
add_dependencies(NyxMeshTests NyxGeneratedReflection)
set_property(TARGET NyxMeshTests PROPERTY FOLDER "Tests")
add_test(NAME NyxMeshTests COMMAND NyxMeshTests)

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
