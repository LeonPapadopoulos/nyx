# Nyx

A hobby game engine and editor written in C++20, rendering with Vulkan.

Having worked on and with a variety of proprietary and off-the-shelf engines so far, I've started to feel the need to start implementing my own and from scratch - not just as an additional learning opportunity, but also to create my personal collection of 'best-of's of what I've worked with thus far. While being actively developed in my spare time, this project is still very much in its infancy at this point in time, with many of its key structures likely to change.

![The Nyx editor: two scene viewports, scene outliner and details panel](Docs/Images/Editor.png)

## Features

- Editor with scene outliner, details panel, asset browser and two viewports
- Picking, transform gizmo, undo/redo
- Vulkan renderer
- Reflection system (see below)
- Animated startup banner

## Getting started

### Requirements

- Windows 10 or 11
- Visual Studio 2022 with the *Desktop development with C++* workload
- CMake 3.23 or newer
- [Vulkan SDK](https://vulkan.lunarg.com/) (provides the Vulkan headers and `glslc`)

### Clone

The dependencies are git submodules, so clone recursively:

```powershell
git clone --recursive https://github.com/LeonPapadopoulos/nyx.git
```

For an existing clone: `git submodule update --init --recursive`.

### Build and run

| Script | What it does |
| --- | --- |
| `Scripts\BuildEditor.bat` | Generates the project files and builds the editor (Debug) |
| `Scripts\BuildGame.bat` | Builds the game (Debug) and runs a scene: the file passed as argument, or `Assets\Scenes\Default.nyxscene` |
| `Scripts\GenerateProjectFiles.bat` | Generates the Visual Studio solution in `Build\Windows` |
| `Scripts\RebuildProjectFiles.bat` | Deletes `Build\Windows` and generates it again |
| `Scripts\BuildStartupPreview.bat` | Builds and opens the startup banner preview tool |
| `Scripts\FormatCode.bat` | Formats the code with clang-format (`check` only lists unformatted files) |

The editor ends up in `Build\Windows\Binaries\Debug\NyxEditor.exe`, the game next to it in `NyxGame.exe`. To work in Visual Studio, open the generated solution in `Build\Windows`; `NyxEditor` is the startup project.

In the editor, **Play** in the toolbar runs the open scene in `NyxGame.exe`, as a separate program and with unsaved changes included. While the game runs, the button is **Stop**, which asks the game to quit and ends it if it hasn't after 3 seconds; clicking it again while the game is stopping ends it at once. To debug the game, either right-click Play and turn on *Game Waits for Debugger*, then attach Visual Studio to `NyxGame.exe`, or install the Visual Studio extension *Microsoft Child Process Debugging Power Tool 2022+* and turn on child process debugging under *Debug > Other Debug Targets > Child Process Debugging Settings*; Visual Studio then attaches to the game by itself when the editor starts it.

The game connects back to the editor that started it, over the *editor link*: a TCP connection on `127.0.0.1` with a port the system picks, so only programs on this machine can connect. Both programs log when the link connects and when it ends, and hovering **Stop** shows its state. `NyxEditorLinkTests` tests the link with both ends in one program.

**Play All**, next to Play, starts several games at once, side by side on the monitor the editor is on: one per *play setup* marked for it. A play setup is a named picture size, e.g. *Steam Deck* at 1280x800; *Window > Play Setups* edits them and previews where Play All puts the windows, and Play's right-click menu marks which ones Play All starts. Play itself uses the first setup. If the windows don't fit, all of them shrink by the same factor, and their titles say so. Each game has its own editor link and gets every edit; **Stop** asks all of them to quit, and a game that crashes or is closed doesn't affect the others. The games of Play All have no console windows unless *Console Windows in Play All* is on; their log reaches the Game Link window either way. Play setups are saved with the editor preferences in `%LOCALAPPDATA%\Nyx\Editor.ini`. `NyxPlayWallTests` tests the setups and the tiling; `NyxGameInstanceTests` tests several games with real processes, by starting copies of itself as stand-in games.

While the game runs, edits in the editor show up in it live: moving an entity, changing a property in the details panel, adding or deleting an entity, and undo and redo of those. Values being dragged, with the gizmo or in the details panel, and text being typed show up in the game every frame while they change; the end of the drag records one undo step. Edits made while the game is still starting are sent once it is linked. Opening or creating a scene stops the game; edits made after Stop aren't sent. `NyxLiveEditTests` and `NyxGameLinkSubscriberTests` test the live edits.

The editor's *Game Link* window (*Window > Game Link*) shows the games' log lines in the *Game Log* tab, and every message of their links, in both directions, in the *Messages* tab, each with the name of its game: select a message to see all its fields, or pause, filter by type and copy. *Record* there writes each game's messages of each play session to a `.nyxlinklog` file in a `LinkLogs` folder next to the editor; `NyxGame.exe --link-log <file>` records the game's side.

`NyxGame.exe` also runs on its own: `NyxGame.exe [scene file] [--window-size <width>x<height>] [--window-pos <x>,<y>] [--window-title <text>]`. The size is that of the picture, without titlebar and borders; the position is where the window's outer top-left corner goes, and may be negative on a monitor left of or above the main one. `NyxGameLaunchOptionsTests` tests the game's command line.

When a game crashes, it sends its call stack to the editor before it ends, and the editor's *Game Crashed* window shows a card for it: what happened, the call stack with files and lines, the game's last log lines, and **Restart** (the same game again, with the scene as it is now), **Copy** and **Dismiss**. A game that crashes before it is linked still gets a card, with its exit code. In Debug builds, Ctrl+Alt+Shift+C in a game window crashes it on purpose, to try this. `NyxGameInstanceTests` crashes a stand-in game for real and checks the report.

The editor's UI is moving to the *painter*, which draws 2D shapes as signed distance fields on the GPU, so edges stay crisp at any size. The GPU also sorts the shapes into the 8x8 tiles each pixel checks, leaving out what an opaque shape hides (plan and notes: `Design/ui-painter.md`). *Debug > Painter Test* shows its test sheet: every shape, line, shadow, glow, text size and image it can draw, as well as image glows, whose light takes its colours from an image's edge, and frosted glass. The mouse wheel zooms around the cursor, dragging pans, *Tile heatmap* colours each 8x8 tile by how many shapes the GPU checks there, and *Compare with ImDrawList* draws the same frame with ImGui's own triangles beside it. Its *In a window* tab paints the sheet straight into a scrolling ImGui window instead, with an ImGui button over each row and a painted badge over each button: painted shapes and ImGui's widgets cover each other in the order the code draws them. The *Widgets* tab is the UI gallery: the painted widgets (`Nyx::UI::Button`, also in the green and red of Play and Stop, `IconButton`, `MenuBarButton`, `WindowControlButton`, `BarTab`, `DocumentTab`, `StatusItem`, `Toggle`, `SliderFloat`, `SectionHeader`, `TreeRow`, `CollapsingHeader`, property rows, fields of one line and of several, `Checkbox`, `MenuItem`, `Selectable`, `Combo`, `ColorSwatch`, `AssetCard` and tables) in every state, beside ImGui's own, at 100% and 150%. The editor's frame, panels, menus and popups use them; *Save PNG* saves it to `Saved/Gallery`, to compare a widget before and after a change. The *Blur* tab shows a scene view beside its blurred copy, which the renderer makes small for every scene view each frame (for glows and glass), with the time the blurs take on the GPU. The *Statistics* tab shows what painting the whole editor costs: the editor's frame rate and the painter's alone, the whole frame step by step on the CPU (the waits, the scene views, the UI and the painter's part, presenting) and on the GPU (the frame, the scene views with their picking and selection masks, the blurs, the painter's binning and draws, the UI pass), for the last frame, on average and at their peak, the draws, commands and tiles per OS window beside what ImGui still draws there, the tile entries before and after leaving out what opaque shapes hide, with what opaque docked cards would bring, and a painted graph of the last frames; while vsync or a frame rate cap holds the editor's frame rate back, a blue info line says so. *Debug > Mockup Overlay* lays a mockup image from disk half transparent over the editor, with its opacity, offset and scale, to match a design. The editor's windows are painted as rounded cards with soft shadows on a dark backdrop, with a gap between docked cards; hovering the gap shows the splitter. The window's frame follows the reference mockup: a titlebar with the logo, the open scene's tab and the workspaces, and painted window controls; a toolbar with the menus (flat buttons that show a pill while their menu is open), the command search, Play, Play All, Pause, Step and Build; and a status bar with the last scene saved or opened, the errors the editor has logged, the graphics API and the frame rate. What Nyx doesn't do yet (projects, workspaces other than Scene, the Edit, Scene, Tools and Help menus, the command search, Pause, Step and Build) is there but disabled, with a tooltip that says so. Empty parts of the titlebar and the toolbar drag the window. Tab bars show a soft pill and an accent underline under the selected tab, which slides to the next one selected. Scrollbars are thin pills that widen while hovered. A scene view draws its picking pass only for a click, and its selection masks, which the outline is drawn from, only while something is selected and what they show changed. With *ambilight*, each scene view's picture glows onto the backdrop around it and faintly through the panels beside it, following the picture as it changes; *Window > Preferences* turns it off or sets its strength and reach, saved in `Editor.ini`. Its *Frame rate* section holds the editor's frame rate back, so it doesn't keep the GPU busy drawing frames the monitor never shows: *Vsync* (on by default) draws no more frames than the monitor refreshes, and *Frame rate cap* holds it to 30, 60, 90, 120, 144 or 240 fps, with or without vsync. Floating windows, menus and tooltips in the main window are frosted glass: they show what lies behind them, blurred, with a soft rim of light along their edge. Dragged out of the editor into OS windows of their own, they keep the glass's tint, rim and rounded corners over a plain backdrop: the desktop behind them can't show through. *Debug > Painted Chrome* switches back to ImGui's own look. Painting that follows other painting with nothing ImGui draws in between is drawn in one go; the *In a window* tab's statistics show how many draws each OS window's painting takes. *Debug > Mark ImGui Shapes* outlines in red every shape ImGui still draws itself, to find what isn't painted yet; in the editor's own panels and titlebar, nothing is left but ImGui's close, collapse and window menu buttons in title and tab bars. `NyxEditorPanelTests` checks that the panels leave no shapes to ImGui. *Debug > Painter Heatmap* colours each 8x8 tile of every OS window by the painter's work there: the commands its pixels walk, summed over all the draws that cover it, or how many draws cover it, with the tile's counts beside the mouse; the Statistics tab switches it too. `NyxUIChromeTests` checks the cards, splitters, tab bars and scrollbars in a dock space without a GPU. `NyxPainterTests` tests recording, measuring the shapes for the tiles, the segments painting is cut into inside ImGui windows and the runs they are drawn in, and the marks of ImGui's shapes; `NyxPainterGpuTests` sorts into tiles and paints on the GPU and checks the tiles and the pixels, also of segments drawn in between other drawing and as one run, and saves what it painted as PNG files when the environment variable `NYX_PAINTER_TEST_OUTPUT` names a folder.

To see what a scene file or a link recording contains, build `NyxDump` and print the file with it, e.g. from the repository folder: `Build\Windows\Binaries\Debug\NyxDump.exe Assets\Scenes\Default.nyxscene`, or a `.nyxlinklog` file the same way.

## Project layout

```
Assets/          Scenes, meshes, materials, textures and startup artwork
Dependencies/    Git submodules: spdlog, GLFW, GLM, cgltf
Docs/            Images used by this README
Editor/          NyxEditor.exe: editor panels, gizmo, transactions and asset database
Engine/
  Shaders/       GLSL shaders, compiled to SPIR-V during the build
  Source/
    Runtime/     Core (logging, assertions, paths), Engine (entities,
                 components, serialization), Input (keyboard and mouse),
                 Renderer (Vulkan)
    Reflection/  Reflection types and the NYX_REFLECT / NYX_PROPERTY macros
Game/            NyxGame.exe: runs a scene through its camera, without the editor
Scripts/         Build and formatting scripts
Startup/         The startup banner and its intro animations
ThirdParty/      Vendored libraries: Dear ImGui, stb_image
Tools/
  NyxDump/       Prints scene files and link recordings as readable text
  NyxHeaderTool/ Reflection code generator and its tests
```

`NyxEngine` is a static library without any editor code. `NyxEditor` links it and adds the editor as a layer of the application (`EditorLayer`), which holds the scene being edited with its undo history (`SceneEditor`), one class per panel, and Play (`PlaySession`); `NyxGame` links it and adds a layer that runs one scene.

## Reflection

Example of how to expose something to the reflection system and thus others like the editor inspector:

```cpp
NYX_REFLECT(Component, meta = (DisplayName = "Transform Component"))
struct TransformComponent
{
    NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Transform", DragSpeed = 0.1))
    glm::vec3 Position{ 0.0f };
    // ...
};

#include "Generated/Runtime/TransformComponent.reflect.h"
```

During the build, `NyxHeaderTool` reads every header in `Runtime/Engine/include` and `Runtime/Renderer/include` and writes the generated files to `Build\Windows\Generated`.

The tool's tests compare its output against golden files: `Tools\NyxHeaderTool\tests\RunHeaderToolTests.bat`.

## Third-party libraries

| Library | Used for |
| --- | --- |
| [GLFW](https://www.glfw.org/) | Window and input |
| [Vulkan](https://www.vulkan.org/) | Rendering |
| [Dear ImGui](https://github.com/ocornut/imgui) (docking branch) | Editor UI |
| [GLM](https://github.com/g-truc/glm) | Maths |
| [spdlog](https://github.com/gabime/spdlog) | Logging |
| [stb_image](https://github.com/nothings/stb) | Image loading |
