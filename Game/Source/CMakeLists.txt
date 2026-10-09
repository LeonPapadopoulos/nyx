# NyxGame.exe: runs a scene without the editor. It links only the engine.
set(NYX_GAME_SOURCES
    main.cpp
    src/EditorLinkLayer.cpp
    src/GameLayer.cpp
)

set(NYX_GAME_HEADERS
    include/EditorLinkLayer.h
    include/GameLayer.h
)

add_executable(NyxGame
	${NYX_GAME_SOURCES}
	${NYX_GAME_HEADERS}
)

target_include_directories(NyxGame
    PRIVATE
        include
)

target_link_libraries(NyxGame
	PRIVATE NyxEngine
)

# The game includes component headers, which include their generated reflection code.
add_dependencies(NyxGame NyxGeneratedReflection)

# Tell Visual Studio to mimic the actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_GAME_SOURCES}
	${NYX_GAME_HEADERS}
)

# Compiled by Engine/Shaders; the renderer loads them from a "Shaders" folder next to the .exe
nyx_copy_shaders_next_to(NyxGame)
