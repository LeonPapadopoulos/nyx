# Default Start for CMAKE
cmake_minimum_required(VERSION 3.23)
# Solution Name
project(GameEngine)

# ~Start setup shared output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Libs)

foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOUPPER ${CONFIG} CONFIG_UPPER)

    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Binaries/${CONFIG})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Binaries/${CONFIG})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${CONFIG_UPPER} ${CMAKE_BINARY_DIR}/Libs/${CONFIG})
endforeach()
# ~End setup shared output directories

# START PRECOMPILED HEADER
# Create shared PCH target, so other targets like Engine, or Sandbox 
# can use it without independently compiling it; effectively speeding up the process
add_library(EnginePCH INTERFACE)
target_precompile_headers(EnginePCH 
    INTERFACE ${CMAKE_SOURCE_DIR}/Engine/src/Enginepch.h
)
target_include_directories(EnginePCH 
    INTERFACE ${CMAKE_SOURCE_DIR}/Engine/include
    INTERFACE ${CMAKE_SOURCE_DIR}/Engine/src
)
# END PRECOMPILED HEADER

# Execute CMake files in subdirectories
# add_subdirectory(Dependencies/entt-main) Why does this fail?
add_subdirectory(ThirdParty/imgui)
add_subdirectory(ThirdParty/stb_image)
add_subdirectory(Dependencies/spdlog)
add_subdirectory(Dependencies/glfw)
add_subdirectory(Dependencies/glm)
add_subdirectory(Engine)
add_subdirectory(Sandbox)

# Set default startup project
set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT Sandbox)
set_property(TARGET imgui PROPERTY FOLDER "ThirdParty")
set_property(TARGET spdlog PROPERTY FOLDER "ThirdParty")
set_property(TARGET stb_image PROPERTY FOLDER "ThirdParty")
set_property(TARGET glm PROPERTY FOLDER "ThirdParty")

# Todo: cleanup targets
if(TARGET glfw)
    set_property(TARGET glfw PROPERTY FOLDER "ThirdParty/glfw3")
endif()

if(TARGET uninstall)
    set_property(TARGET uninstall PROPERTY FOLDER "ThirdParty/glfw3")
endif()

if(TARGET update_mappings)
    set_property(TARGET update_mappings PROPERTY FOLDER "ThirdParty/glfw3")
endif()