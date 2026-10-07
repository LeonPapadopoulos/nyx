
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
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/Paths.cpp

    # Runtime/Engine
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/Application.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/Entity.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/TransformComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/MeshRendererComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/CameraComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/DirectionalLightComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/NameComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/ResourceManager.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/TypedHandle.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/WindowsWindow.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/cgltf_impl.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/BinaryArchive.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/ReflectedArchiveSerializer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/ComponentTypeRegistry.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Engine/src/ComponentRegistration.cpp

    # Runtime/Renderer
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/ImGuiTheme.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Mesh.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Shader.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Texture.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/CubemapTexture.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Material.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Renderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/EditorCamera.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/SceneViewTypes.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/SceneView.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/SceneViewCameraData.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanImGuiBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanRenderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanContext.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanSwapchain.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/OffscreenRenderTarget.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/VulkanViewportTarget.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/GltfImporter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/src/Extent2D.cpp

    # Reflection
    ${NYX_ENGINE_ROOT}/Reflection/src/ReflectionUtils.cpp
)

set(NYX_ENGINE_HEADERS
    # Runtime/Core
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Assertions.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Core.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Log.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/NyxEngineAPI.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Paths.h
    
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h

    # Runtime/Engine
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Application.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Layer.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Entity.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/TransformComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/MeshRendererComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/CameraComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/DirectionalLightComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/NameComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/ResourceManager.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/TypedHandle.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/Window.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/WindowsWindow.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/BinaryArchive.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/SceneSerializationTypes.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/ReflectedArchiveSerializer.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/ComponentTypeRegistry.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/ComponentRegistration.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/IAssetResolver.h
    ${NYX_ENGINE_ROOT}/Runtime/Engine/include/AssetReference.h

    # Runtime/Renderer
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/ImGuiTheme.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Mesh.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Shader.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Texture.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/CubemapTexture.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Material.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Renderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/EditorCamera.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/SceneViewTypes.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/SceneView.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/SceneViewCameraData.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanImGuiBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanRenderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanContext.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanSwapchain.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/OffscreenRenderTarget.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/VulkanViewportTarget.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/GltfImporter.h
    ${NYX_ENGINE_ROOT}/Runtime/Renderer/include/Extent2D.h

    # Reflection
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionTypes.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionMacros.h
    ${NYX_ENGINE_ROOT}/Reflection/include/PropertyValue.h
    ${NYX_ENGINE_ROOT}/Reflection/include/PropertyValueUtils.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionUtils.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectedPropertyRef.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionTraversal.h
)

# A static library: every program (the editor, later the game) links its own copy, so
# third-party state such as the ImGui context or GLFW exists exactly once per process.
add_library(NyxEngine STATIC
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
)

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found.")
endif()

target_compile_features(NyxEngine PUBLIC cxx_std_20)

# START Reflection / CodeGen
set(NYX_GENERATED_ROOT "${CMAKE_BINARY_DIR}/Generated")
set(NYX_GENERATED_RUNTIME_DIR "${NYX_GENERATED_ROOT}/Runtime")
set(NYX_REFLECTION_STAMP "${NYX_GENERATED_RUNTIME_DIR}/Reflection.stamp")
set(NYX_RUNTIME_REFLECTION_INIT_H "${NYX_GENERATED_RUNTIME_DIR}/Runtime.reflect.init.h")
set(NYX_RUNTIME_REFLECTION_INIT_CPP "${NYX_GENERATED_RUNTIME_DIR}/Runtime.reflect.init.cpp")

file(GLOB_RECURSE NYX_REFLECT_HEADER_INPUTS
    CONFIGURE_DEPENDS
    "${NYX_ENGINE_ROOT}/Runtime/Engine/include/*.h"
    "${NYX_ENGINE_ROOT}/Runtime/Renderer/include/*.h"
)

add_custom_command(
    OUTPUT
        "${NYX_REFLECTION_STAMP}"
        "${NYX_RUNTIME_REFLECTION_INIT_H}"
        "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NYX_GENERATED_RUNTIME_DIR}"
    COMMAND "$<TARGET_FILE:NyxHeaderTool>"
            --scan-root "${NYX_ENGINE_ROOT}/Runtime/Engine/include"
            --scan-root "${NYX_ENGINE_ROOT}/Runtime/Renderer/include"
            --output-dir "${NYX_GENERATED_RUNTIME_DIR}"
            --module-init-header "${NYX_RUNTIME_REFLECTION_INIT_H}"
            --module-init-cpp "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
    COMMAND ${CMAKE_COMMAND} -E touch "${NYX_REFLECTION_STAMP}"
    DEPENDS
        NyxHeaderTool
        ${NYX_REFLECT_HEADER_INPUTS}
    COMMENT "Generating reflection metadata"
    VERBATIM
)

add_custom_target(NyxGeneratedReflection
    DEPENDS
        "${NYX_REFLECTION_STAMP}"
        "${NYX_RUNTIME_REFLECTION_INIT_H}"
        "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
)

add_dependencies(NyxEngine NyxGeneratedReflection)

# Registers every reflected component type; called through RegisterComponentTypes().
target_sources(NyxEngine PRIVATE
    "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
)

set_source_files_properties(
    "${NYX_REFLECTION_STAMP}"
    "${NYX_RUNTIME_REFLECTION_INIT_H}"
    "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
    PROPERTIES GENERATED TRUE
)

# PUBLIC, because the component headers include their generated "Generated/Runtime/*.reflect.h".
target_include_directories(NyxEngine
    PUBLIC
        "${CMAKE_BINARY_DIR}"
)
# END Reflection / CodeGen

target_include_directories(NyxEngine
    PUBLIC
        ${NYX_ENGINE_ROOT}/Runtime/Core/include
        ${NYX_ENGINE_ROOT}/Runtime/Engine/include
        ${NYX_ENGINE_ROOT}/Runtime/Renderer/include
        ${NYX_ENGINE_ROOT}/Reflection/include
    PRIVATE
        ${NYX_ENGINE_ROOT}/Runtime/Core/src
        ${NYX_ENGINE_ROOT}/Runtime/Engine/src
        ${NYX_ENGINE_ROOT}/Runtime/Renderer/src
        ${CMAKE_SOURCE_DIR}/Dependencies/cgltf
)

target_link_libraries(NyxEngine
    PUBLIC
        spdlog::spdlog
        glfw
        imgui::imgui
        glm::glm
        Vulkan::Vulkan # Public headers such as Mesh.h include vulkan.hpp
    PRIVATE
        dwmapi
        stb_image
        NyxStartup # The startup banner, from the Startup module.
)


# NYXENGINE_STATIC turns NYXENGINE_API (NyxEngineAPI.h) into nothing: no DLL exports needed.
target_compile_definitions(NyxEngine
	PUBLIC NYXENGINE_STATIC
	PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>
    PUBLIC $<$<CONFIG:Release>:ENGINE_RELEASE>
	PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
    PUBLIC VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS # Same vulkan.hpp behavior wherever it is included
    PUBLIC GLM_ENABLE_EXPERIMENTAL # Quaternion utilities, used by the public TransformComponent.h
)

target_precompile_headers(NyxEngine
    PRIVATE ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h
)

# Tell Visual Studio to mimick actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
)
