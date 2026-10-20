# Game modules

**Proposal.** None of this is built yet. It is the plan for how a game made with Nyx brings its
own components and systems, with reflection, saving, undo and editing working for them as for the
engine's, while Nyx never knows the game.

## What stands in the way today

- The header tool runs over one folder, `Engine/Source/Runtime/Scene/include`, and names
  everything it writes after one module, Runtime: `Generated/Runtime/<Header>.reflect.h`,
  `Runtime.reflect.init.h` and `RegisterRuntimeReflectedTypes()`.
- `Application`'s constructor calls `RegisterComponentTypes()`, which registers only those types.
- `NyxGame.exe` links only the engine, and so does the editor. A component declared anywhere else
  has no reflection, so it can't be saved, edited or sent over the editor link.

## A game lives outside Nyx

- **A game is a repository of its own** that brings in Nyx as a git submodule. Its top-level
  `CMakeLists.txt` adds Nyx, declares its module and asks Nyx for the programs:

  ```cmake
  add_subdirectory(Nyx)

  nyx_add_game_module(MyGame
      SOURCES ${MY_GAME_SOURCES}
      REFLECT_DIRS Source/Components)

  nyx_add_game_programs(MODULE MyGame) # NyxEditor.exe and NyxGame.exe, linked with MyGame
  ```

- **Nyx never refers to a game.** No path, no option default, no name. Built on its own, Nyx
  builds the editor and the game without a module, exactly as today.
- **Why this way round:** the game pins the Nyx version it was made with, and can stay private
  while Nyx is public. An engine change the game needs is made in Nyx first and reaches the game
  by moving the submodule.

## The shape of it

- **A game module is a static library.** It holds the game's components, systems and data, and
  nothing that runs the program: `NyxGame.exe` and the editor stay the programs.
- **`nyx_add_game_module()`** makes the library, runs the header tool over the folders listed,
  links the engine's public headers, and checks the module's own folder dependency table.
- **`nyx_add_game_programs()`** builds the editor and the game into the game's build folder,
  linked with the module, with the engine's shaders, the shader compiler and the tools copied
  next to them as in Nyx's own build. Nyx's own build calls the same function without a module.
- **The header tool learns module names.** A new `--module-name` (Runtime when not given, so the
  engine's build doesn't change) names the output folder and the generated functions:
  `Generated/<Module>/<Header>.reflect.h`, `Register<Module>ReflectedTypes()`,
  `Register<Module>ReflectedSources()`. Property hashes and the format don't change. Its golden
  tests get a second module.
- **The module says what it has, through one function it exports:**

  ```cpp
  struct GameModule
  {
      const char* Name;
      void (*RegisterTypes)();                // its components, post-load steps included
      void (*RegisterSources)();              // editor only: where its types are declared
      void (*CreateSystems)(Nyx::GameWorld&); // its systems, in their order
  };

  const GameModule& GetGameModule();
  ```

  The programs call it explicitly. Nothing registers itself from a static initializer: the linker
  drops objects in a static library that nothing refers to, and the order of static initializers
  across files is not defined. A build without a module links an empty one.
- **`NyxGame.exe`** calls `RegisterTypes()` after `Application` is made and before the scene
  loads, and `GameLayer` calls `CreateSystems()` for its `GameWorld`.
- **The editor** calls `RegisterTypes()` and `RegisterSources()` in `EditorLayer::OnAttach()`. The
  game's components then appear in Add Component and the details panel, and save, undo and go over
  the editor link like the engine's. The editor makes no systems: it doesn't simulate.
- **Type names are unique across modules.** `ComponentTypeRegistry` refuses a second type of the
  same name, with an error that names both modules.

## Rules

- A module includes the engine's public headers, never the editor's.
- The engine never includes a module. Its include folders don't contain any, so the existing
  check covers that.
- The engine is built twice: `NyxEngine` with `NYX_UI_INSPECTION=0` and `NyxEditorEngine` with
  `=1`. A module is compiled once and linked into both programs, so its code must not depend on
  that setting. Today only `ImGuiSource.h` reads it; a test checks that no other public header
  does.
- Assets: the game's own assets live in its repository. The project root (`Paths`) becomes the
  game's folder, with Nyx's `Assets` searched after it, so engine assets such as the built-in
  meshes and shaders still resolve.
- A scene with a game's components opened in a build without that module loads with a warning per
  unknown type, and saving it drops them, as the scene format already does.

## Tests

- Nyx's own tests build a small test module, out of its own folder and through the same two CMake
  functions a game uses. It registers one reflected component, which saves, loads, shows in the
  details panel and reaches the game as a live edit; a second type of the same name is refused.
  This keeps the path a private game depends on tested in public.

## Not decided yet

- **Reloading game code without restarting** (a DLL module). Not needed now; a static library
  keeps debugging and builds simple.
- **Projects in the editor.** The toolbar's project menu stays disabled; a game's build is its
  project for now.
