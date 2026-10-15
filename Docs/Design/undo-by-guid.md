# Undo by guid

How the editor's undo names what it changes, and why. The code is in `Editor/Source`
(`TransactionSystem`, `TransactionTypes`, `SceneEntityTransactionDomain`,
`ReflectedObjectSnapshot`, `ComponentEdits`, `EntityCopies`).

## Steps name entities by guid

An undo step names the entity it changed by its `EntityGuid` (`MakeSceneEntityRef()`), and finds
it again by guid when it is undone or redone. Never by `Entity` handle:

- A handle is a slot in the registry, and a deleted entity's slot is reused by the next one made.
  A step that kept the handle would, after delete A, add B, undo, undo, act on B instead of
  bringing A back.
- An entity that comes back through undo is made anew, in whatever slot is free. It gets back its
  old guid, so every later step that names it still finds it.

The same guid names the entity in scene files and in the messages to a running game, so undo,
saving and live edits all agree on which entity is which.

## Steps keep components whole

- **A value edit** (`SetValueChange`) holds the property's value before and after, and where the
  property is: its component, and for a property inside a struct (a `MeshRenderer`'s mesh, an
  `AssetReference`), the path of struct properties down to it. Without that path, undoing an edit
  of a struct's field would find nothing to change.
- **Deleting or adding an entity, or a component** keeps a snapshot of it whole
  (`RootObjectSnapshot`, `SubobjectSnapshot`): every `Serialize` property, as one block of the
  tagged format scene files use. Undo brings it back exactly as it was, struct properties and
  asset paths included, and loads its assets again.
- **Copy and paste, and duplicate** (`EntityCopies.h`) keep the entity as a scene file stores it.
  The copy always gets a new guid, since a guid names exactly one entity.

## Edits in progress

`TransactionSystem::Push()` records an edit whose changes are already made. While a value is
dragged or typed, `Preview()` tells the subscribers about the changes without recording them; the
end of the drag records one step. That is how a running game sees a drag every frame while undo
gets a single step.

## Who listens

Subscribers (`ITransactionSubscriber`) hear of every recorded edit, preview, undo and redo:

- `GameLinkSubscriber` sends the state of every entity a step touched to the running games;
- `ComponentPostLoadSubscriber` loads the assets of components that changed.

The material editor (`MaterialDocument`) has an undo history of its own, so undo in a material
doesn't undo the scene, and the other way round.

`NyxEditorSceneTests`, `NyxEditorPanelTests` and `NyxGameLinkSubscriberTests` cover undo, the
editing commands and what the game receives.
