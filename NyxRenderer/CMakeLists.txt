set(NYX_RENDERER_SOURCES
    src/NyxRendererPCH.cpp
    src/Texture.cpp
    src/Mesh.cpp
    src/Shader.cpp
)

set(NYX_RENDERER_HEADERS
    src/NyxRendererPCH.h
    include/NyxRendererAPI.h
    include/Texture.h
    include/Mesh.h
    include/Shader.h
)

add_library(NyxRenderer SHARED ${NYX_RENDERER_SOURCES} ${NYX_RENDERER_HEADERS})

target_compile_features(NyxRenderer PUBLIC cxx_std_20)

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR
        "Vulkan SDK not found.\n"
        "Install the Vulkan SDK, then regenerate project files.")
endif()

target_include_directories(NyxRenderer
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(NyxRenderer
    PUBLIC NyxCore
    PRIVATE Vulkan::Vulkan
    # PRIVATE glfw
    # PRIVATE imgui::imgui
    PUBLIC glm::glm
    PRIVATE stb_image
)

target_compile_definitions(NyxRenderer
    # PUBLIC NYXRENDERER_STATIC # for STATIC library
    PRIVATE NYXRENDERER_EXPORTS # For SHARED library
    PUBLIC $<$<CONFIG:Debug>:RENDERER_DEBUG>
    PUBLIC $<$<CONFIG:Release>:RENDERER_RELEASE>
    PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
    PRIVATE VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
    ${NYX_RENDERER_SOURCES}
    ${NYX_RENDERER_HEADERS}
)

target_precompile_headers(NyxRenderer 
    PRIVATE src/NyxRendererPCH.h
)