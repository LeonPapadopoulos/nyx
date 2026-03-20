
# automatically collect and include all .h/.cpp files
# CONFIGURE_DEPENDS to automatically check for changes upon build request
# Many sources online stating it is better to refrain from this approach; to be investigated
#[[
file(GLOB_RECURSE ENGINE_SOURCES
    CONFIGURE_DEPENDS
    src/*.cpp
)
file(GLOB_RECURSE ENGINE_HEADERS
    CONFIGURE_DEPENDS
    include/*.h
)
]]


set(NYX_ENGINE_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

set(NYX_ENGINE_SOURCES
    # Runtime/Core
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/Assertions.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/Core.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/Log.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.cpp

    # Runtime/Engine
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/Application.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/Entity.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/ResourceManager.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/TypedHandle.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/WindowsWindow.cpp

    # Runtime/Renderer
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/ImGuiTheme.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Mesh.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Shader.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Texture.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Renderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanImGuiBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanRenderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanContext.cpp

    # Editor
    # ${NYX_ENGINE_ROOT}/Editor/src/EditorLayer.cpp
)

set(NYX_ENGINE_HEADERS
    # Runtime/Core
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Assertions.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Core.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Log.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/NyxEngineAPI.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h

    # Runtime/Engine
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Application.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Entity.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/ResourceManager.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/TypedHandle.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Window.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/WindowsWindow.h

    # Runtime/Renderer
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/ImGuiTheme.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Mesh.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Shader.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Texture.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Renderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanImGuiBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanRenderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanContext.h

    # Editor
    # ${NYX_ENGINE_ROOT}/Editor/include/EditorLayer.h
)

add_library(NyxEngine SHARED 
	${NYX_ENGINE_SOURCES} 
	${NYX_ENGINE_HEADERS}
)

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found.")
endif()

target_compile_features(NyxEngine PUBLIC cxx_std_20)

target_include_directories(NyxEngine
    PUBLIC
        ${NYX_ENGINE_ROOT}/Runtime/Core/include
        ${NYX_ENGINE_ROOT}/Runtime/Engine/include
        ${NYX_ENGINE_ROOT}/Runtime/Renderer/include
        ${NYX_ENGINE_ROOT}/Editor/include
    PRIVATE
        ${NYX_ENGINE_ROOT}/Runtime/Core/src
        ${NYX_ENGINE_ROOT}/Runtime/Engine/src
        ${NYX_ENGINE_ROOT}/Runtime/Renderer/src
        ${NYX_ENGINE_ROOT}/Editor/src
)

target_link_libraries(NyxEngine
    PUBLIC
        spdlog::spdlog
        glfw
        imgui::imgui
        glm::glm
    PRIVATE
        dwmapi
        Vulkan::Vulkan
        stb_image
)


# define ENGINE_EXPORTS so EngineAPI.h dll macros toggle from export to import
# PRIVATE, so it doesn't apply for modules linking to Engine
target_compile_definitions(NyxEngine 
	PRIVATE NYXENGINE_EXPORTS
	PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>
    PUBLIC $<$<CONFIG:Release>:ENGINE_RELEASE>
	PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
    PRIVATE VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS
)

target_precompile_headers(NyxEngine
    PRIVATE ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h
)

# Tell Visual Studio to mimick actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
)
