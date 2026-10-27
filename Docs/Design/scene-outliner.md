# Scene outliner

The editor's list of a scene's entities: a search by name, an order of its own, and folders that
sort the entities and do nothing else. They parent no transform and no game reads them, as in
Unreal's World Outliner.

The engine's side is `SceneOutline.h` (in `Runtime/Scene`: the data, reflected) and
`SceneSerializer`, which saves it with the scene. The editor's is in its `Scene` folder:
`SceneOutlineEdits` (the tree as shown, and the edits), `SceneOutlineTransactionDomain` (its undo),
`OutlinerFolderStates` (which folders are open) and `SceneOutlinerPanel`.

## Where it lives: one outline per scene, not a component per entity

The other way would have been a small component on each entity, its folder's path and a sort key,
marked so that games skip it. The outline is the scene's instead, one object beside its entities:

- **A folder can be empty.** *New Folder* makes one with nothing in it yet, and deleting the last
  entity of a folder shouldn't take the folder away. A component on entities can't hold a folder
  that no entity is in, so folders would need a second place anyway, and two places for one tree
  can disagree.
- **Order is about all the entities at once.** Moving one entity changes where it is among the
  others; with a sort key per entity, keys run out between neighbours and have to be renumbered,
  touching entities the move didn't touch. A list is changed in one place.
- **A component goes wherever its entity goes**: into copies (a paste would bring its old folder
  and sort key along), into every message to a running game, and into the game's world when it
  loads the scene. Nyx has no kind of component that is saved but kept from games; it would need
  one, checked in the serializer, the editor link and undo's snapshots. The outline is a block of
  the scene file after the entities, which a game never reads.
- **Entities know nothing of the outliner.** An entity it doesn't list, made by a paste, a tool or
  an older build, simply shows at the end.

What it costs: an undo step of the outline holds its arrays whole, before and after (about 30 bytes
an entity). That is a few hundred kilobytes a step for ten thousand entities; if scenes grow that
big, a step can hold only the elements that moved.

## The data

`SceneOutline` has two arrays, both in order:

- `Folders`: each with an `Id` (random and 64 bits, like an entity's guid; never 0), a `Name` and
  its `Parent` folder (0 for the top). In a folder, its subfolders come first, in this array's
  order.
- `Entities`: each entity's guid and its `Folder`, in the order they show. Entities are named by
  guid, as undo and scene files name them.

What the outliner shows is made from it every frame (`BuildOutlineTree()`):

- An entity with a name that the outline doesn't list shows at the top after the listed ones, in
  the order the registry has their names. A scene saved before there was an outline lists none, so
  it shows in the order its file has its entities, as before.
- An entity that is gone stays listed while the editor runs, so undoing its delete brings it back to
  its place. Saving leaves those out and lists every entity there is (`MakeSavedOutline()`), so a
  scene loads in the order it was saved, whatever slots its entities had.
- What can't be shown is set right as it is read: a folder without an id, or with one taken
  already, is left out; a folder whose parent is gone, or whose parents lead back to it, and an
  entity whose folder is gone, show at the top.

## The file

After the entities: `SceneOutlineMarker` ('OUTL') and the outline as one property block, in the
tagged format, so properties can be added to it later as to any component. Scene files stay
version 2: a reader stops after the entities, so a build from before reads a new scene and loses
only the outline, and a scene from before has none. `LoadFromFile()` takes the outline only when
asked for it (the editor); a game asks for none, and it is skipped. Anything after the entities
that isn't an outline is skipped with a warning, for a newer build's sake.

## Undo

The outline is one root object of the editor's undo (`EObjectDomain::SceneOutline`), with itself as
its only subobject, as the input asset is. Every edit is one step of `ReplaceArrayChange`s of
`Folders` and `Entities`, of those that changed. An edit starts from the outline with every shown
entity listed in the order shown, so a step does exactly what it shows; one that would show the
same as before is not recorded, e.g. a row dropped where it is.

*Add Entity*, paste and duplicate put the new entity after the selection, in its folder, in the same
step that makes the entity (`PlaceNewEntity()`, and `PasteEntity()`'s `alsoDo`): undo takes both
back at once, and redo makes the entity again, then puts it in its place. Deleting a folder moves
its subfolders to where it was and its entities first among its parent's, as they showed above
them; no entity is deleted.

The game link and the asset loader only look at changes of entities, so an outline edit reaches no
game and loads nothing.

## Open folders are the editor's

Whether a folder is open belongs to whoever looks, not to the scene: it isn't saved in it and isn't
undone. `OutlinerFolderStates` keeps the closed folders of each scene, by its path, in
`Saved/OutlinerFolders.txt`. A folder starts open. *Save As* takes the state along to the new path.
While searching, the folders that hold a match show open, without changing what is remembered.

## Several at once

The edits take lists of entities and folders (`OutlineItems`), and move them as one step keeping
their order, so selecting several entities later needs no other edit. What is single today is the
panel's: the scene's selection holds one entity, the outliner's one folder, and a drag carries one
row.

## Tests

`NyxEditorOutlinerTests`: saving and loading (an older scene's order, folders and order kept, a
game skipping the outline), reordering, moving into and out of folders and nesting them, deleting a
folder, where new, pasted and duplicated entities go, each with its undo; the search, Ctrl+F and the
clear button, dragging rows and the drop line, *New Folder*, renaming by F2 and a double click, and
the open folders kept per scene, all drawn headless.
