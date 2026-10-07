# NyxGame.exe: runs a scene without the editor. It links only the engine.
set(NYX_GAME_SOURCES
    main.cpp
    src/GameLayer.cpp
)

set(NYX_GAME_HEADERS
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

# The renderer loads the shaders from the "Shaders" folder in the working directory,
# so run from next to the built .exe
set_target_properties(NyxGame PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NyxGame>"
)

nyx_copy_shaders_next_to(NyxGame)
