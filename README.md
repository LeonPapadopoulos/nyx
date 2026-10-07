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
| `Scripts\GenerateProjectFiles.bat` | Generates the Visual Studio solution in `Build\Windows` |
| `Scripts\RebuildProjectFiles.bat` | Deletes `Build\Windows` and generates it again |
| `Scripts\BuildStartupPreview.bat` | Builds and opens the startup banner preview tool |

The editor ends up in `Build\Windows\Binaries\Debug\NyxEditor.exe`. To work in Visual Studio, open the generated solution in `Build\Windows`; `NyxEditor` is the startup project.

## Project layout

```
Assets/          Scenes, meshes, materials, textures and startup artwork
Dependencies/    Git submodules: spdlog, GLFW, GLM, cgltf
Docs/            Images used by this README
Editor/          NyxEditor.exe, the application entry point
Engine/
  Shaders/       GLSL shaders, compiled to SPIR-V during the build
  Source/
    Runtime/     Core (logging, assertions, paths), Engine (entities,
                 components, serialization), Renderer (Vulkan)
    Editor/      Editor panels, gizmo, transactions and asset database
    Reflection/  Reflection types and the NYX_REFLECT / NYX_PROPERTY macros
Scripts/         Build helper scripts
Startup/         The startup banner and its intro animations
ThirdParty/      Vendored libraries: Dear ImGui, stb_image
Tools/
  NyxHeaderTool/ Reflection code generator and its tests
```

`NyxEngine` is built as a DLL and linked by `NyxEditor`; the build copies it next to the executable.

## Reflection

A component declares which properties are editable, undoable and saved:

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

During the build, `NyxHeaderTool` reads every header in `Runtime/Engine/include` and `Runtime/Renderer/include` and writes the generated files to `Build\Windows\Generated`. The details panel, undo/redo and scene files then work with the new component without further code.

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
