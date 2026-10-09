# NyxDump.exe: prints Nyx data files as readable text, e.g. NyxDump Assets\Scenes\Default.nyxscene
add_executable(NyxDump
    main.cpp
)

target_link_libraries(NyxDump
    PRIVATE NyxEngine
)

# Includes engine headers, which include their generated reflection code
add_dependencies(NyxDump NyxGeneratedReflection)

# Pressing F5 in Visual Studio prints the default scene
set_target_properties(NyxDump PROPERTIES
    VS_DEBUGGER_COMMAND_ARGUMENTS "\"${CMAKE_SOURCE_DIR}/Assets/Scenes/Default.nyxscene\""
)
