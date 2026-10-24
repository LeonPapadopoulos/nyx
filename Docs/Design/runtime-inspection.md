# Runtime inspection

How the editor shows what a running game made of an entity while it runs, and why it works this
way. Built: run-time component types (`EComponentTypeFlags::Runtime`), `InspectRuntimeComponents()`
(`Scene`), the link messages `InspectEntity` and `InspectedEntity`, the game's answer
(`EditorLinkLayer`), the editor's asking (`GameInstance`, `PlaySession`) and the Details panel's
*Runtime* section.

## Run-time component types

- **A component a world makes while it runs and never saves**, such as a mesh made at run time, the
  instances of a scatter, or a game's state of a system's own that the player can't edit (a status, a
  count, what a system last computed). Saving it would be wrong: it is derived, made again after
  loading.
- **A type opts in when it is registered** (`ComponentTypeRegistry`):
  - a reflected type, after the header tool registered it: `SetRuntime<T>()`, e.g. in a game
    module's `RegisterTypes()`. Its properties with `Serialize` are what the editor is sent; the type
    is never saved, whatever its properties' flags;
  - a type that isn't reflected: `RegisterRuntime<T>(name, displayName, describe)`, which makes its
    metadata (a name, no properties). `RuntimeMeshComponent` and `MeshInstancesComponent` are
    registered so;
  - either can say what it is as a short list of names and values (`SetDescribe<T>()`, or the third
    argument), e.g. *Vertices 1200*, *Revision 3*: for what reflection doesn't show, such as a
    pointer's data.
- **A run-time type is left out** of scene files (`SceneSerializer::WriteEntity()`, so also of
  copies, pastes and the editor link's `CreateEntity`; one in data is skipped with a warning), undo's
  snapshots of an entity, the Details panel's editable components and Add Component.
- `InspectRuntimeComponents(world, entity)` gives an entity's run-time components: each one's type
  name, display name, its reflected properties as a property block, and its values. The same
  function serves the editor's own world and a game's answer, so both look the same.

## Over the link

- **Asked, not streamed.** The editor sends `InspectEntity { Request, Entity }` by guid; the game
  answers at once with `InspectedEntity { Request, Entity, bFound, Step, Components }`, the step its
  simulation is at. The game keeps no state for it: nothing to start, stop or forget when the editor
  goes, and nothing sent that nobody looks at.
- **Paced by the editor.** `GameInstance::SetInspectedEntity()` asks every `InspectInterval`
  (250 ms: four times a second), and only once the last answer came, so a game paused in the
  debugger is asked once, not a queue full. An answer about another entity than the one asked
  about now is dropped. `GetInspection()` is the last answer about it.
- **Cheap:** one entity, only while the Details panel shows its *Runtime* section open, only the
  first game of a Play All (`PlaySession::InspectEntity()`). An answer is a few hundred bytes for a
  few components; the game spends a guid lookup and writing them.
- **Versions:** two new message types, so `EditorLinkProtocolVersion` stays: a side that doesn't
  know them skips them with a warning. A component type the editor doesn't know (a game module it
  wasn't built with) still shows its name and values; its properties then can't be read.

## In the Details panel

- **A *Runtime* section** after the components, greyed, read-only, each run-time component a
  collapsible row with its reflected properties (read into a scratch component of its type and drawn
  with the usual widgets, disabled) and its values as property rows.
- **Where it comes from**, on its first line: the editor's own world when nothing is playing (and
  the section only shows when the entity has a run-time component); while Play runs, the first game:
  *Game 1, step 1234*, *waiting for Game 1*, or *Game 1 has no such entity* (e.g. one deleted in
  play, or made by the game, which the editor's world doesn't have).
- The panel says which entity it shows (`DetailsPanel::GetInspectedEntity()`, its guid while the
  section is open and drawn); `EditorLayer` hands that to `PlaySession::InspectEntity()` each frame,
  and the game's answer back (`SetGameInspection()`) before the panel is drawn. The `Scene` folder
  knows nothing of `Play`.

## Later

- Draw what an answer says in the scene views, such as shapes a game describes.
- Entities the game made at run time: they have guids of the game's own; picking them in the game's
  window could show them.
- Asking several games of a Play All at once.
