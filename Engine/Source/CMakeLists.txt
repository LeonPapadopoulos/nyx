
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
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/ChildProcess.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/CrashHandler.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/FrameRateLimiter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/FixedStepClock.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/DebugDraw.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/FileWatcher.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/BinaryArchive.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/MeshData.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/PointCloudRecognizer.cpp

    # Runtime/Net
    ${NYX_ENGINE_ROOT}/Runtime/Net/src/NetConnection.cpp

    # Runtime/Platform
    ${NYX_ENGINE_ROOT}/Runtime/Platform/src/WindowPlacement.cpp

    # Runtime/Input
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputTypes.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputFrame.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputSystem.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/VirtualInputBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/GlfwInputBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputPipeline.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputTriggers.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputUser.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputTrace.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/ImGuiInput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputOutput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/SdlGamepadBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/FlyCameraInput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/FlyCameras.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/OrbitCameraInput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/ThirdPersonInput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputInspection.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputHistory.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputDebugView.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputAsset.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputChecks.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputRecipes.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputStream.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Input/src/InputRecording.cpp

    # Runtime/Scene
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/Entity.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/TransformComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/MeshRendererComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/CameraComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/CameraProjection.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/FlyCameraComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/CharacterComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/MoveCharacterCommand.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/OrbitCameraComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/DirectionalLightComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/EnvironmentComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/NameComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/PhysicsComponents.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ConstraintComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/GroundComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/GradientNoise.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/GroundMesh.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ScatterComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ScatterInstances.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ScatterBlockerComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/EntityGuid.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/GuidComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/EntityGuidLookup.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/SurfaceAnchor.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/PreviousTransformComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/MeshInstancesComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/RuntimeMeshComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/SurfaceDecalComponent.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/TypedHandle.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ReflectedArchiveSerializer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ComponentTypeRegistry.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ComponentInspection.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/ComponentRegistration.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Scene/src/SceneSerializer.cpp

    # Runtime/Sim
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/CommandQueue.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/GameWorld.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/PhysicsBodies.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/PhysicsConstraints.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/PhysicsMaterials.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/PhysicsPick.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/OrbitCameras.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/CharacterCommands.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Sim/src/SparseField.cpp

    # Runtime/Physics
    ${NYX_ENGINE_ROOT}/Runtime/Physics/src/PhysicsWorld.cpp

    # Runtime/Assets
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/cgltf_impl.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/GltfImporter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/ImageFile.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/MaterialAsset.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/PhysicsMaterialAsset.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Assets/src/ShaderCompileTypes.cpp

    # Runtime/UI
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/ImGuiTheme.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/Painter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/PainterCommands.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/PainterTiles.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/WindowPainter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UIAnimation.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UITheme.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UIThemeFile.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UIWidgets.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UIChrome.cpp
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/UIDiagnostics.cpp

    # Runtime/Render
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/AssetResolver.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/SurfaceDecals.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/DrawnWorldTime.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Mesh.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/RuntimeMeshCache.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/MeshInstanceCache.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/SceneCulling.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShadowCascades.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanShadows.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/AmbientOcclusion.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Atmosphere.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Bloom.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanBloom.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanAmbientOcclusion.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Texture.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/CubemapTexture.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Material.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Renderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/EditorCamera.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/SceneViewTypes.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/SceneView.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/SceneViewCameraData.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanImGuiBackend.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanContext.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanUtil.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanSwapchain.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/OffscreenRenderTarget.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanViewportTarget.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/Extent2D.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanPainter.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanGlass.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanBlur.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShaderCompiler.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShaderCompileService.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShaderPrograms.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShaderLog.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/ShaderCache.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanBindless.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanMaterials.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanEnvironment.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanDebugLines.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanEditorOverlays.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/RendererLiveEdits.cpp

    # Runtime/Link
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/EditorLink.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/EditorLinkMessages.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/LiveEdits.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/LiveInput.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/EditorLinkLogSink.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Link/src/EditorLinkRecorder.cpp

    # Runtime/App
    ${NYX_ENGINE_ROOT}/Runtime/App/src/Application.cpp
    ${NYX_ENGINE_ROOT}/Runtime/App/src/GameLaunchOptions.cpp
    ${NYX_ENGINE_ROOT}/Runtime/App/src/ThirdPersonPlayer.cpp

    # Reflection
    ${NYX_ENGINE_ROOT}/Reflection/src/ReflectionUtils.cpp
    ${NYX_ENGINE_ROOT}/Reflection/src/PropertyValueAccess.cpp
)

set(NYX_ENGINE_HEADERS
    # Runtime/Core
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Assertions.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Core.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Log.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/NyxEngineAPI.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Paths.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/ChildProcess.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/CrashHandler.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/FrameRateLimiter.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/FixedStepClock.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/DebugDraw.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/Profiler.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/FileWatcher.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/StringUtil.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/ClockTime.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/BinaryArchive.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/MeshData.h
    ${NYX_ENGINE_ROOT}/Runtime/Core/include/PointCloudRecognizer.h
    
    ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h

    # Runtime/Net
    ${NYX_ENGINE_ROOT}/Runtime/Net/include/NetConnection.h

    # Runtime/Input
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputTypes.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputFrame.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputSystem.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/VirtualInputBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/GlfwInputBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputActions.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputPipeline.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputTriggers.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputContexts.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputUser.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputTrace.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/ImGuiInput.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputOutput.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/SdlGamepadBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/FlyCameraInput.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/FlyCameras.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/OrbitCameraInput.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/ThirdPersonInput.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputInspection.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputHistory.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputDebugView.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputAsset.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputChecks.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputRecipes.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputStream.h
    ${NYX_ENGINE_ROOT}/Runtime/Input/include/InputRecording.h

    # Runtime/Scene
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/Entity.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/TransformComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/MeshRendererComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/CameraComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/CameraProjection.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/FlyCameraComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/CharacterComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/MoveCharacterCommand.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/OrbitCameraComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/DirectionalLightComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/EnvironmentComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/NameComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/PhysicsComponents.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ConstraintComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/GroundComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/GradientNoise.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/GroundMesh.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ScatterComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ScatterInstances.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ScatterBlockerComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/EntityGuid.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/GuidComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/TypedHandle.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/SceneSerializationTypes.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ReflectedArchiveSerializer.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ComponentTypeRegistry.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ComponentInspection.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/ComponentRegistration.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/SceneSerializer.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/IAssetResolver.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/AssetReference.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/EntityGuidLookup.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/SurfaceAnchor.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/PreviousTransformComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/RuntimeMeshComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/MeshInstancesComponent.h
    ${NYX_ENGINE_ROOT}/Runtime/Scene/include/SurfaceDecalComponent.h

    # Runtime/Sim
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/CommandQueue.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/SimSystem.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/GameWorld.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/PhysicsBodies.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/PhysicsConstraints.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/PhysicsMaterials.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/PhysicsPick.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/OrbitCameras.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/CharacterCommands.h
    ${NYX_ENGINE_ROOT}/Runtime/Sim/include/SparseField.h

    # Runtime/Physics
    ${NYX_ENGINE_ROOT}/Runtime/Physics/include/PhysicsWorld.h

    # Runtime/Assets
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/GltfImporter.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/ImageFile.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/ShaderLayout.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/BindlessTextures.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/MaterialAsset.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/PhysicsMaterialAsset.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/MaterialOptions.h
    ${NYX_ENGINE_ROOT}/Runtime/Assets/include/ShaderCompileTypes.h

    # Runtime/UI
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/ImGuiTheme.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/ImGuiConversions.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/ImGuiSource.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/Painter.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/PainterCommands.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/PainterTiles.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/PainterDrawOptions.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/WindowPainter.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UIAnimation.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UITheme.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UIThemeFile.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UIWidgets.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UIChrome.h
    ${NYX_ENGINE_ROOT}/Runtime/UI/include/UIDiagnostics.h

    # Runtime/Render
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Resource.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/AssetResolver.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/SurfaceDecals.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/DrawnWorldTime.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Mesh.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/RuntimeMeshCache.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/MeshInstanceCache.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/SceneCulling.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShadowCascades.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanShadows.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/AmbientOcclusion.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Atmosphere.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Bloom.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanBloom.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanAmbientOcclusion.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Texture.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/CubemapTexture.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Material.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Renderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/EditorCamera.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/SceneViewTypes.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/SceneView.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/SceneViewCameraData.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanImGuiBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanRenderer.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanContext.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanUtil.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanSwapchain.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/OffscreenRenderTarget.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanViewportTarget.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/Extent2D.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanPainter.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanGlass.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanBlur.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShaderCompiler.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShaderCompileService.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShaderPrograms.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShaderLog.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/ShaderCache.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanBindless.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanMaterials.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanEnvironment.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/RendererLiveEdits.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanUIBackend.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanDebugLines.h
    ${NYX_ENGINE_ROOT}/Runtime/Render/include/VulkanEditorOverlays.h

    # Runtime/Link
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/EditorLink.h
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/EditorLinkMessages.h
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/LiveEdits.h
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/LiveInput.h
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/EditorLinkLogSink.h
    ${NYX_ENGINE_ROOT}/Runtime/Link/include/EditorLinkRecorder.h

    # Runtime/Platform
    ${NYX_ENGINE_ROOT}/Runtime/Platform/include/Window.h
    ${NYX_ENGINE_ROOT}/Runtime/Platform/include/WindowsWindow.h

    # Runtime/App
    ${NYX_ENGINE_ROOT}/Runtime/App/include/Application.h
    ${NYX_ENGINE_ROOT}/Runtime/App/include/Layer.h
    ${NYX_ENGINE_ROOT}/Runtime/App/include/GameModule.h
    ${NYX_ENGINE_ROOT}/Runtime/App/include/GameLaunchOptions.h
    ${NYX_ENGINE_ROOT}/Runtime/App/include/ThirdPersonPlayer.h

    # Reflection
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionTypes.h
    ${NYX_ENGINE_ROOT}/Reflection/include/SourceLocation.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionMacros.h
    ${NYX_ENGINE_ROOT}/Reflection/include/PropertyValue.h
    ${NYX_ENGINE_ROOT}/Reflection/include/PropertyValueUtils.h
    ${NYX_ENGINE_ROOT}/Reflection/include/PropertyValueAccess.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionUtils.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectedPropertyRef.h
    ${NYX_ENGINE_ROOT}/Reflection/include/ReflectionTraversal.h
)

# Most engine code is identical for editor and game and only needs compiling once.
# The window and the renderer, with its UI backend, are compiled separately for each target,
# with the target's NYX_UI_INSPECTION.
add_library(NyxEngineCommon OBJECT
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
)
set(NYX_ENGINE_UI_SOURCES
    ${NYX_ENGINE_ROOT}/Runtime/Platform/src/WindowsWindow.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanRenderer.cpp
    ${NYX_ENGINE_ROOT}/Runtime/Render/src/VulkanUIBackend.cpp
)
add_library(NyxEngine STATIC ${NYX_ENGINE_UI_SOURCES})
add_library(NyxEditorEngine STATIC ${NYX_ENGINE_UI_SOURCES}
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/ImGuiSource.cpp)
target_link_libraries(NyxEngine PRIVATE NyxEngineCommon)
target_link_libraries(NyxEditorEngine PRIVATE NyxEngineCommon)

# Consumers inherit the matching UI API; no executable overrides a shared library's mode.
target_compile_definitions(NyxEngine PUBLIC NYX_UI_INSPECTION=0)
target_compile_definitions(NyxEditorEngine PUBLIC NYX_UI_INSPECTION=1)

find_package(Vulkan QUIET)
if(NOT Vulkan_FOUND)
    message(FATAL_ERROR "Vulkan SDK not found.")
endif()

# Slang, the shader compiler (ShaderCompiler.cpp), from the Vulkan SDK. Its headers come with
# Vulkan::Vulkan's include folder.
get_filename_component(NYX_VULKAN_LIB_DIR "${Vulkan_LIBRARY}" DIRECTORY)
find_library(NYX_SLANG_LIBRARY slang HINTS "${NYX_VULKAN_LIB_DIR}" REQUIRED)

# START Reflection / CodeGen
set(NYX_GENERATED_ROOT "${CMAKE_BINARY_DIR}/Generated")
set(NYX_GENERATED_RUNTIME_DIR "${NYX_GENERATED_ROOT}/Runtime")
set(NYX_REFLECTION_STAMP "${NYX_GENERATED_RUNTIME_DIR}/Reflection.stamp")
set(NYX_RUNTIME_REFLECTION_INIT_H "${NYX_GENERATED_RUNTIME_DIR}/Runtime.reflect.init.h")
set(NYX_RUNTIME_REFLECTION_INIT_CPP "${NYX_GENERATED_RUNTIME_DIR}/Runtime.reflect.init.cpp")
set(NYX_RUNTIME_REFLECTION_SOURCES_CPP "${NYX_GENERATED_RUNTIME_DIR}/Runtime.reflect.sources.cpp")

file(GLOB_RECURSE NYX_REFLECT_HEADER_INPUTS
    CONFIGURE_DEPENDS
    "${NYX_ENGINE_ROOT}/Runtime/Scene/include/*.h"
    "${NYX_ENGINE_ROOT}/Runtime/Input/include/*.h"
    "${NYX_ENGINE_ROOT}/Runtime/Assets/include/*.h"
)

add_custom_command(
    OUTPUT
        "${NYX_REFLECTION_STAMP}"
        "${NYX_RUNTIME_REFLECTION_INIT_H}"
        "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
        "${NYX_RUNTIME_REFLECTION_SOURCES_CPP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory "${NYX_GENERATED_RUNTIME_DIR}"
    COMMAND "$<TARGET_FILE:NyxHeaderTool>"
            --scan-root "${NYX_ENGINE_ROOT}/Runtime/Scene/include"
            --scan-root "${NYX_ENGINE_ROOT}/Runtime/Input/include"
            --scan-root "${NYX_ENGINE_ROOT}/Runtime/Assets/include"
            --output-dir "${NYX_GENERATED_RUNTIME_DIR}"
            --module-init-header "${NYX_RUNTIME_REFLECTION_INIT_H}"
            --module-init-cpp "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
            --editor-sources-cpp "${NYX_RUNTIME_REFLECTION_SOURCES_CPP}"
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
        "${NYX_RUNTIME_REFLECTION_SOURCES_CPP}"
)

# Registers every reflected component type; called through RegisterComponentTypes().
target_sources(NyxEngineCommon PRIVATE
    "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
)

set_source_files_properties(
    "${NYX_REFLECTION_STAMP}"
    "${NYX_RUNTIME_REFLECTION_INIT_H}"
    "${NYX_RUNTIME_REFLECTION_INIT_CPP}"
    "${NYX_RUNTIME_REFLECTION_SOURCES_CPP}"
    PROPERTIES GENERATED TRUE
)

# PUBLIC, because the component headers include their generated "Generated/Runtime/*.reflect.h".
foreach(NYX_ENGINE_TARGET NyxEngineCommon NyxEngine NyxEditorEngine)
    add_dependencies(${NYX_ENGINE_TARGET} NyxGeneratedReflection)
    target_compile_features(${NYX_ENGINE_TARGET} PUBLIC cxx_std_20)
    target_include_directories(${NYX_ENGINE_TARGET}
        PUBLIC
            "${CMAKE_BINARY_DIR}"
    )
    # END Reflection / CodeGen

    target_include_directories(${NYX_ENGINE_TARGET}
        PUBLIC
            ${NYX_ENGINE_ROOT}/Runtime/Core/include
            ${NYX_ENGINE_ROOT}/Runtime/Net/include
            ${NYX_ENGINE_ROOT}/Runtime/Input/include
            ${NYX_ENGINE_ROOT}/Runtime/Scene/include
            ${NYX_ENGINE_ROOT}/Runtime/Sim/include
            ${NYX_ENGINE_ROOT}/Runtime/Physics/include
            ${NYX_ENGINE_ROOT}/Runtime/Assets/include
            ${NYX_ENGINE_ROOT}/Runtime/UI/include
            ${NYX_ENGINE_ROOT}/Runtime/Render/include
            ${NYX_ENGINE_ROOT}/Runtime/Link/include
            ${NYX_ENGINE_ROOT}/Runtime/Platform/include
            ${NYX_ENGINE_ROOT}/Runtime/App/include
            ${NYX_ENGINE_ROOT}/Reflection/include
        PRIVATE
            ${NYX_ENGINE_ROOT}/Runtime/Core/src
            ${PROJECT_SOURCE_DIR}/Dependencies/cgltf
            ${PROJECT_SOURCE_DIR}/Dependencies/glfw/deps # stb_image_write.h, for ImageFile.cpp
    )

    target_link_libraries(${NYX_ENGINE_TARGET}
        PUBLIC
            spdlog::spdlog
            glfw
            SDL3::SDL3-static # Gamepads (SdlGamepadBackend)
            imgui::imgui
            glm::glm
            Vulkan::Vulkan # Public headers such as Mesh.h include vulkan.hpp
        PRIVATE
            dwmapi
            ws2_32 # Winsock, for the editor link (NetConnection.cpp)
            "${NYX_SLANG_LIBRARY}"
            delayimp # slang.dll is delay-loaded, see below
            dbghelp # Call stacks with symbols, for crash reports (CrashHandler.cpp)
            stb_image
            NyxStartup # The startup banner, from the Startup module.
    )

    # Physics (PhysicsWorld.cpp) on Jolt, whose headers only the Physics folder includes
    target_link_libraries(${NYX_ENGINE_TARGET} PRIVATE Jolt)

    # The profiler (Profiler.h): Tracy's client, PUBLIC so every program gets it, or nothing at all
    target_compile_definitions(${NYX_ENGINE_TARGET} PUBLIC "NYX_PROFILER=$<BOOL:${NYX_PROFILER}>")
    if(NYX_PROFILER)
        target_link_libraries(${NYX_ENGINE_TARGET} PUBLIC Tracy::TracyClient)
    endif()


    # NYXENGINE_STATIC turns NYXENGINE_API (NyxEngineAPI.h) into nothing: no DLL exports needed.
    target_compile_definitions(${NYX_ENGINE_TARGET}
        PUBLIC NYXENGINE_STATIC
        PUBLIC $<$<CONFIG:Debug>:ENGINE_DEBUG>
        PUBLIC $<$<CONFIG:Release>:ENGINE_RELEASE>
        PUBLIC $<$<CONFIG:Debug>:ASSERTIONS_ENABLED>
        PUBLIC "NYX_DEBUG_DRAW=$<NOT:$<CONFIG:Release>>" # DebugDraw.h: none in Release builds
        PUBLIC VULKAN_HPP_HANDLE_ERROR_OUT_OF_DATE_AS_SUCCESS # Same vulkan.hpp behavior wherever it is included
        PUBLIC GLM_ENABLE_EXPERIMENTAL # Quaternion utilities, used by the public TransformComponent.h
    )

    # Only programs that compile shaders need slang.dll next to them (nyx_copy_shader_compiler_next_to());
    # the others never load it. ShaderCompiler loads it up front and reports when it is missing.
    # Programs that don't use ShaderCompiler at all import nothing from slang.dll: /IGNORE:4199
    # silences the linker's note that the delay-load has nothing to do there.
    target_link_options(${NYX_ENGINE_TARGET} INTERFACE "/DELAYLOAD:slang.dll" "/IGNORE:4199")

    target_precompile_headers(${NYX_ENGINE_TARGET}
        PRIVATE ${NYX_ENGINE_ROOT}/Runtime/Core/src/NyxPCH.h
    )
endforeach()

# Game modules: nyx_add_game_module(), nyx_link_game_module(), and the empty NyxNoGameModule
include(${CMAKE_CURRENT_SOURCE_DIR}/GameModules.cmake)

# Link messages, recordings and scene files as readable text, for the editor's message log and
# NyxDump. The game doesn't need it, so it is in none of the engine libraries; each program that
# uses it links the engine library it uses as well.
set(NYX_LINK_TOOLS_SOURCES
    ${NYX_ENGINE_ROOT}/LinkTools/src/EditorLinkMessageText.cpp
    ${NYX_ENGINE_ROOT}/LinkTools/src/EditorLinkRecordingPrinter.cpp
    ${NYX_ENGINE_ROOT}/LinkTools/src/ReflectedArchivePrinter.cpp
    ${NYX_ENGINE_ROOT}/LinkTools/src/ScenePrinter.cpp
    ${NYX_ENGINE_ROOT}/LinkTools/src/InputFilePrinter.cpp
)
set(NYX_LINK_TOOLS_HEADERS
    ${NYX_ENGINE_ROOT}/LinkTools/include/EditorLinkMessageText.h
    ${NYX_ENGINE_ROOT}/LinkTools/include/EditorLinkRecordingPrinter.h
    ${NYX_ENGINE_ROOT}/LinkTools/include/ReflectedArchivePrinter.h
    ${NYX_ENGINE_ROOT}/LinkTools/include/ScenePrinter.h
    ${NYX_ENGINE_ROOT}/LinkTools/include/InputFilePrinter.h
)
add_library(NyxLinkTools OBJECT ${NYX_LINK_TOOLS_SOURCES} ${NYX_LINK_TOOLS_HEADERS})
target_include_directories(NyxLinkTools PUBLIC ${NYX_ENGINE_ROOT}/LinkTools/include)
# The engine's headers and settings only: an object library takes no objects from another
target_link_libraries(NyxLinkTools PRIVATE NyxEngineCommon)
add_dependencies(NyxLinkTools NyxGeneratedReflection)

# Tell Visual Studio to mimick actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_ENGINE_SOURCES}
	${NYX_ENGINE_HEADERS}
    ${NYX_ENGINE_UI_SOURCES}
    ${NYX_ENGINE_ROOT}/Runtime/UI/src/ImGuiSource.cpp
    ${NYX_LINK_TOOLS_SOURCES}
    ${NYX_LINK_TOOLS_HEADERS}
)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
