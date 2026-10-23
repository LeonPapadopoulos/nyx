# NyxGame.exe: runs a scene without the editor. It links the engine and a game module, or none.
# Made by nyx_add_game_programs() (the top CMakeLists.txt), so a game's repository can build it with
# its own module.

# nyx_add_game_program([MODULE <name>])
function(nyx_add_game_program)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "MODULE" "")
    set(root "${CMAKE_CURRENT_FUNCTION_LIST_DIR}")

    set(sources
        ${root}/main.cpp
        ${root}/src/EditorLinkLayer.cpp
        ${root}/src/GameLayer.cpp
        ${root}/src/GameFlyCamera.cpp
    )
    set(headers
        ${root}/include/EditorLinkLayer.h
        ${root}/include/GameLayer.h
        ${root}/include/GameFlyCamera.h
    )

    add_executable(NyxGame ${sources} ${headers})

    target_include_directories(NyxGame
        PRIVATE
            ${root}/include
    )

    target_link_libraries(NyxGame
        PRIVATE NyxEngine
        shell32 # CommandLineToArgvW, for the command line in UTF-8
    )
    nyx_link_game_module(NyxGame MODULE ${ARG_MODULE})

    # The game includes component headers, which include their generated reflection code.
    add_dependencies(NyxGame NyxGeneratedReflection)

    # Tell Visual Studio to mimic the actual file hierarchy
    source_group(TREE ${root} FILES ${sources} ${headers})

    # Compiled by Engine/Shaders; the renderer loads them from a "Shaders" folder next to the .exe
    nyx_copy_shaders_next_to(NyxGame)
    # Live shader reload compiles in NyxShaderCompiler.exe, which sits next to the game
    add_dependencies(NyxGame NyxShaderCompiler)
endfunction()
