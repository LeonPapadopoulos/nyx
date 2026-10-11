# Compiled straight into the executable, not a library: the asset type registrations
# (*AssetTypeRegistration.cpp) register themselves through global objects, which the
# linker would drop from a static library because nothing references them.
set(NYX_EDITOR_SOURCES
    main.cpp
    src/EditorLayer.cpp
    src/GameInstance.cpp
    src/PlayWall.cpp
    src/GameLinkPanel.cpp
    src/GameLinkSubscriber.cpp
    src/ComponentPostLoadSubscriber.cpp
    src/ComponentEdits.cpp
    src/EntityCopies.cpp
    src/EditorPreferences.cpp
    src/ImGuiDebugTools.cpp
    src/ReflectionSourceRegistry.cpp
    src/SourceNavigation.cpp
    src/UISourceInspector.cpp
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
    include/GameInstance.h
    include/PlayWall.h
    include/GameLinkPanel.h
    include/GameLinkSubscriber.h
    include/ComponentPostLoadSubscriber.h
    include/ComponentEdits.h
    include/EntityCopies.h
    include/EditorPreferences.h
    include/ImGuiDebugTools.h
    include/ReflectionSourceRegistry.h
    include/SourceNavigation.h
    include/UISourceInspector.h
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

set(NYX_EDITOR_REFLECTION_SOURCES "${CMAKE_BINARY_DIR}/Generated/Runtime/Runtime.reflect.sources.cpp")
target_sources(NyxEditor PRIVATE "${NYX_EDITOR_REFLECTION_SOURCES}")

configure_file(src/EditorBuildConfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/EditorBuildConfig.h @ONLY)

target_include_directories(NyxEditor
    PRIVATE
        include
        src
        ${CMAKE_CURRENT_BINARY_DIR}
)

target_link_libraries(NyxEditor
	PRIVATE NyxEditorEngine ole32 oleaut32 shell32 comsuppw
)

# The editor includes component headers, which include their generated reflection code.
add_dependencies(NyxEditor NyxGeneratedReflection)

# Play starts NyxGame from next to the editor, so building the editor builds the game too.
add_dependencies(NyxEditor NyxGame)

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

if(BUILD_TESTING)
    # Exercise the editor's real Open/New paths without starting a window or GPU device.
    set(NYX_EDITOR_TEST_SOURCES ${NYX_EDITOR_SOURCES})
    list(REMOVE_ITEM NYX_EDITOR_TEST_SOURCES main.cpp)

    add_executable(NyxEditorSceneTests tests/SceneReplacementTests.cpp ${NYX_EDITOR_TEST_SOURCES}
        "${NYX_EDITOR_REFLECTION_SOURCES}")
    target_include_directories(NyxEditorSceneTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxEditorSceneTests PRIVATE NyxEditorEngine ole32 oleaut32 shell32 comsuppw)
    add_dependencies(NyxEditorSceneTests NyxGeneratedReflection)
    target_precompile_headers(NyxEditorSceneTests REUSE_FROM NyxEditor)
    set_property(TARGET NyxEditorSceneTests PROPERTY FOLDER "Tests")

    add_test(NAME NyxEditorSceneTests COMMAND NyxEditorSceneTests)

    add_executable(NyxUISourceTests tests/UISourceInspectorTests.cpp
        src/UISourceInspector.cpp src/EditorPreferences.cpp src/SourceNavigation.cpp src/ImGuiDebugTools.cpp
        src/ReflectionSourceRegistry.cpp "${NYX_EDITOR_REFLECTION_SOURCES}")
    target_include_directories(NyxUISourceTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxUISourceTests PRIVATE NyxEditorEngine ole32 oleaut32 shell32 comsuppw)
    target_precompile_headers(NyxUISourceTests REUSE_FROM NyxEditor)
    add_dependencies(NyxUISourceTests NyxGeneratedReflection)
    set_property(TARGET NyxUISourceTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxUISourceTests COMMAND NyxUISourceTests)

    # Edits, undo and redo turned into live edit messages, and applied to a second world as the game does
    add_executable(NyxGameLinkSubscriberTests tests/GameLinkSubscriberTests.cpp
        src/GameLinkSubscriber.cpp src/ComponentPostLoadSubscriber.cpp src/ComponentEdits.cpp src/EntityCopies.cpp src/TransactionSystem.cpp src/TransactionDiffUtil.cpp src/SceneEntityTransactionDomain.cpp
        src/RootObjectSnapshotUtils.cpp src/ReflectedObjectSerialization.cpp src/ReflectedPropertyAccess.cpp
        src/SceneDocument.cpp src/InspectorTargetId.cpp src/InspectorDrawContext.cpp)
    target_include_directories(NyxGameLinkSubscriberTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxGameLinkSubscriberTests PRIVATE NyxEditorEngine)
    target_precompile_headers(NyxGameLinkSubscriberTests REUSE_FROM NyxEditor)
    add_dependencies(NyxGameLinkSubscriberTests NyxGeneratedReflection)
    set_property(TARGET NyxGameLinkSubscriberTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxGameLinkSubscriberTests COMMAND NyxGameLinkSubscriberTests)

    # Several games at once, as Play starts them: this program starts copies of itself as stand-in games
    add_executable(NyxGameInstanceTests tests/GameInstanceTests.cpp src/GameInstance.cpp)
    target_include_directories(NyxGameInstanceTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxGameInstanceTests PRIVATE NyxEditorEngine)
    target_precompile_headers(NyxGameInstanceTests REUSE_FROM NyxEditor)
    add_dependencies(NyxGameInstanceTests NyxGeneratedReflection)
    set_property(TARGET NyxGameInstanceTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxGameInstanceTests COMMAND NyxGameInstanceTests)

    # Play setups and Play All: saving setups, tiling the windows, choosing the games
    add_executable(NyxPlayWallTests tests/PlayWallTests.cpp src/PlayWall.cpp src/EditorPreferences.cpp)
    target_include_directories(NyxPlayWallTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxPlayWallTests PRIVATE NyxEditorEngine ole32 shell32)
    target_precompile_headers(NyxPlayWallTests REUSE_FROM NyxEditor)
    add_dependencies(NyxPlayWallTests NyxGeneratedReflection)
    set_property(TARGET NyxPlayWallTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxPlayWallTests COMMAND NyxPlayWallTests)
endif()