# 
add_executable(Sandbox main.cpp)

target_link_libraries(Sandbox 
	PRIVATE Engine
)

#[[
# After building, have it place the generated Engine.dll next to the Sandbox.exe
add_custom_command(TARGET Sandbox POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	$<TARGET_FILE:Engine>
	$<TARGET_FILE_DIR:Sandbox>
)
]]