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
target_link_libraries(NyxEditorLinkTests PRIVATE NyxEngine)
add_dependencies(NyxEditorLinkTests NyxGeneratedReflection)
set_property(TARGET NyxEditorLinkTests PROPERTY FOLDER "Tests")
add_test(NAME NyxEditorLinkTests COMMAND NyxEditorLinkTests)

# Live edits: messages made from one world and applied to another, as the editor and the game do.
add_executable(NyxLiveEditTests LiveEditTests.cpp)
target_link_libraries(NyxLiveEditTests PRIVATE NyxEngine)
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

# ResourceManager and AsyncResourceManager with stand-in resources.
add_executable(NyxResourceManagerTests ResourceManagerTests.cpp)
target_link_libraries(NyxResourceManagerTests PRIVATE NyxEngine)
add_dependencies(NyxResourceManagerTests NyxGeneratedReflection)
set_property(TARGET NyxResourceManagerTests PROPERTY FOLDER "Tests")
add_test(NAME NyxResourceManagerTests COMMAND NyxResourceManagerTests)

# The input system with virtual devices: frames, taps shorter than a frame, devices coming and
# going, focus, the cursor. No window needed.
add_executable(NyxInputTests InputTests.cpp)
target_link_libraries(NyxInputTests PRIVATE NyxEngine)
add_dependencies(NyxInputTests NyxGeneratedReflection)
set_property(TARGET NyxInputTests PROPERTY FOLDER "Tests")
add_test(NAME NyxInputTests COMMAND NyxInputTests)

# The painter's CPU side: draw commands, groups, clip and transform stacks, tile binning. No GPU needed.
add_executable(NyxPainterTests PainterTests.cpp)
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
add_executable(NyxPainterGpuTests PainterGpuTests.cpp)
target_link_libraries(NyxPainterGpuTests PRIVATE NyxEngine)
# stb_image_write.h, which GLFW ships, saves painted images when NYX_PAINTER_TEST_OUTPUT is set.
target_include_directories(NyxPainterGpuTests PRIVATE "${CMAKE_SOURCE_DIR}/Dependencies/glfw/deps")
add_dependencies(NyxPainterGpuTests NyxGeneratedReflection)
set_property(TARGET NyxPainterGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxPainterGpuTests)
nyx_copy_shader_compiler_next_to(NyxPainterGpuTests)
add_test(NAME NyxPainterGpuTests COMMAND NyxPainterGpuTests)

# SPIRV-Tools' validator, for the shader compiler tests. Its release and debug builds both come with the SDK.
find_library(NYX_SPIRV_TOOLS_LIBRARY SPIRV-Tools HINTS "${NYX_VULKAN_LIB_DIR}" REQUIRED)
find_library(NYX_SPIRV_TOOLS_DEBUG_LIBRARY SPIRV-Toolsd HINTS "${NYX_VULKAN_LIB_DIR}" REQUIRED)

# ShaderCompiler without a GPU: materials, layouts, diagnostics, dependencies, and the engine's GLSL
# compiled by Slang with the same layouts as glslc's (from NyxShaders, until change 4 retires glslc).
add_executable(NyxShaderCompilerTests ShaderCompilerTests.cpp)
target_link_libraries(NyxShaderCompilerTests PRIVATE NyxEngine optimized "${NYX_SPIRV_TOOLS_LIBRARY}" debug "${NYX_SPIRV_TOOLS_DEBUG_LIBRARY}")
# The SDK ships SPIRV-Toolsd.lib without its PDB
target_link_options(NyxShaderCompilerTests PRIVATE "/IGNORE:4099")
target_compile_definitions(NyxShaderCompilerTests PRIVATE
    NYX_TEST_SHADERS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    NYX_ENGINE_SHADERS_DIR="${CMAKE_SOURCE_DIR}/Engine/Shaders"
    NYX_GLSLC_SHADERS_DIR="${CMAKE_BINARY_DIR}/Engine/Shaders/Compiled"
)
add_dependencies(NyxShaderCompilerTests NyxGeneratedReflection NyxShaders)
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

if(MSVC)
    add_test(NAME NyxRuntimeInspectionSymbols
        COMMAND ${CMAKE_COMMAND}
            "-DLINKER=${CMAKE_LINKER}"
            "-DRUNTIME_LIBRARY=$<TARGET_FILE:NyxEngine>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeInspectionSymbols.cmake")
endif()
