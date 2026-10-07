# Startup module: the banner shown while the editor starts, and its intro
# animations (see README.md). It is self-contained: it needs only Windows and
# GDI+, and the engine uses it solely through include/WindowsStartupBanner.h.

# Intros drawn on the CPU, compiled with optimizations in every configuration.
add_subdirectory(src/SoftwareIntros)

set(NYX_STARTUP_SOURCES
    include/WindowsStartupBanner.h
    src/WindowsStartupBanner.cpp
    src/StartupBannerIntro.h
    src/StartupBannerIntro.cpp
    src/StartupBannerFracture.h
    src/StartupBannerFracture.cpp
    src/StartupBannerSoftwareIntro.h
    src/StartupBannerSoftwareIntro.cpp
)

# Linked into NyxEngine, which shows the banner while the editor initializes.
add_library(NyxStartup STATIC ${NYX_STARTUP_SOURCES})
target_compile_features(NyxStartup PUBLIC cxx_std_20)
target_include_directories(NyxStartup
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(NyxStartup PRIVATE NyxSoftwareIntros gdiplus user32 gdi32)
set_property(TARGET NyxStartup PROPERTY FOLDER "Startup")

# Visual-review tool: plays, benchmarks and exports the intros without starting
# the editor. It compiles the module's sources itself, because its diagnostics
# (benchmark, frame export) only exist when NYX_STARTUP_PREVIEW is defined.
add_executable(NyxStartupPreview
    Preview/main.cpp
    ${NYX_STARTUP_SOURCES}
    # Finds the project's Assets folder from wherever the executable is run.
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/Core/src/Paths.cpp
)
target_compile_features(NyxStartupPreview PRIVATE cxx_std_20)
target_compile_definitions(NyxStartupPreview PRIVATE NYX_STARTUP_PREVIEW)
target_include_directories(NyxStartupPreview PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/Engine/Source/Runtime/Core/include
)
target_link_libraries(NyxStartupPreview PRIVATE NyxSoftwareIntros gdiplus user32 gdi32)
set_property(TARGET NyxStartupPreview PROPERTY FOLDER "Startup")

# Show the module's real folder layout in Visual Studio.
list(TRANSFORM NYX_STARTUP_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${NYX_STARTUP_SOURCES})
