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

# The painter shader on a real GPU: paints small frames and checks pixels read back. Needs a
# Vulkan device; its window stays hidden.
add_executable(NyxPainterGpuTests PainterGpuTests.cpp)
target_link_libraries(NyxPainterGpuTests PRIVATE NyxEngine)
# stb_image_write.h, which GLFW ships, saves painted images when NYX_PAINTER_TEST_OUTPUT is set.
target_include_directories(NyxPainterGpuTests PRIVATE "${CMAKE_SOURCE_DIR}/Dependencies/glfw/deps")
add_dependencies(NyxPainterGpuTests NyxGeneratedReflection)
set_property(TARGET NyxPainterGpuTests PROPERTY FOLDER "Tests")
nyx_copy_shaders_next_to(NyxPainterGpuTests)
add_test(NAME NyxPainterGpuTests COMMAND NyxPainterGpuTests)

if(MSVC)
    add_test(NAME NyxRuntimeInspectionSymbols
        COMMAND ${CMAKE_COMMAND}
            "-DLINKER=${CMAKE_LINKER}"
            "-DRUNTIME_LIBRARY=$<TARGET_FILE:NyxEngine>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/RuntimeInspectionSymbols.cmake")
endif()
