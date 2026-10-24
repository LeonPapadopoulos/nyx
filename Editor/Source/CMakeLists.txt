# The editor without its main(): a library that NyxEditor and the editor's test targets link, so
# the editor compiles once
set(NYX_EDITOR_SOURCES
    # App
    App/src/EditorLayer.cpp
    App/src/EditorAssetTypes.cpp
    App/src/Workspaces.cpp
    App/src/EditorFrame.cpp
    App/src/MaterialWorkspace.cpp

    # Scene
    Scene/src/SceneEditor.cpp
    Scene/src/SceneOutlinerPanel.cpp
    Scene/src/DetailsPanel.cpp
    Scene/src/SceneViewPanel.cpp
    Scene/src/SceneViewSet.cpp
    Scene/src/EditorCameraController.cpp
    Scene/src/SceneFilePopups.cpp
    Scene/src/DefaultScene.cpp
    Scene/src/ComponentPostLoadSubscriber.cpp
    Scene/src/ComponentEdits.cpp
    Scene/src/EntityCopies.cpp
    Scene/src/SceneDocument.cpp
    Scene/src/TransformGizmo.cpp
    Scene/src/SceneEntityTransactionDomain.cpp

    # Properties
    Properties/src/InspectorDrawContext.cpp
    Properties/src/ReflectedPropertyDrawer.cpp
    Properties/src/PropertyWidgetRegistry.cpp
    Properties/src/EntityReferenceWidget.cpp

    # Transactions
    Transactions/src/InspectorTargetId.cpp
    Transactions/src/EditorTransactionContext.cpp
    Transactions/src/TransactionSystem.cpp
    Transactions/src/TransactionDiffUtil.cpp
    Transactions/src/RootObjectSnapshotUtils.cpp

    # Assets
    Assets/src/AssetDatabase.cpp
    Assets/src/AssetBrowserPanel.cpp
    Assets/src/AssetTypeRegistry.cpp
    Assets/src/AssetDragDrop.cpp

    # Materials
    Materials/src/MaterialDocument.cpp
    Materials/src/MaterialParametersPanel.cpp
    Materials/src/MaterialPreviewPanel.cpp
    Materials/src/ShaderLogPanel.cpp
    Materials/src/NewMaterial.cpp

    # Play
    Play/src/PlaySession.cpp
    Play/src/PlayControls.cpp
    Play/src/CrashCardsPanel.cpp
    Play/src/GameInstance.cpp
    Play/src/PlayWall.cpp
    Play/src/GameLinkPanel.cpp
    Play/src/GameLinkSubscriber.cpp

    # ReflectedAssets
    ReflectedAssets/src/ReflectedAssetDocument.cpp
    ReflectedAssets/src/ReflectedAssetEditors.cpp
    ReflectedAssets/src/ReflectedAssetWindows.cpp

    # Input
    Input/src/InputAssetDocument.cpp
    Input/src/InputPanel.cpp
    Input/src/InputTuningView.cpp

    # Preferences
    Preferences/src/PreferencesWindow.cpp
    Preferences/src/ThemeEditorWindow.cpp
    Preferences/src/EditorPreferences.cpp
    Preferences/src/SourceNavigation.cpp

    # Common
    Common/src/FileDialogs.cpp
    Common/src/ReflectionSourceRegistry.cpp
    Common/src/UISourceInspector.cpp
    Common/src/ReflectedPropertyAccess.cpp
    Common/src/EntityDragDrop.cpp
)

set(NYX_EDITOR_HEADERS
    # App
    App/include/EditorLayer.h
    App/include/Workspaces.h
    App/include/EditorFrame.h
    App/include/MaterialWorkspace.h

    # Scene
    Scene/include/SceneEditor.h
    Scene/include/SceneOutlinerPanel.h
    Scene/include/DetailsPanel.h
    Scene/include/SceneViewPanel.h
    Scene/include/SceneViewSet.h
    Scene/include/EditorCameraController.h
    Scene/include/SceneFilePopups.h
    Scene/include/DefaultScene.h
    Scene/include/ComponentPostLoadSubscriber.h
    Scene/include/ComponentEdits.h
    Scene/include/EntityCopies.h
    Scene/include/SceneDocument.h
    Scene/include/TransformGizmo.h
    Scene/include/TransactionObjectRefHelpers.h
    Scene/include/SceneEntityTransactionDomain.h

    # Properties
    Properties/include/InspectorDrawContext.h
    Properties/include/ReflectedPropertyDrawer.h
    Properties/include/PropertyWidgetRegistry.h
    Properties/include/EntityReferenceWidget.h

    # Transactions
    Transactions/include/InspectorTargetId.h
    Transactions/include/EditorTransactionContext.h
    Transactions/include/TransactionTypes.h
    Transactions/include/TransactionSystem.h
    Transactions/include/TransactionDiffUtil.h
    Transactions/include/TransactionObjectRef.h
    Transactions/include/TransactionDomain.h
    Transactions/include/ReflectedObjectSnapshot.h
    Transactions/include/RootObjectSnapshotUtils.h

    # Assets
    Assets/include/AssetDragDrop.h
    Assets/include/AssetDatabase.h
    Assets/include/AssetBrowserPanel.h
    Assets/include/AssetTypeRegistry.h

    # Materials
    Materials/include/MaterialDocument.h
    Materials/include/MaterialParametersPanel.h
    Materials/include/MaterialPreviewPanel.h
    Materials/include/ShaderLogPanel.h
    Materials/include/NewMaterial.h

    # Play
    Play/include/PlaySession.h
    Play/include/PlayControls.h
    Play/include/CrashCardsPanel.h
    Play/include/GameInstance.h
    Play/include/PlayWall.h
    Play/include/GameLinkPanel.h
    Play/include/GameLinkSubscriber.h

    # ReflectedAssets
    ReflectedAssets/include/ReflectedAssetDocument.h
    ReflectedAssets/include/ReflectedAssetEditors.h
    ReflectedAssets/include/ReflectedAssetWindows.h

    # Input
    Input/include/InputAssetDocument.h
    Input/include/InputPanel.h
    Input/include/InputTuningView.h

    # Preferences
    Preferences/include/ThemeEditorWindow.h
    Preferences/include/PreferencesWindow.h
    Preferences/include/EditorPreferences.h
    Preferences/include/SourceNavigation.h

    # Common
    Common/src/EditorPCH.h
    Common/include/FileDialogs.h
    Common/include/ReflectionSourceRegistry.h
    Common/include/UISourceInspector.h
    Common/include/ReflectedPropertyAccess.h
    Common/include/EntityDragDrop.h
)

# The prototype and diagnostics tools (NYX_DEV_TOOLS), with the painter's test sheet that Painter
# Test shows. The library has them in every configuration, so the test targets can test them; an
# executable only takes the library's objects it uses, and NyxEditor uses them in Debug builds
# only, where NYX_DEV_TOOLS lets EditorLayer own them.
set(NYX_DEV_TOOLS_DIR ${PROJECT_SOURCE_DIR}/Engine/Source/DevTools)
set(NYX_EDITOR_DEV_TOOL_SOURCES
    DevTools/src/ImGuiDebugTools.cpp
    DevTools/src/PainterTestWindow.cpp
    DevTools/src/MockupOverlay.cpp
    ${NYX_DEV_TOOLS_DIR}/src/PainterTestSheet.cpp
)
set(NYX_EDITOR_DEV_TOOL_HEADERS
    DevTools/include/ImGuiDebugTools.h
    DevTools/include/PainterTestWindow.h
    DevTools/include/MockupOverlay.h
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

configure_file(Preferences/src/EditorBuildConfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/EditorBuildConfig.h @ONLY)

target_include_directories(NyxEditorLib
    PUBLIC
        App/include
        Scene/include
        Properties/include
        Transactions/include
        Assets/include
        Materials/include
        Play/include
        Input/include
        ReflectedAssets/include
        Preferences/include
        DevTools/include
        Common/include
        Common/src
        ${CMAKE_CURRENT_BINARY_DIR}
        ${NYX_DEV_TOOLS_DIR}/include
)

target_link_libraries(NyxEditorLib
	PUBLIC NyxEditorEngine NyxLinkTools ole32 oleaut32 shell32 comsuppw
)

# The editor includes component headers, which include their generated reflection code.
add_dependencies(NyxEditorLib NyxGeneratedReflection)

target_precompile_headers(NyxEditorLib
    PRIVATE Common/src/EditorPCH.h
)

# Tell Visual Studio to mimic the actual file hierarchy
source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES
	${NYX_EDITOR_SOURCES}
	${NYX_EDITOR_HEADERS}
	DevTools/src/ImGuiDebugTools.cpp
	DevTools/src/PainterTestWindow.cpp
	DevTools/src/MockupOverlay.cpp
	DevTools/include/PainterTestWindow.h
	DevTools/include/MockupOverlay.h
	DevTools/include/ImGuiDebugTools.h
)
source_group(DevTools FILES
	${NYX_DEV_TOOLS_DIR}/src/PainterTestSheet.cpp
	${NYX_DEV_TOOLS_DIR}/include/PainterTestSheet.h
)

# NyxEditor.exe, the editor with a game module or none. Made by nyx_add_game_programs() (the top
# CMakeLists.txt), so a game's repository can build it with its own module.
# nyx_add_editor_program([MODULE <name>])
function(nyx_add_editor_program)
    cmake_parse_arguments(PARSE_ARGV 0 ARG "" "MODULE" "")

    add_executable(NyxEditor ${CMAKE_CURRENT_FUNCTION_LIST_DIR}/main.cpp)
    target_link_libraries(NyxEditor PRIVATE NyxEditorLib)
    nyx_link_game_module(NyxEditor MODULE ${ARG_MODULE} EDITOR)

    # The editor shares the library's precompiled header, so it needs the settings this folder gives
    # its targets; the function may be called from any folder, where they don't apply.
    target_compile_definitions(NyxEditor PRIVATE "NYX_DEV_TOOLS=$<AND:$<BOOL:${NYX_DEV_TOOLS}>,$<CONFIG:Debug>>")
    set_target_properties(NyxEditor PROPERTIES
        MSVC_DEBUG_INFORMATION_FORMAT "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>"
    )
    target_precompile_headers(NyxEditor REUSE_FROM NyxEditorLib)

    # Play starts NyxGame from next to the editor, so building the editor builds the game too.
    add_dependencies(NyxEditor NyxGame)

    # Set working directory to be next to the built .exe. The editor's layout is kept in the
    # project's Saved/Layouts, not in the working directory.
    set_target_properties(NyxEditor PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:NyxEditor>"
    )

    # Compiled by Engine/Shaders; the renderer loads them from a "Shaders" folder next to the .exe
    nyx_copy_shaders_next_to(NyxEditor)
    # Live shader reload compiles in NyxShaderCompiler.exe, which sits next to the editor
    add_dependencies(NyxEditor NyxShaderCompiler)
endfunction()

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

    # The layout kept between runs: the workspaces' layout files, docking, the window's placement
    add_executable(NyxEditorLayoutTests tests/EditorLayoutTests.cpp)
    target_include_directories(NyxEditorLayoutTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorLayoutTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorLayoutTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorLayoutTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorLayoutTests COMMAND NyxEditorLayoutTests)

    # The scene views on a real GPU: any number opened and closed, each with its own camera, the
    # Debug menu's culling and shadow views, their section of the layout files. Needs a Vulkan
    # device; its window stays hidden.
    add_executable(NyxEditorSceneViewGpuTests tests/SceneViewGpuTests.cpp)
    target_include_directories(NyxEditorSceneViewGpuTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorSceneViewGpuTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorSceneViewGpuTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorSceneViewGpuTests PROPERTY FOLDER "Tests")
    nyx_copy_shaders_next_to(NyxEditorSceneViewGpuTests)
    nyx_copy_shader_compiler_next_to(NyxEditorSceneViewGpuTests)
    add_test(NAME NyxEditorSceneViewGpuTests COMMAND NyxEditorSceneViewGpuTests)

    # The input asset as the editor edits it: undo inside its arrays, live previews, saving, the
    # Tuning and Checks tabs drawn headless
    add_executable(NyxEditorInputTuningTests tests/InputTuningTests.cpp)
    target_include_directories(NyxEditorInputTuningTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorInputTuningTests PRIVATE NyxEditorLib)
    target_precompile_headers(NyxEditorInputTuningTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorInputTuningTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorInputTuningTests COMMAND NyxEditorInputTuningTests)

    # Assets edited by reflection, a physics material and the test module's creature: their lines, the
    # editor's undo with named entries added, renamed and taken away, problems at their values,
    # saving, files read again, what reaches the games, New, the windows drawn headless
    add_executable(NyxReflectedAssetEditorTests tests/ReflectedAssetEditorTests.cpp)
    target_include_directories(NyxReflectedAssetEditorTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxReflectedAssetEditorTests PRIVATE NyxEditorLib)
    nyx_link_game_module(NyxReflectedAssetEditorTests MODULE NyxTestModule EDITOR)
    target_precompile_headers(NyxReflectedAssetEditorTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxReflectedAssetEditorTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxReflectedAssetEditorTests COMMAND NyxReflectedAssetEditorTests)

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

    # The editor with a game module, linked as the editor links a game's: the module's source locations
    add_executable(NyxEditorGameModuleTests tests/EditorGameModuleTests.cpp)
    target_include_directories(NyxEditorGameModuleTests PRIVATE ${NYX_TEST_SUPPORT_DIR})
    target_link_libraries(NyxEditorGameModuleTests PRIVATE NyxEditorLib)
    nyx_link_game_module(NyxEditorGameModuleTests MODULE NyxTestModule EDITOR)
    target_precompile_headers(NyxEditorGameModuleTests REUSE_FROM NyxEditorLib)
    set_property(TARGET NyxEditorGameModuleTests PROPERTY FOLDER "Tests")
    add_test(NAME NyxEditorGameModuleTests COMMAND NyxEditorGameModuleTests)

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

    # Every #include between the editor's folders against their table in Docs/Architecture.md
    add_test(NAME NyxEditorFolderDependencies
        COMMAND ${CMAKE_COMMAND}
            "-DFOLDERS_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
            "-DTABLE_HEADING=## Editor folders"
            "-DARCHITECTURE_DOC=${PROJECT_SOURCE_DIR}/Docs/Architecture.md"
            -P "${PROJECT_SOURCE_DIR}/Engine/Source/tests/FolderDependencies.cmake")
endif()