# Default Start for CMAKE
cmake_minimum_required(VERSION 3.23)

# Lets a target choose how MSVC stores debug information (MSVC_DEBUG_INFORMATION_FORMAT).
# It must be set before project(); the editor uses it, see Editor/Source/CMakeLists.txt.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# Solution Name
project(Nyx)

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

# The editor's prototype and diagnostics tools (Painter Test, Mockup Overlay). While on, Debug
# builds have them; Release builds, which are what ships, never do.
option(NYX_DEV_TOOLS "Build the editor's prototype and diagnostics tools into Debug builds" ON)

# The Tracy profiler in every program (Profiler.h). It collects nothing until a viewer connects;
# off, nothing of Tracy is compiled or linked.
option(NYX_PROFILER "Build the Tracy profiler into the engine and its programs" ON)

## Execute CMake files in subdirectories
# Dependencies
add_subdirectory(Dependencies/spdlog)
add_subdirectory(Dependencies/glfw)
add_subdirectory(Dependencies/glm)
add_subdirectory(ThirdParty/imgui)
add_subdirectory(ThirdParty/stb_image)
if(NYX_PROFILER)
    # Tracy's client listens for a viewer only once one asks for data, only on this machine (as
    # the editor link does), and leaves crashes to Nyx's own handler (CrashHandler.cpp).
    set(TRACY_ENABLE ON CACHE BOOL "" FORCE)
    set(TRACY_ON_DEMAND ON CACHE BOOL "" FORCE)
    set(TRACY_ONLY_LOCALHOST ON CACHE BOOL "" FORCE)
    set(TRACY_NO_CRASH_HANDLER ON CACHE BOOL "" FORCE)
    add_subdirectory(Dependencies/tracy)
endif()

# Jolt, the physics engine (Engine/Source/Runtime/Physics), as a static library. Built with the
# same runtime library and flags as Nyx, without link-time optimization, without its own profiler,
# its GPU compute, its object streams or its install target. Its asserts are on in Debug.
set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(ENABLE_ALL_WARNINGS OFF CACHE BOOL "" FORCE)
set(FLOATING_POINT_EXCEPTIONS_ENABLED OFF CACHE BOOL "" FORCE)
set(PROFILER_IN_DEBUG_AND_RELEASE OFF CACHE BOOL "" FORCE)
set(ENABLE_OBJECT_STREAM OFF CACHE BOOL "" FORCE)
set(ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(JPH_USE_DX12 OFF CACHE BOOL "" FORCE)
set(JPH_USE_VK OFF CACHE BOOL "" FORCE)
set(JPH_USE_MTL OFF CACHE BOOL "" FORCE)
set(JPH_USE_CPU_COMPUTE OFF CACHE BOOL "" FORCE)
add_subdirectory(Dependencies/JoltPhysics/Build)
target_compile_definitions(Jolt PUBLIC "$<$<CONFIG:Debug>:JPH_ENABLE_ASSERTS>")

# Tests
include(CTest)

# Main Modules
add_subdirectory(Tools/NyxHeaderTool)
add_subdirectory(Startup)
# Shaders first: they define nyx_copy_shaders_next_to() and nyx_copy_shader_compiler_next_to(),
# which the engine's tests call.
add_subdirectory(Engine/Shaders)
add_subdirectory(Engine/Source)
add_subdirectory(Editor/Source)
add_subdirectory(Game/Source)
add_subdirectory(Tools/NyxDump)
add_subdirectory(Tools/NyxShaderCompiler)

# nyx_add_game_programs([MODULE <name>] [PROJECT_ROOT <folder>])
#
# The editor and the game, NyxEditor.exe and NyxGame.exe, built with the game module <name>
# (nyx_add_game_module()) or none. Nyx built on its own makes them without a module; a game's
# repository, which adds Nyx as a subdirectory, makes them with its module.
# PROJECT_ROOT is the folder with the project's Assets: the top project's folder if not given.
function(nyx_add_game_programs)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "MODULE;PROJECT_ROOT" "")
    if(NOT ARG_PROJECT_ROOT)
        set(ARG_PROJECT_ROOT "${CMAKE_SOURCE_DIR}")
    endif()
    cmake_path(ABSOLUTE_PATH ARG_PROJECT_ROOT BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE)

    nyx_add_game_program(MODULE ${ARG_MODULE})
    nyx_add_editor_program(MODULE ${ARG_MODULE})

    # Next to the engine's other programs and the compiled shaders, from whichever folder this is
    # called
    foreach(program NyxGame NyxEditor)
        set_target_properties(${program} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/Binaries)
        foreach(config ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${config} configUpper)
            set_target_properties(${program} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY_${configUpper} ${CMAKE_BINARY_DIR}/Binaries/${config}
            )
        endforeach()
    endforeach()

    # Where the programs find the engine's folder and the project's (Paths): next to them, for
    # every configuration
    file(GENERATE OUTPUT "$<TARGET_FILE_DIR:NyxGame>/NyxRoots.ini" CONTENT
"# Written by nyx_add_game_programs(): where the programs here find the engine and the project
EngineRoot = ${CMAKE_CURRENT_FUNCTION_LIST_DIR}
ProjectRoot = ${ARG_PROJECT_ROOT}
")

    # Set default startup project
    set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT NyxEditor)
endfunction()

if(PROJECT_IS_TOP_LEVEL)
    nyx_add_game_programs()
endif()
set_property(TARGET imgui PROPERTY FOLDER "ThirdParty")
set_property(TARGET spdlog PROPERTY FOLDER "ThirdParty")
set_property(TARGET stb_image PROPERTY FOLDER "ThirdParty")
set_property(TARGET glm PROPERTY FOLDER "ThirdParty")
if(TARGET TracyClient)
    set_property(TARGET TracyClient PROPERTY FOLDER "ThirdParty")
endif()
set_property(TARGET Jolt PROPERTY FOLDER "ThirdParty")

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