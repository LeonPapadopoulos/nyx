# Compiles the engine's shaders with NyxShaderCompiler (its build mode) into a shader cache, on
# every build and each only when it or a file it reads changed. The renderer loads the cache from a
# "Shaders" folder next to the executable; every program is built into Binaries/<config>, so the
# cache is made right there, once for all of them. A program that renders calls
# nyx_copy_shaders_next_to(<target>), so that it is built after the shaders.

find_package(Vulkan REQUIRED)

# Listed so the IDE shows them; the compiler takes every .slang, .vert, .frag and .comp in this
# folder (not in Library, which they import)
set(NYX_SHADER_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/MissingMaterial.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Skybox.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Tonemap.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Environment.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Shadow.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Bloom.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/AmbientOcclusion.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/ColorGrading.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Particle.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Grid.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Grid.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugLines.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/DebugLines.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Picking.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Picking.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/SelectionMask.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/SelectionMask.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/OutlineComposite.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/OutlineComposite.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Painter.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Painter.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/PainterBin.comp
    ${CMAKE_CURRENT_SOURCE_DIR}/Blur.vert
    ${CMAKE_CURRENT_SOURCE_DIR}/Blur.frag
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/atmosphere.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/bindless.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/color.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/environment.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/lighting.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/shadows.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/view.slang
    ${CMAKE_CURRENT_SOURCE_DIR}/Library/nyx/surface.slang
)

set(NYX_SHADER_BUILD_DIR "${CMAKE_BINARY_DIR}/Binaries/$<CONFIG>/Shaders")

# Runs on every build; what is up to date is skipped in a moment
add_custom_target(NyxShaders ALL
    COMMAND "$<TARGET_FILE:NyxShaderCompiler>" build
        --source "${CMAKE_CURRENT_SOURCE_DIR}"
        --output "${NYX_SHADER_BUILD_DIR}"
        --search-path "${CMAKE_CURRENT_SOURCE_DIR}/Library"
    COMMENT "Compiling shaders"
    SOURCES ${NYX_SHADER_SOURCES}
    VERBATIM
)
add_dependencies(NyxShaders NyxShaderCompiler)
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${NYX_SHADER_SOURCES})

# Builds the shaders before <target>, whose "Shaders" folder they are compiled into. (Copying them
# for each program, as glslc's .spv files were, would have every program write the same folder at
# once in a parallel build.)
function(nyx_copy_shaders_next_to target)
    add_dependencies(${target} NyxShaders)
endfunction()

# Copies Slang's DLLs next to the executable of <target>, for programs that compile shaders
# (ShaderCompiler). slang.dll loads slang-compiler.dll, which compiles GLSL too; the SDK's other
# Slang DLLs aren't needed.
get_filename_component(NYX_VULKAN_SDK_BIN "${Vulkan_LIBRARY}/../../Bin" ABSOLUTE)
set(NYX_SHADER_COMPILER_DLLS
    "${NYX_VULKAN_SDK_BIN}/slang.dll"
    "${NYX_VULKAN_SDK_BIN}/slang-compiler.dll"
)
set_property(GLOBAL PROPERTY NYX_SHADER_COMPILER_DLLS "${NYX_SHADER_COMPILER_DLLS}")

function(nyx_copy_shader_compiler_next_to target)
    get_property(dlls GLOBAL PROPERTY NYX_SHADER_COMPILER_DLLS)
    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${dlls} "$<TARGET_FILE_DIR:${target}>"
        COMMAND_EXPAND_LISTS
    )
endfunction()
