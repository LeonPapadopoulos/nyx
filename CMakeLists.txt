# Default Start for CMAKE
cmake_minimum_required(VERSION 3.23)

# Lets a target choose how MSVC stores debug information (MSVC_DEBUG_INFORMATION_FORMAT).
# It must be set before project(); the editor uses it, see Editor/Source/CMakeLists.txt.
if(POLICY CMP0141)
    cmake_policy(SET CMP0141 NEW)
endif()

# Solution Name
project(Nyx)

# ~Start build configurations (Docs/Architecture.md, "Build configurations")
# - Debug: nothing optimised, the debug C++ runtime and its checked containers.
# - DebugGame: the engine, the editor and the libraries they use optimised as in Development; the
#   game's own code (nyx_mark_game_code()) unoptimised, to step through it.
# - Development: everything optimised, with debug information, Nyx's asserts, logging and profiler.
# - Release: everything optimised, without asserts: what ships.
# All the code of one program must use the same C++ runtime and checked containers, or MSVC refuses
# to link it (LNK2038). So DebugGame and Development use the release runtime, as Release does
# (/MD, CMake's default for every configuration but Debug), with no checked containers
# (_ITERATOR_DEBUG_LEVEL 0) and NDEBUG. These are cache entries, so that a game's repository, which
# adds Nyx as a subdirectory, has them for its own targets too.
set(CMAKE_CONFIGURATION_TYPES "Debug;DebugGame;Development;Release" CACHE STRING "Build configurations, set by Nyx" FORCE)
if(MSVC)
    # /Zo: better debug information for optimised code
    set(nyxDevelopmentFlags "/O2 /Ob2 /Zo /DNDEBUG")
    set(nyxDebugGameFlags "/Zo /DNDEBUG")
    set(nyxLinkerFlags "/debug /INCREMENTAL")
else()
    set(nyxDevelopmentFlags "-O2 -g -DNDEBUG")
    set(nyxDebugGameFlags "-g -DNDEBUG")
    set(nyxLinkerFlags "")
endif()
foreach(language C CXX)
    set(CMAKE_${language}_FLAGS_DEVELOPMENT "${nyxDevelopmentFlags}" CACHE STRING "Set by Nyx" FORCE)
    # Without an optimisation level: each target gets its own, see _nyx_set_debuggame_optimization()
    set(CMAKE_${language}_FLAGS_DEBUGGAME "${nyxDebugGameFlags}" CACHE STRING "Set by Nyx" FORCE)
endforeach()
foreach(binary EXE SHARED MODULE)
    set(CMAKE_${binary}_LINKER_FLAGS_DEVELOPMENT "${nyxLinkerFlags}" CACHE STRING "Set by Nyx" FORCE)
    set(CMAKE_${binary}_LINKER_FLAGS_DEBUGGAME "${nyxLinkerFlags}" CACHE STRING "Set by Nyx" FORCE)
endforeach()
# Debug information (/Zi) in every configuration but Release, as CMake gives Debug
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,DebugGame,Development>:ProgramDatabase>" CACHE STRING "Set by Nyx" FORCE)

# The optimisation levels DebugGame gives a target or a file, as lists in `optimized` and
# `unoptimized`. A macro: the functions below run in other folders' scopes, a game's included.
macro(_nyx_optimization_flags)
    if(MSVC)
        set(optimized /O2 /Ob2)
        set(unoptimized /Od /Ob0)
    else()
        set(optimized -O2)
        set(unoptimized -O0)
    endif()
endmacro()

# nyx_mark_game_code(<target>...)
#
# Marks targets as a game's own code: DebugGame compiles them unoptimised, everything else as in
# Development. nyx_add_game_module() marks the module; a game marks its own programs and tests.
function(nyx_mark_game_code)
    set_target_properties(${ARGN} PROPERTIES NYX_GAME_CODE ON)
endfunction()

# nyx_optimize_game_sources(<target> <file>...)
#
# Compiles these files of a game code target optimised in DebugGame, as the engine is: for hot
# loops that are too slow unoptimised. #pragma optimize can't do it (Optimization.h). Relative
# files are taken from the folder of the CMakeLists.txt that calls it.
function(nyx_optimize_game_sources target)
    _nyx_optimization_flags()
    set_property(SOURCE ${ARGN} TARGET_DIRECTORY ${target} APPEND PROPERTY COMPILE_OPTIONS
        "$<$<CONFIG:DebugGame>:${optimized}>")
endfunction()

# Gives each target in <directory> and the folders under it its optimisation in DebugGame: none for
# game code, Development's for the rest.
function(_nyx_set_debuggame_optimization directory)
    _nyx_optimization_flags()
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
        get_target_property(type ${target} TYPE)
        if(type MATCHES "^(EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY|OBJECT_LIBRARY)$")
            target_compile_options(${target} PRIVATE
                "$<$<AND:$<CONFIG:DebugGame>,$<COMPILE_LANGUAGE:C,CXX>>:$<IF:$<BOOL:$<TARGET_PROPERTY:NYX_GAME_CODE>>,${unoptimized},${optimized}>>")
        endif()
    endforeach()
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    foreach(subdirectory IN LISTS subdirectories)
        _nyx_set_debuggame_optimization("${subdirectory}")
    endforeach()
endfunction()
# Once every target is made: at the end of the top folder, a game's when Nyx is part of one
cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL _nyx_set_debuggame_optimization "${CMAKE_SOURCE_DIR}")
# ~End build configurations

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

# The editor's prototype and diagnostics tools (Painter Test, Mockup Overlay). While on, every
# configuration but Release has them; Release builds, which are what ships, never do.
option(NYX_DEV_TOOLS "Build the editor's prototype and diagnostics tools into every configuration but Release" ON)

# The Tracy profiler in every program (Profiler.h). It collects nothing until a viewer connects;
# off, nothing of Tracy is compiled or linked.
option(NYX_PROFILER "Build the Tracy profiler into the engine and its programs" ON)

## Execute CMake files in subdirectories
# Dependencies
add_subdirectory(Dependencies/spdlog)
add_subdirectory(Dependencies/glfw)
add_subdirectory(Dependencies/glm)

# SDL3, for gamepads only: joystick, gamepad, haptic, sensor and HIDAPI, built static. No video,
# audio or rendering; GLFW keeps the window, keyboard and mouse.
foreach(SDL_PART AUDIO VIDEO GPU RENDER CAMERA POWER DIALOG TRAY)
    set(SDL_${SDL_PART} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(SDL_PART JOYSTICK HAPTIC HIDAPI SENSOR STATIC)
    set(SDL_${SDL_PART} ON CACHE BOOL "" FORCE)
endforeach()
foreach(SDL_PART SHARED TEST_LIBRARY TESTS EXAMPLES INSTALL UNINSTALL)
    set(SDL_${SDL_PART} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory(Dependencies/SDL)
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
# its GPU compute, its object streams or its install target. Its asserts are on in Debug only.
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
add_subdirectory(Tools/NyxPadCheck)

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

if(TARGET SDL3-static)
    set_property(TARGET SDL3-static PROPERTY FOLDER "ThirdParty")
endif()

if(TARGET update_mappings)
    set_property(TARGET update_mappings PROPERTY FOLDER "ThirdParty/glfw3")
endif()