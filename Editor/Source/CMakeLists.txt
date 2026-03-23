
add_executable(NyxEditor 
	main.cpp
)

target_link_libraries(NyxEditor 
	PRIVATE NyxEngine
)

# After building, have it place the generated Engine.dll next to the Editor.exe
add_custom_command(TARGET NyxEditor POST_BUILD 
	COMMAND ${CMAKE_COMMAND} -E copy_if_different
	$<TARGET_FILE:NyxEngine>
	$<TARGET_FILE_DIR:NyxEditor>
)

# Set working directory to be next to the built .exe
# Relevant for finding the Shaders subdirectory
set_target_properties(NyxEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NyxEditor>"
)

#######################################################################################
## Shader

# Find glslc from the Vulkan SDK / PATH
find_program(GLSLC glslc REQUIRED)

set(NYX_SHADER_SOURCE_DIR ${CMAKE_SOURCE_DIR}/Engine/Shaders)
set(NYX_SHADER_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/Shaders)

set(NYX_SHADER_SOURCES
    ${NYX_SHADER_SOURCE_DIR}/DefaultShader.vert
    ${NYX_SHADER_SOURCE_DIR}/DefaultShader.frag
)

set(NYX_SHADER_OUTPUTS)

foreach(SHADER ${NYX_SHADER_SOURCES})
    get_filename_component(SHADER_NAME ${SHADER} NAME)
    set(SPV ${NYX_SHADER_BUILD_DIR}/${SHADER_NAME}.spv)

    add_custom_command(
        OUTPUT ${SPV}
        COMMAND ${CMAKE_COMMAND} -E make_directory "${NYX_SHADER_BUILD_DIR}"
        COMMAND ${GLSLC} "${SHADER}" -o "${SPV}"
        DEPENDS ${SHADER}
        MAIN_DEPENDENCY ${SHADER}
        COMMENT "Compiling shader ${SHADER_NAME}"
        VERBATIM
    )

    list(APPEND NYX_SHADER_OUTPUTS ${SPV})
endforeach()

add_custom_target(NyxShadersCompile ALL
    DEPENDS ${NYX_SHADER_OUTPUTS}
)

add_custom_target(NyxShadersCopy ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:NyxEditor>/Shaders"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${NYX_SHADER_OUTPUTS}
        "$<TARGET_FILE_DIR:NyxEditor>/Shaders"
    COMMAND_EXPAND_LISTS
    DEPENDS NyxShadersCompile
)

add_dependencies(NyxEditor NyxShadersCopy)

#######################################################################################