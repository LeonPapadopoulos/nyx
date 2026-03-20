# 
add_executable(NyxEditor main.cpp)

target_link_libraries(NyxEditor 
	PRIVATE NyxEngine
)


# After building, have it place the generated Engine.dll next to the Editor.exe
add_custom_command(TARGET NyxEditor POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	$<TARGET_FILE:NyxCore>
	$<TARGET_FILE:NyxEngine>
	$<TARGET_FILE:NyxRenderer>
	$<TARGET_FILE_DIR:NyxEditor>
)
