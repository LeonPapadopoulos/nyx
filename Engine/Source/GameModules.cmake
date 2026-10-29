# Game modules: a game's own code and component types, built against the engine and linked into
# the editor and the game (GameModule.h). Nyx never names a game: a game's repository brings in Nyx
# with add_subdirectory(), declares its module with nyx_add_game_module() and gets the programs
# with nyx_add_game_programs(MODULE <name>) (the repository's top CMakeLists.txt).

# The game module of a program built without one: GetLinkedGameModule() gives an empty module.
add_library(NyxNoGameModule OBJECT ${CMAKE_CURRENT_LIST_DIR}/NoGameModule/src/NoGameModule.cpp)
# The engine's headers and settings only: an object library takes no objects from another
target_link_libraries(NyxNoGameModule PRIVATE NyxEngineCommon)
add_dependencies(NyxNoGameModule NyxGeneratedReflection)

# The engine's reflected types, which a module's types may hold as properties, such as an
# AssetReference: the header tool reads their names from here and generates nothing for them.
set_property(GLOBAL PROPERTY NYX_ENGINE_REFLECT_DIR "${CMAKE_CURRENT_LIST_DIR}/Runtime/Scene/include")

# Declares a function the module names for REGISTER, SYSTEMS or PLAYER, for the generated
# GetLinkedGameModule(): `signature` with @FUNCTION@ for its name, inside its namespace if
# `qualified` has one (MyGame::RegisterTypes).
function(_nyx_declare_module_function module option qualified signature outVariable)
    if(NOT qualified MATCHES "^([A-Za-z_][A-Za-z0-9_]*::)*[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "nyx_add_game_module(${module}): ${option} '${qualified}' is no qualified function name, e.g. MyGame::RegisterTypes")
    endif()
    string(FIND "${qualified}" "::" separator REVERSE)
    if(separator EQUAL -1)
        string(REPLACE "@FUNCTION@" "${qualified}" declaration "${signature}")
        set(${outVariable} "\n${declaration};\n" PARENT_SCOPE)
    else()
        string(SUBSTRING "${qualified}" 0 ${separator} namespaceName)
        math(EXPR nameStart "${separator} + 2")
        string(SUBSTRING "${qualified}" ${nameStart} -1 functionName)
        string(REPLACE "@FUNCTION@" "${functionName}" declaration "${signature}")
        set(${outVariable} "\nnamespace ${namespaceName}\n{\n\t${declaration};\n}\n" PARENT_SCOPE)
    endif()
endfunction()

# nyx_add_game_module(<name> REFLECT_DIRS <dir>... [SOURCES <file>...] [INCLUDE_DIRS <dir>...]
#                     [REGISTER <function>] [SYSTEMS <function>] [PLAYER <function>])
#
# Makes the module <name>, a C++ identifier that names its generated files and functions:
# - NyxHeaderTool reads the headers in REFLECT_DIRS into Generated/<name>, so a component header
#   ends with #include "Generated/<name>/<Header>.reflect.h". The folders are include folders of the
#   module too. A component may hold the engine's reflected structs, such as Nyx::SurfaceAnchor.
# - The target <name>, an object library: SOURCES and the generated type registration, compiled
#   once against the engine's headers. A module links no engine library: each program links the
#   one it uses, and the module's objects with it (nyx_link_game_module()).
# - REGISTER names a function of the module's, `void F()`, run right after its generated type
#   registration: for post-load steps (ComponentTypeRegistry::SetPostLoad) and the like.
# - SYSTEMS names a function of the module's, `void F(Nyx::GameWorld& world)`, that adds its
#   simulation systems to the game's world, in their order.
# - PLAYER names a function of the module's,
#   `std::unique_ptr<Nyx::IPlayerController> F(Nyx::Input::InputSystem& input)`, that makes the
#   player's controller: it turns input into commands for the simulation each frame
#   (PlayerController.h).
#   All are qualified names, e.g. MyGame::RegisterTypes; the module defines them in its SOURCES.
# The module is game code (nyx_mark_game_code()): DebugGame compiles it unoptimised.
# Relative folders and files are taken from the folder of the CMakeLists.txt that calls it.
function(nyx_add_game_module name)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "" "REGISTER;SYSTEMS;PLAYER" "SOURCES;REFLECT_DIRS;INCLUDE_DIRS")

    if(NOT name MATCHES "^[A-Za-z_][A-Za-z0-9_]*$")
        message(FATAL_ERROR "nyx_add_game_module: '${name}' is no C++ identifier; it names the module's generated functions")
    endif()
    if(TARGET ${name})
        message(FATAL_ERROR "nyx_add_game_module: a target named '${name}' exists already")
    endif()
    if(NOT ARG_REFLECT_DIRS)
        message(FATAL_ERROR "nyx_add_game_module(${name}): REFLECT_DIRS names no folder of component headers")
    endif()

    set(generatedDir "${CMAKE_BINARY_DIR}/Generated/${name}")
    set(stamp "${generatedDir}/Reflection.stamp")
    set(initHeader "${generatedDir}/${name}.reflect.init.h")
    set(initCpp "${generatedDir}/${name}.reflect.init.cpp")
    set(sourcesCpp "${generatedDir}/${name}.reflect.sources.cpp")

    set(reflectDirs "")
    set(scanArguments "")
    set(headerInputs "")
    foreach(dir IN LISTS ARG_REFLECT_DIRS)
        cmake_path(ABSOLUTE_PATH dir BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" NORMALIZE OUTPUT_VARIABLE absoluteDir)
        if(NOT IS_DIRECTORY "${absoluteDir}")
            message(FATAL_ERROR "nyx_add_game_module(${name}): REFLECT_DIRS folder '${absoluteDir}' doesn't exist")
        endif()
        list(APPEND reflectDirs "${absoluteDir}")
        list(APPEND scanArguments --scan-root "${absoluteDir}")
        file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${absoluteDir}/*.h")
        list(APPEND headerInputs ${headers})
    endforeach()

    get_property(engineReflectDir GLOBAL PROPERTY NYX_ENGINE_REFLECT_DIR)

    add_custom_command(
        OUTPUT "${stamp}" "${initHeader}" "${initCpp}" "${sourcesCpp}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${generatedDir}"
        COMMAND "$<TARGET_FILE:NyxHeaderTool>"
                ${scanArguments}
                --type-root "${engineReflectDir}"
                --output-dir "${generatedDir}"
                --module-name "${name}"
                --module-init-header "${initHeader}"
                --module-init-cpp "${initCpp}"
                --editor-sources-cpp "${sourcesCpp}"
        COMMAND ${CMAKE_COMMAND} -E touch "${stamp}"
        DEPENDS NyxHeaderTool ${headerInputs}
        COMMENT "Generating reflection metadata for game module ${name}"
        VERBATIM
    )
    add_custom_target(${name}Reflection DEPENDS "${stamp}" "${initHeader}" "${initCpp}" "${sourcesCpp}")

    add_library(${name} OBJECT ${ARG_SOURCES} "${initCpp}")
    target_link_libraries(${name} PRIVATE NyxEngineCommon)
    target_include_directories(${name} PUBLIC ${reflectDirs} ${ARG_INCLUDE_DIRS})
    add_dependencies(${name} ${name}Reflection NyxGeneratedReflection)

    # What each program needs besides the module's objects: GetLinkedGameModule(), naming its
    # registration, its systems and its player. The editor's also registers where the module's
    # types are declared, generated code that includes the editor's headers, so it is compiled only
    # for the editor; the editor doesn't simulate, so its module has no systems and no player.
    set(moduleDeclarations "")
    set(registerCall "")
    set(createSystems "nullptr")
    if(ARG_REGISTER)
        _nyx_declare_module_function(${name} REGISTER "${ARG_REGISTER}" "void @FUNCTION@()" declaration)
        string(APPEND moduleDeclarations "${declaration}")
        set(registerCall "\n\t\t::${ARG_REGISTER}();")
    endif()
    if(ARG_SYSTEMS)
        _nyx_declare_module_function(${name} SYSTEMS "${ARG_SYSTEMS}" "void @FUNCTION@(Nyx::GameWorld& world)" declaration)
        string(APPEND moduleDeclarations "${declaration}")
        set(createSystems "&::${ARG_SYSTEMS}")
    endif()
    set(createPlayer "nullptr")
    if(ARG_PLAYER)
        _nyx_declare_module_function(${name} PLAYER "${ARG_PLAYER}"
            "std::unique_ptr<Nyx::IPlayerController> @FUNCTION@(Nyx::Input::InputSystem& input)" declaration)
        string(APPEND moduleDeclarations "${declaration}")
        set(createPlayer "&::${ARG_PLAYER}")
    endif()

    set(gameGlue "${generatedDir}/${name}.module.game.cpp")
    file(CONFIGURE OUTPUT "${gameGlue}" @ONLY CONTENT [=[
// Generated by nyx_add_game_module(@name@): the game module the game is built with
#include "GameModule.h"
#include "Generated/@name@/@name@.reflect.init.h"
@moduleDeclarations@
namespace
{
	// The module's generated type registration, then its own (REGISTER)
	void RegisterModuleTypes()
	{
		Nyx::Reflection::Generated::Register@name@ReflectedTypes();@registerCall@
	}
}

namespace Nyx
{
	const GameModule& GetLinkedGameModule()
	{
		static const GameModule Module{ .Name = "@name@",
			.RegisterTypes = &RegisterModuleTypes,
			.CreateSystems = @createSystems@,
			.CreatePlayerController = @createPlayer@ };
		return Module;
	}
}
]=])

    set(editorGlue "${generatedDir}/${name}.module.editor.cpp")
    file(CONFIGURE OUTPUT "${editorGlue}" @ONLY CONTENT [=[
// Generated by nyx_add_game_module(@name@): the game module the editor is built with
#include "GameModule.h"
#include "Generated/@name@/@name@.reflect.init.h"
@moduleDeclarations@
namespace Nyx::Editor
{
	// Defined in @name@.reflect.sources.cpp
	void Register@name@ReflectedSources();
}

namespace
{
	// The module's generated type registration, then its own (REGISTER)
	void RegisterModuleTypes()
	{
		Nyx::Reflection::Generated::Register@name@ReflectedTypes();@registerCall@
	}
}

namespace Nyx
{
	const GameModule& GetLinkedGameModule()
	{
		static const GameModule Module{ .Name = "@name@",
			.RegisterTypes = &RegisterModuleTypes,
			.RegisterSources = &Nyx::Editor::Register@name@ReflectedSources };
		return Module;
	}
}
]=])

    add_library(${name}GameGlue OBJECT "${gameGlue}")
    target_link_libraries(${name}GameGlue PRIVATE NyxEngineCommon ${name})
    add_dependencies(${name}GameGlue ${name}Reflection)

    add_library(${name}EditorGlue OBJECT "${editorGlue}" "${sourcesCpp}")
    target_link_libraries(${name}EditorGlue PRIVATE NyxEditorLib ${name})
    add_dependencies(${name}EditorGlue ${name}Reflection)

    # The game's own code: unoptimised in DebugGame
    nyx_mark_game_code(${name} ${name}GameGlue ${name}EditorGlue)

    set_target_properties(${name} ${name}Reflection ${name}GameGlue ${name}EditorGlue PROPERTIES FOLDER "GameModules/${name}")
endfunction()

# nyx_link_game_module(<target> [MODULE <name>] [EDITOR])
#
# Links the module <name> into the program <target>: its objects, and the GetLinkedGameModule()
# for the game, or with EDITOR for the editor. Without MODULE, the empty one.
function(nyx_link_game_module target)
    cmake_parse_arguments(PARSE_ARGV 1 ARG "EDITOR" "MODULE" "")

    if(NOT ARG_MODULE)
        target_link_libraries(${target} PRIVATE NyxNoGameModule)
        return()
    endif()

    if(NOT TARGET ${ARG_MODULE}GameGlue)
        message(FATAL_ERROR "nyx_link_game_module(${target}): no game module '${ARG_MODULE}'; declare it with nyx_add_game_module() first")
    endif()

    if(ARG_EDITOR)
        target_link_libraries(${target} PRIVATE ${ARG_MODULE} ${ARG_MODULE}EditorGlue)
    else()
        target_link_libraries(${target} PRIVATE ${ARG_MODULE} ${ARG_MODULE}GameGlue)
    endif()
endfunction()
