# Game modules

How a game made with Nyx brings its own component types, with reflection, saving, undo and
editing working for them as for the engine's, while Nyx never knows the game. The code is in
`Engine/Source/GameModules.cmake`, `Engine/Source/Runtime/App/include/GameModule.h`, the header
tool's `--module-name`, `nyx_add_game_programs()` in the top `CMakeLists.txt`, and `Paths` for
where the game's files are.

## A game lives outside Nyx

- **A game is a repository of its own** that brings in Nyx as a git submodule. Its top
  `CMakeLists.txt` adds Nyx, declares its module and asks Nyx for the programs:

  ```cmake
  cmake_minimum_required(VERSION 3.25)
  project(MyGame)

  add_subdirectory(Nyx)

  nyx_add_game_module(MyGame
      REFLECT_DIRS Source/Components
      SOURCES ${MY_GAME_SOURCES})

  nyx_add_game_programs(MODULE MyGame) # NyxEditor.exe and NyxGame.exe, linked with MyGame
  ```

  CMake 3.25 or newer, so that the policy the editor's debug information needs (CMP0141) is on
  from the game's own `project()`.
- **Nyx never refers to a game.** No path, no option default, no name. Built on its own, Nyx
  calls `nyx_add_game_programs()` without a module (only when it is the top project).
- **Why this way round:** the game pins the Nyx version it was made with, and can stay private
  while Nyx is public. An engine change the game needs is made in Nyx first and reaches the game
  by moving the submodule.
- Nyx's CMake files name their own folders by `PROJECT_SOURCE_DIR`, never `CMAKE_SOURCE_DIR`,
  which is the game's when Nyx is a subdirectory.

## The shape of it

- **`nyx_add_game_module(<name> REFLECT_DIRS ... [SOURCES ...] [INCLUDE_DIRS ...] [REGISTER <f>] [SYSTEMS <f>] [PLAYER <f>])`**:
  - runs the header tool over `REFLECT_DIRS` with `--module-name <name>`, into
    `Generated/<name>`; a component header ends with
    `#include "Generated/<name>/<Header>.reflect.h"`. The engine's reflected headers go in as
    `--type-root`, so a component can hold one of the engine's structs, such as a
    `Nyx::SurfaceAnchor`;
  - makes `<name>`, an object library of `SOURCES` and the generated registration, compiled once
    against the engine's headers. It links no engine library, so the game and the editor, which
    use different ones (`NyxEngine`, `NyxEditorEngine`), each link the module's objects with their
    own;
  - writes each program's `GetLinkedGameModule()` into `<name>GameGlue` and `<name>EditorGlue`.
    The editor's also registers where the module's types are declared, generated code that
    includes the editor's headers, so it is compiled only for the editor.
- **`nyx_link_game_module(<target> [MODULE <name>] [EDITOR])`** links a module's objects and the
  program's `GetLinkedGameModule()` into a program, or without `MODULE` the empty
  `NyxNoGameModule`. Tests use it as the programs do.
- **`nyx_add_game_programs([MODULE <name>])`** makes `NyxGame` and `NyxEditor`
  (`nyx_add_game_program()`, `nyx_add_editor_program()`) in `Binaries/<config>` of the build folder,
  next to the shaders, the shader compiler and the Slang DLLs, as in Nyx's own build. The editor
  shares its library's precompiled header, so the function gives it that folder's settings
  (`NYX_DEV_TOOLS`, embedded debug information) wherever it is called from.
- **A module's own code at startup**, named by qualified function names it defines:
  - `REGISTER MyGame::RegisterTypes`, `void ()`: runs right after the module's generated type
    registration, in the game and the editor, e.g. to set post-load steps
    (`ComponentTypeRegistry::SetPostLoad()`);
  - `SYSTEMS MyGame::CreateSystems`, `void (Nyx::GameWorld&)`: adds the module's simulation systems
    to the game's world, in their order. Only the game calls it; the editor doesn't simulate.
  - `PLAYER MyGame::CreatePlayer`,
    `std::unique_ptr<Nyx::IPlayerController> (Nyx::Input::InputSystem&)`: makes the player's
    controller, which adds its actions and contexts to the input system and, each frame, turns
    them into commands for the next step (`PlayerController.h`, `CommandQueue.h`). Only the game
    calls it.
- **The module says what it has** through `GameModule`: its name, `RegisterTypes` (the generated
  registration and `REGISTER`), `CreateSystems` and `CreatePlayerController` for the game, and
  `RegisterSources` for the editor. Each program passes `GetLinkedGameModule()` in
  `ApplicationSpecs::Module`. `Application` registers its types right after the engine's, before
  any layer; `EditorLayer::OnAttach()` registers its sources. The game's components then appear in
  Add Component and the details panel, and save, undo and go over the editor link like the
  engine's.
- **Explicit, not self-registering.** Nothing registers itself from a static initializer: the
  linker drops objects nothing refers to, and the order of static initializers across files is not
  defined.
- **Type names are unique.** `ComponentTypeRegistry` refuses a second type with a name already
  registered, with an error. The engine's types register first, so they always keep their names.

## Where the game's files are

- **Two roots.** The engine root is Nyx's folder: the engine's shaders, the editor's themes, the
  engine's own assets such as the standard material shader and the default scene. The project root
  is the game's folder: its `Assets` and `Saved`. In Nyx's own build both are Nyx's folder.
- `nyx_add_game_programs()` writes both into `NyxRoots.ini` next to the programs; the project root
  is the top project's folder unless `PROJECT_ROOT` names another. `Paths` reads the file on first
  use. A relative root is taken from the file's folder, so a build can be moved whole. Without the
  file, both roots are the first folder above the program with an `Assets` folder.
- **Assets are looked for in the project first, then in the engine** (`Paths::FindAsset()`,
  `GetAssetSearchDirs()`): a game's material can use the engine's standard shader, and a game
  overrides an engine asset by having one at the same path. Materials with their shaders and
  textures, textures, skyboxes, live shader reload and the material editor all look this way.
- **What the editor makes goes into the project**: new scenes, materials and shaders in the
  project's `Assets`. An existing material is saved where it was found.

## Rules

- A module includes the engine's public headers, never the editor's.
- The engine never includes a module. Its include folders don't contain any, so the existing
  folder check covers that.
- A module is compiled once and linked into both programs, so its code must not depend on
  `NYX_UI_INSPECTION`, which only `ImGuiSource.h` reads.
- A scene with a game's components opened in a build without that module loads with a warning per
  unknown type, and saving it drops them, as the scene format already does.

## Tests

- `NyxTestModule` (`Engine/Source/tests/TestGameModule`) is made and linked through the same
  functions a game uses, so the path a private game depends on stays tested in public:
  - `NyxGameModuleTests`: the game's linked module, its type registered next to the engine's, a
    scene that saves and loads its components, one holding the engine's `SurfaceAnchor`, its
    post-load step (`REGISTER`) and its systems (`SYSTEMS`) running in a `GameWorld`; its commands
    keeping only their Serialize properties; its player (`PLAYER`) turning a key press and the
    cursor's ray into commands that the next step applies, once;
  - `NyxEditorGameModuleTests`: the editor's linked module registers where its types and
    properties are declared.
- `NyxComponentTypeRegistryTests`: a second type of a name already taken is refused.

## Not built yet

- **Engine assets in the asset browser.** It lists the project's assets only.
- **A module's folder dependency table**, checked like the engine's and the editor's.
- **Reloading game code without restarting** (a DLL module). A static build keeps debugging
  simple.
- **Projects in the editor.** The toolbar's project menu stays disabled; a game's build is its
  project for now.
