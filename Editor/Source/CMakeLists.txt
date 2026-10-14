# Compiled straight into the executable, not a library: the asset type registrations
# (*AssetTypeRegistration.cpp) register themselves through global objects, which the
# linker would drop from a static library because nothing references them.
set(NYX_EDITOR_SOURCES
    main.cpp
    src/EditorLayer.cpp
    src/SceneEditor.cpp
    src/SceneOutlinerPanel.cpp
    src/DetailsPanel.cpp
    src/SceneViewPanel.cpp
    src/EditorCameraController.cpp
    src/PainterTestWindow.cpp
    src/MockupOverlay.cpp
    src/FileDialogs.cpp
    src/SceneFilePopups.cpp
    src/PreferencesWindow.cpp
    src/ThemeEditorWindow.cpp
    src/PlaySession.cpp
    src/PlayControls.cpp
    src/CrashCardsPanel.cpp
    src/DefaultScene.cpp
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
    src/InspectorDrawContext.cpp
    src/TransformGizmo.cpp
    src/InspectorTargetId.cpp
    src/EditorTransactionContext.cpp
    src/ReflectedPropertyDrawer.cpp
    src/ReflectedPropertyAccess.cpp
    src/TransactionSystem.cpp
    src/TransactionDiffUtil.cpp
    src/SceneEntityTransactionDomain.cpp
    src/RootObjectSnapshotUtils.cpp
    src/PropertyWidgetRegistry.cpp
    src/AssetDatabase.cpp
    src/AssetBrowserPanel.cpp
    src/AssetTypeRegistry.cpp
    src/SceneAssetTypeRegistration.cpp
    src/MeshAssetTypeRegistration.cpp
    src/MaterialAssetTypeRegistration.cpp
    src/MaterialDocument.cpp
    src/MaterialParametersPanel.cpp
    src/MaterialPreviewPanel.cpp
    src/ShaderLogPanel.cpp
    src/AssetDragDrop.cpp
    src/NewMaterial.cpp
    src/Workspaces.cpp
    src/ShaderAssetTypeRegistration.cpp
)

set(NYX_EDITOR_HEADERS
    src/EditorPCH.h
    include/EditorLayer.h
    include/ThemeEditorWindow.h
    include/MaterialDocument.h
    include/MaterialParametersPanel.h
    include/MaterialPreviewPanel.h
    include/ShaderLogPanel.h
    include/AssetDragDrop.h
    include/NewMaterial.h
    include/Workspaces.h
    include/SceneEditor.h
    include/SceneOutlinerPanel.h
    include/DetailsPanel.h
    include/SceneViewPanel.h
    include/EditorCameraController.h
    include/PainterTestWindow.h
    include/MockupOverlay.h
    include/FileDialogs.h
    include/SceneFilePopups.h
    include/PreferencesWindow.h
    include/PlaySession.h
    include/PlayControls.h
    include/CrashCardsPanel.h
    include/DefaultScene.h
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
    include/InspectorDrawContext.h
    include/TransformGizmo.h
    include/InspectorTargetId.h
    include/EditorTransactionContext.h
    include/ReflectedPropertyDrawer.h
    include/ReflectedPropertyAccess.h
    include/TransactionTypes.h
    include/TransactionSystem.h
    include/TransactionDiffUtil.h
    include/TransactionObjectRef.h
    include/TransactionObjectRefHelpers.h
    include/TransactionDomain.h
    include/SceneEntityTransactionDomain.h
    include/ReflectedObjectSnapshot.h
    include/RootObjectSnapshotUtils.h
    include/PropertyWidgetRegistry.h
    include/AssetDatabase.h
    include/AssetBrowserPanel.h
    include/AssetTypeRegistry.h
    include/EditorAssetActivationContext.h
)

# The editor and its test targets keep debug information in the object files (/Z7), not in a
# compiler PDB (/Zi). The test targets reuse the editor's precompiled header, and with /Zi each
# of them compiles into its own copy of the editor's NyxEditor.pdb. Parallel builds then write
# these same-named copies at once, which corrupts them (linker warnings LNK4020 and LNK4204).
# With /Z7 there is no compiler PDB to copy: the header's types are in cmake_pch.obj.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

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
# Live shader reload compiles in NyxShaderCompiler.exe, which sits next to the editor
add_dependencies(NyxEditor NyxShaderCompiler)

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

    # The pieces EditorLayer is built from: SceneEditor's commands and hotkeys, and the panels drawn headless
    add_executable(NyxEditorPanelTests tests/EditorPanelTests.cpp ${NYX_EDITOR_TEST_SOURCES}
        "${NYX_EDITOR_REFLECTION_SOURCES}")
    target_include_directories(NyxEditorPanelTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxEditorPanelTests PRIVATE NyxEditorEngine ole32 oleaut32 shell32 comsuppw)
    add_dependencies(NyxEditorPanelTests NyxGeneratedReflection)
    target_precompile_headers(NyxEditorPanelTests REUSE_FROM NyxEditor)
    set_property(TARGET NyxEditorPanelTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorPanelTests COMMAND NyxEditorPanelTests)

    # The material document and the Parameters window, headless, over a stand-in for the renderer
    add_executable(NyxMaterialEditorTests tests/MaterialEditorTests.cpp src/MaterialDocument.cpp src/MaterialParametersPanel.cpp src/MaterialPreviewPanel.cpp src/ShaderLogPanel.cpp
        src/NewMaterial.cpp src/Workspaces.cpp src/AssetDragDrop.cpp src/AssetTypeRegistry.cpp src/AssetDatabase.cpp)
    target_include_directories(NyxMaterialEditorTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxMaterialEditorTests PRIVATE NyxEditorEngine)
    add_dependencies(NyxMaterialEditorTests NyxGeneratedReflection)
    target_precompile_headers(NyxMaterialEditorTests REUSE_FROM NyxEditor)
    set_property(TARGET NyxMaterialEditorTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxMaterialEditorTests COMMAND NyxMaterialEditorTests)

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
        src/RootObjectSnapshotUtils.cpp src/ReflectedPropertyAccess.cpp
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

    # The fly camera of the scene views, with virtual keyboard and mouse and stand-in views
    add_executable(NyxEditorCameraTests tests/EditorCameraTests.cpp src/EditorCameraController.cpp)
    target_include_directories(NyxEditorCameraTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxEditorCameraTests PRIVATE NyxEditorEngine)
    target_precompile_headers(NyxEditorCameraTests REUSE_FROM NyxEditor)
    add_dependencies(NyxEditorCameraTests NyxGeneratedReflection)
    set_property(TARGET NyxEditorCameraTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorCameraTests COMMAND NyxEditorCameraTests)

    # Play setups and Play All: saving setups, tiling the windows, choosing the games
    add_executable(NyxPlayWallTests tests/PlayWallTests.cpp src/PlayWall.cpp src/EditorPreferences.cpp)
    target_include_directories(NyxPlayWallTests PRIVATE include src ${CMAKE_CURRENT_BINARY_DIR})
    target_link_libraries(NyxPlayWallTests PRIVATE NyxEditorEngine ole32 shell32)
    target_precompile_headers(NyxPlayWallTests REUSE_FROM NyxEditor)
    add_dependencies(NyxPlayWallTests NyxGeneratedReflection)
    set_property(TARGET NyxPlayWallTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxPlayWallTests COMMAND NyxPlayWallTests)
endif()