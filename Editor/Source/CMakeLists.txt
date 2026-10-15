# The editor without its main(): a library that NyxEditor and the editor's test targets link, so
# the editor compiles once
set(NYX_EDITOR_SOURCES
    src/EditorLayer.cpp
    src/SceneEditor.cpp
    src/SceneOutlinerPanel.cpp
    src/DetailsPanel.cpp
    src/SceneViewPanel.cpp
    src/EditorCameraController.cpp
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
    src/EditorAssetTypes.cpp
    src/MaterialDocument.cpp
    src/MaterialParametersPanel.cpp
    src/MaterialPreviewPanel.cpp
    src/ShaderLogPanel.cpp
    src/AssetDragDrop.cpp
    src/NewMaterial.cpp
    src/Workspaces.cpp
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
)

# The prototype and diagnostics tools (NYX_DEV_TOOLS), with the painter's test sheet that Painter
# Test shows. The library has them in every configuration, so the test targets can test them; an
# executable only takes the library's objects it uses, and NyxEditor uses them in Debug builds
# only, where NYX_DEV_TOOLS lets EditorLayer own them.
set(NYX_DEV_TOOLS_DIR ${PROJECT_SOURCE_DIR}/Engine/Source/DevTools)
set(NYX_EDITOR_DEV_TOOL_SOURCES
    src/PainterTestWindow.cpp
    src/MockupOverlay.cpp
    ${NYX_DEV_TOOLS_DIR}/src/PainterTestSheet.cpp
)
set(NYX_EDITOR_DEV_TOOL_HEADERS
    include/PainterTestWindow.h
    include/MockupOverlay.h
    ${NYX_DEV_TOOLS_DIR}/include/PainterTestSheet.h
)
add_compile_definitions("NYX_DEV_TOOLS=$<AND:$<BOOL:${NYX_DEV_TOOLS}>,$<CONFIG:Debug>>")

# The editor and its test targets keep debug information in the object files (/Z7), not in a
# compiler PDB (/Zi). They share the editor library's precompiled header, and with /Zi each of
# them compiles into its own copy of the library's compiler PDB. Parallel builds then write these
# same-named copies at once, which corrupts them (linker warnings LNK4020 and LNK4204). With /Z7
# there is no compiler PDB to copy: the header's types are in cmake_pch.obj.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

set(NYX_EDITOR_REFLECTION_SOURCES "${CMAKE_BINARY_DIR}/Generated/Runtime/Runtime.reflect.sources.cpp")

add_library(NyxEditorLib STATIC
	${NYX_EDITOR_SOURCES}
	${NYX_EDITOR_HEADERS}
	${NYX_EDITOR_DEV_TOOL_SOURCES}
	${NYX_EDITOR_DEV_TOOL_HEADERS}
	"${NYX_EDITOR_REFLECTION_SOURCES}"
)

configure_file(src/EditorBuildConfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/EditorBuildConfig.h @ONLY)

target_include_directories(NyxEditorLib
    PUBLIC
        include
        src
        ${CMAKE_CURRENT_BINARY_DIR}
        ${NYX_DEV_TOOLS_DIR}/include
)

target_link_libraries(NyxEditorLib
	PUBLIC NyxEditorEngine ole32 oleaut32 shell32 comsuppw
)

# The editor includes component headers, which include their generated reflection code.
add_dependencies(NyxEditorLib NyxGeneratedReflection)

target_precompile_headers(NyxEditorLib
    PRIVATE src/EditorPCH.h
)

# Tell Visual Studio to mimic the actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_EDITOR_SOURCES}
	${NYX_EDITOR_HEADERS}
	src/PainterTestWindow.cpp
	src/MockupOverlay.cpp
	include/PainterTestWindow.h
	include/MockupOverlay.h
)
source_group(DevTools FILES
	${NYX_DEV_TOOLS_DIR}/src/PainterTestSheet.cpp
	${NYX_DEV_TOOLS_DIR}/include/PainterTestSheet.h
)

add_executable(NyxEditor main.cpp)
target_link_libraries(NyxEditor PRIVATE NyxEditorLib)
target_precompile_headers(NyxEditor REUSE_FROM NyxEditorLib)

# Play starts NyxGame from next to the editor, so building the editor builds the game too.
add_dependencies(NyxEditor NyxGame)

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
    # TestSupport.h: Require() and the test directory, shared with the engine's tests
    set(NYX_TEST_SUPPORT_DIR ${PROJECT_SOURCE_DIR}/Engine/Source/tests)

    # Exercise the editor's real Open/New paths without starting a window or GPU device.
    add_executable(NyxEditorSceneTests tests/SceneReplacementTests.cpp)
    target_include_directories(NyxEditorSceneTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorSceneTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorSceneTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorSceneTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorSceneTests COMMAND NyxEditorSceneTests)

    # The pieces EditorLayer is built from: SceneEditor's commands and hotkeys, and the panels drawn headless
    add_executable(NyxEditorPanelTests tests/EditorPanelTests.cpp)
    target_include_directories(NyxEditorPanelTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorPanelTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorPanelTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorPanelTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorPanelTests COMMAND NyxEditorPanelTests)

    # The material document and the Parameters window, headless, over a stand-in for the renderer
    add_executable(NyxMaterialEditorTests tests/MaterialEditorTests.cpp)
    target_include_directories(NyxMaterialEditorTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxMaterialEditorTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxMaterialEditorTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxMaterialEditorTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxMaterialEditorTests COMMAND NyxMaterialEditorTests)

    add_executable(NyxUISourceTests tests/UISourceInspectorTests.cpp)
    target_include_directories(NyxUISourceTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxUISourceTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxUISourceTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxUISourceTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxUISourceTests COMMAND NyxUISourceTests)

    # Edits, undo and redo turned into live edit messages, and applied to a second world as the game does
    add_executable(NyxGameLinkSubscriberTests tests/GameLinkSubscriberTests.cpp)
    target_include_directories(NyxGameLinkSubscriberTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxGameLinkSubscriberTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxGameLinkSubscriberTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxGameLinkSubscriberTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxGameLinkSubscriberTests COMMAND NyxGameLinkSubscriberTests)

    # Several games at once, as Play starts them: this program starts copies of itself as stand-in games
    add_executable(NyxGameInstanceTests tests/GameInstanceTests.cpp)
    target_include_directories(NyxGameInstanceTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxGameInstanceTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxGameInstanceTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxGameInstanceTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxGameInstanceTests COMMAND NyxGameInstanceTests)

    # The fly camera of the scene views, with virtual keyboard and mouse and stand-in views
    add_executable(NyxEditorCameraTests tests/EditorCameraTests.cpp)
    target_include_directories(NyxEditorCameraTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorCameraTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorCameraTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorCameraTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorCameraTests COMMAND NyxEditorCameraTests)

    # Play setups and Play All: saving setups, tiling the windows, choosing the games
    add_executable(NyxPlayWallTests tests/PlayWallTests.cpp)
    target_include_directories(NyxPlayWallTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxPlayWallTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxPlayWallTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxPlayWallTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxPlayWallTests COMMAND NyxPlayWallTests)
endif()