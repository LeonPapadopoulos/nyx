# Compiles the engine's shaders to SPIR-V once. Every program that renders calls
# nyx_copy_shaders_next_to(<target>), because the renderer loads the compiled shaders
# from a "Shaders" folder next to the executable.

# Find glslc from the Vulkan SDK / PATH
find_program(GLSLC glslc REQUIRED)

set(NYX_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/DefaultShader.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/DefaultShader.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Grid.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Grid.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Skybox.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Skybox.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugLines.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugLines.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Picking.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Picking.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/SelectionMask.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/SelectionMask.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/OutlineComposite.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/OutlineComposite.frag
)

set(NYX_SHADER_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/Compiled)
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

# Recompiles changed shaders on every build
add_custom_target(NyxShaders ALL
    DEPENDS ${NYX_SHADER_OUTPUTS}
)

# Kept globally, because nyx_copy_shaders_next_to() is called from other directories
set_property(GLOBAL PROPERTY NYX_SHADER_OUTPUTS "${NYX_SHADER_OUTPUTS}")

# Copies the compiled shaders into a "Shaders" folder next to the executable of <target>,
# on every build of <target>.
function(nyx_copy_shaders_next_to target)
    get_property(shaderFiles GLOBAL PROPERTY NYX_SHADER_OUTPUTS)

    add_custom_target(${target}Shaders ALL
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:${target}>/Shaders"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${shaderFiles}
            "$<TARGET_FILE_DIR:${target}>/Shaders"
        COMMAND_EXPAND_LISTS
    )

    add_dependencies(${target}Shaders NyxShaders)
    add_dependencies(${target} ${target}Shaders)
endfunction()
