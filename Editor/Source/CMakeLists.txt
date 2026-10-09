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
# ImGui saves the editor's window layout (imgui.ini) in the working directory
set_target_properties(NyxEditor PROPERTIES
    VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NyxEditor>"
)

#######################################################################################
## Shader

# Compiled by Engine/Shaders; the renderer loads them from a "Shaders" folder next to the .exe
nyx_copy_shaders_next_to(NyxEditor)

#######################################################################################