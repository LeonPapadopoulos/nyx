# Undo by guid

Plan written 2026-10-11. Fixes the three undo bugs from the handoff's known bugs, before more ways to edit (values while dragging, duplicate and paste, adding and removing components) are built on top of undo.

## The bugs, and why they happen

1. **Delete A, add B, undo twice: A doesn't come back.**
   - Undo steps name entities by handle: `MakeSceneEntityRef()` puts `Entity::Value` (slot index and generation) into `ObjectRef::Id`.
   - Undoing the delete calls `Registry::RestoreEntity()` for that exact handle. B took the slot, so the generation no longer matches and the restore fails.
2. **Undoing an edit of a struct field (e.g. a mesh path) does nothing.**
   - The details panel records the edit as a `SetValueChange` whose `TypeMetadata` is the struct type (`AssetReference`) and whose `PropertyIndex` is the field's index.
   - `SceneEntityTransactionDomain::ResolveMutable()` looks that type up in the component registry. It isn't a component, so it returns null and the change is skipped.
3. **Undoing a delete brings a MeshRenderer back without its mesh and material.**
   - `RootObjectSnapshot` keeps `PropertyValue`s, which only hold numbers, vectors, quaternions and strings.
   - `MeshRendererComponent::Mesh` and `::Material` are `AssetReference` structs, so the snapshot leaves them out.

## Done when

- Each bug has a test in `NyxEditorSceneTests` or `NyxGameLinkSubscriberTests` that failed before and passes after.
- Undo and redo of add, delete, transform edits and struct field edits always end in the same state as before, also after handles were reused.
- The game started with Play ends up like the editor after every step. `NyxGameLinkSubscriberTests` already compares the two worlds.
- No change to scene files or link messages.

## Changes

### 1. Undo steps name entities by guid

- For `EObjectDomain::SceneEntity`, `ObjectRef::Id` holds the entity's guid instead of its handle. `MakeSceneEntityRef()` reads the guid from the entity's `GuidComponent`.
- `SceneEntityTransactionDomain` finds the handle for a guid when a change is applied, through a guid → handle lookup in `SceneDocument`. The lookup is kept up to date on create, destroy, Open and New.
- **Undoing a delete** creates a new entity in any free slot and gives it the snapshot's guid, instead of `RestoreEntity()` with the old handle.
- **The selection** stays a handle. After undo or redo it is looked up again by guid, so it follows the entity into its new slot.
- `GameLinkSubscriber` reads the guid straight from the `ObjectRef` and no longer needs to look for it in snapshots (`FindGuid`).
- **Open question:** an entity without a `GuidComponent` can't be undone. Either every entity the editor creates gets a guid (check `SceneDocument::CreateEntity()`), or recording such an edit gives it one.
- **Test:** delete A, add B, undo, undo. A is back with its guid and components, and B is gone.

### 2. Snapshots keep whole components

- `RootObjectSnapshot` stores each component as a property block: the bytes `ReflectedArchiveSerializer` writes for scene files, struct properties included. It no longer stores a list of `PropertyValue`s.
- Restoring reads the block back with `DeserializeObject`, then resolves assets the same way loading a scene does (`ScenePostLoadContext`). `EditorLayer`'s separate `ResolveAllMeshRendererAssets` after undo can then go.
- `TransactionDiffUtil` and `ReflectedDiffUtil`, which compare snapshots to find what changed, have to compare struct properties too. Comparing each property's bytes is enough.
- **Test:** delete a cube with a mesh and material, then undo. The mesh path, material path and resolved assets are back, and the game gets the same entity (`CreateEntity`).

### 3. Edits of struct fields name their component

- `SetValueChange` gets the component type and a property path: the property indices from the component down to the field, e.g. `MeshRenderer` → `Mesh` → `Path`. `TypeMetadata` and `PropertyIndex` stay for the field itself.
- `ResolveMutable()` resolves the component, then walks the path to the struct that holds the field.
- The details panel already knows the path when it draws nested structs; `InspectorDrawContext` passes it into the recorded change.
- `GameLinkSubscriber` gets the component from the change and no longer searches every component for the struct type (`ContainsStruct`). It still sends the whole struct property.
- **Test:** change a MeshRenderer's mesh path in the details panel's way, undo, redo. Each step changes the path, and the game follows.

## Left for later

- Making component add and remove undoable. It uses the same snapshots and refs, so it is easier after this.
- Duplicate and paste, which need new guids for the copies.
- Merging the old `SceneEntitySnapshot` (fixed Name, Transform and MeshRenderer fields) into `RootObjectSnapshot`, if nothing uses it anymore.

## As built

### 1. Undo steps name entities by guid

Built and tested on Windows 2026-10-11. Not tried by hand in the editor yet.

- **Reproduced first:** `TestUndoAfterSlotReuse` in `NyxGameLinkSubscriberTests` (delete A, add B, which takes A's slot, undo twice) failed on the old code with "A didn't come back after undoing its delete". It passes now. It also checks redo, an edit recorded on B that is undone and redone across the delete and add, and that the game's world matches the editor's after every step.
- **The open question:** `SceneDocument::CreateEntity()` always adds a `GuidComponent`, and loading gives every entity one, so every entity the editor edits has a guid. `MakeSceneEntityRef()` returns an invalid ref for an entity without one, and nothing is recorded for it.
- **Refs:** `MakeSceneEntityRef(scene, entity)` stores the guid in `ObjectRef::Id`. `MakeSceneEntityRef(guid)` and `GetSceneEntityGuid(ref)` are the other directions. Callers: the outliner's Add and Delete, the details panel, the gizmo (three places), and the tests.
- **Lookup, different from the plan:** `SceneDocument::FindEntity(guid)` keeps a cache of where it last found each guid. It is not a map kept up to date on create, destroy, Open and New, because the registry is changed in too many places (`GetRegistry()`, Open, New, the undo domain). An entry is only used after checking that its entity is alive and still has that guid; otherwise the registry is searched again. `SceneDocument::GetGuid(entity)` is the reverse.
- **Undoing a delete / redoing an add:** `SceneEntityTransactionDomain::CreateRootObject()` makes a new entity in any free slot with the ref's guid, then the snapshot restores the rest. If an entity with that guid is already alive, it refuses, so no guid is ever on two entities. `Registry::RestoreEntity()` is no longer used by the editor.
- **Deleting through undo** goes through `SceneDocument::DestroyEntity()`, which also clears the selection if it was that entity.
- **Selection:** `EditorLayer::HandleUndoRedoHotkeys()` remembers the selected entity's guid and finds it again after undo or redo, so it stays selected when it comes back in another slot. Not covered by a test, since it needs ImGui's key handling.
- **GameLinkSubscriber** keys each transaction's edits on the guid in the ref and finds the entity's current handle with `FindEntity()`. `FindGuid()`, which read the guid out of snapshots, is gone, and so is the "entity has no guid" warning, which can no longer happen.
- **Not touched:** the older undo path that names objects by `InspectorTargetId` (`ReflectedTransactionSystem`, `DiffUtil.h`, `EditableObjectRegistrations.h`) still decodes handles. Nothing creates it; it is dead code, like the files listed in the handoff.
- Tests: `NyxGameLinkSubscriberTests` (new test above), `NyxEditorSceneTests`, `NyxUISourceTests`, `NyxGameInstanceTests` and `NyxPlayWallTests` pass.
- **To try by hand:** add an entity and move it, select another entity and delete it, add a new one, then undo three times and redo three times. Each step should do what it says, the deleted entity should come back with its transform, and with a game running, the game should match.

### 2. Snapshots keep whole components

Built and tested on Windows 2026-10-11, after a hand test of change 1 found both problems below. Not tried by hand yet.

- **Reported:** after undo and redo, the cube came back without mesh and material. Typing the paths in again showed the mesh in the running games, but not in the editor.
- **Reproduced first:** `TestUndoKeepsWholeComponents` (delete a cube with mesh, material and transform, undo, redo, undo) failed with "Undo the delete: the mesh was lost". `TestEditsLoadAssets` checks that a typed mesh path loads the mesh in the editor. Both pass now.
- **Snapshots:** `RootObjectSnapshot` holds `SubobjectSnapshot`s, each a component's type and its Serialize properties as one property block written by `ReflectedArchiveSerializer::SerializeObject`, as scene files store components. Restoring reads it back with `DeserializeObject`. Struct properties (`AssetReference`) are now kept. `ReflectedObjectSnapshot` is unchanged, since `TransactionDiffUtil` still uses it to find which properties an edit changed.
- **Loading assets, the second problem:** the editor resolved assets only at startup and after the undo and redo hotkeys, never after an edit in the details panel. The game runs each component's `PostLoad` after every live edit. The new `ComponentPostLoadSubscriber` does the same in the editor: after every commit, undo and redo, it runs `PostLoad` for every component of each entity the transaction touched. `EditorLayer`'s own resolving after undo and redo (`ResolveAllMeshRendererAssets`) is gone. Any edit of an entity now resolves its assets again, which is a path lookup.
- **Not done, unlike the plan:** comparing struct properties in `TransactionDiffUtil` / `ReflectedDiffUtil`. They find changed properties for `SetValueChange`s, not for snapshots, so that belongs to change 3.
- **Still broken until change 3:** undo and redo of a typed mesh or material path, since that is a struct field edit.
- Tests: `NyxGameLinkSubscriberTests` (two new tests, and the test harness now has a `ComponentPostLoadSubscriber` with a resolver that records requests), `NyxEditorSceneTests`, `NyxUISourceTests`, `NyxGameInstanceTests` and `NyxPlayWallTests` pass.
- **To try by hand:** delete a cube, undo: it is back and visible. Redo, undo again: still visible. Type `Meshes/Cube.nyxmesh` into an entity's empty mesh path: the editor shows the cube.

### 3. Edits of struct fields name their component

Built and tested on Windows 2026-10-11. Not tried by hand yet.

- **Reproduced first:** `TestEditsUndoAndRedo` now requires that undo and redo of a mesh path edit change the path back and forth. Recorded the way the details panel recorded it, undo failed with "Undo didn't change the mesh path back". It passes now, and the game follows each step.
- **As planned:** `SetValueChange` gets a `Location` (`SubobjectPath`: the component type, and the struct property indices from it down to the struct). `TransactionSystem::ApplySetValueChange()` resolves the component, then walks the path (`ResolveLocation()`). If the path doesn't fit the types anymore, or doesn't end at the change's type, the change is skipped instead of writing into the wrong object.
- **Recording:** `InspectorDrawContext::CurrentLocation` is the location of the object being drawn. `EditorLayer` clears it for each component. `DrawStructPropertyRow()` (it now gets the owner type) sets the component type on the first struct level, adds the struct property's index, and restores the outer location after drawing the struct's fields. `PropertyWidgetRegistry` passes the location to `TransactionDiffUtil::TakeSnapshot()`, which copies it into every `SetValueChange`.
- **GameLinkSubscriber:** `FindChangedComponentProperty()` takes the component and its property straight from the location. `ContainsStruct()` and `GetChangedComponentProperties()`, which searched every component of the entity for the struct type and sent each property that might hold it, are gone. A struct edit now sends exactly the one component property that holds the struct.
- **Assets:** `TestEditsLoadAssets` also undoes a typed mesh path: the path is empty again, and the loaded mesh is gone (`ComponentPostLoadSubscriber` from change 2).
- **Details panel test:** `TestDetailsPanelLocations` in `NyxEditorSceneTests` draws a MeshRenderer headless with ImGui, with a stand-in string widget that records the location it is drawn with. Mesh.Path and Material.Path must each come with the location of their struct, and the location must be empty again afterwards. With the drawer's location tracking switched off, it fails.
- **Not done, as in the plan's left for later:** `ReflectedObjectSnapshot` (used by `TransactionDiffUtil`) still compares plain values, which is enough, since a struct edit snapshots the struct itself.
- Tests: all test programs pass (`NyxGameLinkSubscriberTests`, `NyxEditorSceneTests`, `NyxUISourceTests`, `NyxGameInstanceTests`, `NyxPlayWallTests`, `NyxGameLaunchOptionsTests`, `NyxEditorLinkTests`, `NyxLiveEditTests`, `NyxArchiveTests`).
- **To try by hand:** in the details panel, change a cube's mesh path to something else and back, and its material path; undo and redo each, with a game running. The editor and the game should change together, and the cube should disappear and reappear as the path becomes invalid and valid again.

## Follow-up: Add Component and Remove Component

Built and tested on Windows 2026-10-11. Tried by hand: works.

- **Why:** the editor had no way to add or remove components, so a new entity stayed a name and a guid. It is also the last kind of edit that wasn't undoable or sent to running games.
- **UI:** an *Add Component* button under the details panel lists the registered component types the entity doesn't have. Right-clicking a component's header offers *Remove Component*. `GuidComponent` and `NameComponent` are neither offered nor removable (`CanAddOrRemoveComponent()`): undo, files and the game link find entities by guid, and the outliner shows the name.
- **Recording:** `ComponentEdits.h` has `AddComponent()` and `RemoveComponent()`, which change the entity and push one undo step. New change kinds `AddSubobject` (snapshot of the new component, for redo) and `RemoveSubobject` (snapshot of the removed component, for undo), both with a `SubobjectSnapshot` from change 2. `RootObjectSnapshotUtils` gained `CaptureSubobjectSnapshot()` and `RestoreSubobjectSnapshot()`, which the whole-entity snapshot code now uses too.
- **Games:** `GameLinkSubscriber` sends the whole entity (`CreateEntity`) when a component is added or removed. The game's `ApplyCreateEntity()` already replaces the entity with the same guid, so no engine change was needed.
- **Assets:** `ComponentPostLoadSubscriber` handles the new change kinds like any other (it takes the target from every change), so a MeshRenderer brought back by undo loads its mesh.
- **Pending edits:** adding or removing can move other components of that type in memory, so the details panel's and the gizmo's edits in progress are dropped first. They have always ended by then, since adding or removing takes a click on a menu.
- Tests: `TestAddingAndRemovingComponents` in `NyxGameLinkSubscriberTests`: add, no second add, a mesh path edit, remove, no second remove, undo of the remove (mesh path and loaded mesh back), undo of the path and the add, redo of all three, each sent as `CreateEntity` with the game's world matching, and the guid and name staying. The details panel UI itself (button, menu) isn't covered by a test. All test programs pass.
- **To try by hand:** select a new entity, add a Transform Component and a Mesh Renderer Component, type `Mesh` and `Meshes/Cube.nyxmesh` into its mesh, and `Material` and `Materials/Textured.nyxmat` into its material: a cube appears, also in a running game. Remove the Mesh Renderer by right-clicking its header, then undo: the cube is back.

## Follow-up: Duplicate, Copy and Paste

Built and tested on Windows 2026-10-11. Tried by hand: works.

- **What:** Duplicate (Ctrl+D, a *Duplicate* button in the outliner), Copy (Ctrl+C) and Paste (Ctrl+V) of the selected entity, also in the outliner's right-click menu on an entity. The hotkeys do nothing while a text field has the keyboard. The copy becomes the selection.
- **EntityCopies.h:** `CopyEntity()` writes the entity as scene files do (`SceneSerializer::WriteEntity`). `PasteEntity()` reads that into a new entity, gives it a **new guid** and a name from `MakeCopyName()`, and records it like *Add Entity* (an `AddObject` change with a whole snapshot), so undo removes it, redo brings back the same copy (same guid), assets load through `ComponentPostLoadSubscriber`, and running games get `CreateEntity`. Damaged or empty copied data pastes nothing and leaves no entity behind.
- **Clipboard:** the editor's own (`EditorLayer::EntityClipboard`), holding the copied bytes, not the system clipboard. It survives deleting the original and opening another scene, so an entity can be pasted into another scene.
- **Names:** `MakeCopyName()` strips a trailing " (N)" and takes the smallest N from 2 up that no entity of the scene has: Cube, then Cube (2), Cube (3). Only digits in brackets count as a number.
- **Pending edits** are dropped before pasting, as for Add Component, since a new entity can move components in memory.
- **Same place:** copies keep the original's transform, so they appear on top of it.
- **README:** not changed; its feature list stays as Leon keeps it.
- Tests: `TestDuplicateAndPaste` and `TestCopyNames` in `NyxGameLinkSubscriberTests`: new guids, numbered names, whole components with loaded assets, `CreateEntity` to the game with matching worlds, undo and redo, pasting twice after deleting the original, undoing back to the original, and damaged or empty copies. All test programs pass.
- **To try by hand:** select a cube, Ctrl+D, and drag the copy away with the gizmo; a running game shows both. Ctrl+C on the copy, open another scene, Ctrl+V: it appears there. Undo removes it.
