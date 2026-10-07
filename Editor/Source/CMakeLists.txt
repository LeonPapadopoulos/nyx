# Compiled straight into the executable, not a library: the asset type registrations
# (*AssetTypeRegistration.cpp) register themselves through global objects, which the
# linker would drop from a static library because nothing references them.
set(NYX_EDITOR_SOURCES
    main.cpp
    src/EditorLayer.cpp
    src/SceneDocument.cpp
    src/EditorProperty.cpp
    src/ComponentEditorRegistration.cpp
    src/ComponentEditors.cpp
    src/InspectorDrawContext.cpp
    src/TransformGizmo.cpp
    src/InspectorTargetId.cpp
    src/InspectorTargetIdHelpers.cpp
    src/EditableObjectTraits.cpp
    src/EditorTransactionContext.cpp
    src/EditableTargetResolver.cpp
    src/TransactionHistory.cpp
    src/DiffUtil.cpp
    src/EditableObjectRegistrations.cpp
    src/ReflectedPropertyDrawer.cpp
    src/ReflectedPropertyAccess.cpp
    src/ReflectedTransactionSystem.cpp
    src/ReflectedDiffUtil.cpp
    src/TransactionSystem.cpp
    src/TransactionDiffUtil.cpp
    src/EditorTransactionSubscriber.cpp
    src/SceneEntityTransactionDomain.cpp
    src/ReflectedObjectSerialization.cpp
    src/RootObjectSnapshotUtils.cpp
    src/PropertyWidgetRegistry.cpp
    src/SceneSerializer.cpp
    src/AssetDatabase.cpp
    src/AssetBrowserPanel.cpp
    src/AssetTypeRegistry.cpp
    src/SceneAssetTypeRegistration.cpp
    src/MeshAssetTypeRegistration.cpp
    src/MaterialAssetTypeRegistration.cpp
)

set(NYX_EDITOR_HEADERS
    src/EditorPCH.h
    include/EditorLayer.h
    include/SceneDocument.h
    include/EditorProperty.h
    include/ComponentEditorRegistration.h
    include/ComponentEditors.h
    include/InspectorDrawContext.h
    include/TransformGizmo.h
    include/InspectorTargetId.h
    include/InspectorTargetIdHelpers.h
    include/EditableObjectTraits.h
    include/EditorTransactionContext.h
    include/EditableTargetResolver.h
    include/TransactionHistory.h
    include/DiffUtil.h
    include/EditableObjectRegistrations.h
    include/ReflectedPropertyDrawer.h
    include/ReflectedPropertyAccess.h
    include/ReflectedTransactionSystem.h
    include/ReflectedDiffUtil.h
    include/TransactionTypes.h
    include/TransactionSystem.h
    include/TransactionDiffUtil.h
    include/EditorTransactionSubscriber.h
    include/TransactionObjectRef.h
    include/TransactionObjectRefHelpers.h
    include/SceneEntitySnapshot.h
    include/TransactionDomain.h
    include/SceneEntityTransactionDomain.h
    include/ReflectedObjectSnapshot.h
    include/ReflectedObjectSerialization.h
    include/RootObjectSnapshotUtils.h
    include/PropertyWidgetRegistry.h
    include/SceneSerializer.h
    include/AssetDatabase.h
    include/AssetBrowserPanel.h
    include/AssetTypeRegistry.h
    include/EditorAssetActivationContext.h
)

add_executable(NyxEditor
	${NYX_EDITOR_SOURCES}
	${NYX_EDITOR_HEADERS}
)

target_include_directories(NyxEditor
    PRIVATE
        include
        src
)

target_link_libraries(NyxEditor
	PRIVATE NyxEngine
)

# The editor includes component headers, which include their generated reflection code.
add_dependencies(NyxEditor NyxGeneratedReflection)

target_precompile_headers(NyxEditor
    PRIVATE src/EditorPCH.h
)

# Tell Visual Studio to mimic the actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_EDITOR_SOURCES}
	${NYX_EDITOR_HEADERS}
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
    ${NYX_SHADER_SOURCE_DIR}/Grid.vert
    ${NYX_SHADER_SOURCE_DIR}/Grid.frag
    ${NYX_SHADER_SOURCE_DIR}/Skybox.vert
    ${NYX_SHADER_SOURCE_DIR}/Skybox.frag
    ${NYX_SHADER_SOURCE_DIR}/DebugLines.vert
    ${NYX_SHADER_SOURCE_DIR}/DebugLines.frag
    ${NYX_SHADER_SOURCE_DIR}/Picking.vert
    ${NYX_SHADER_SOURCE_DIR}/Picking.frag
    ${NYX_SHADER_SOURCE_DIR}/SelectionMask.vert
    ${NYX_SHADER_SOURCE_DIR}/SelectionMask.frag
    ${NYX_SHADER_SOURCE_DIR}/OutlineComposite.vert
    ${NYX_SHADER_SOURCE_DIR}/OutlineComposite.frag
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

# Custom target to catch changes to shader files and have them recompiled automatically on build
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