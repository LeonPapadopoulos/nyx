# Overridable values

A component that takes most of its values from somewhere else, such as a data file or a parent, and
overrides a few of them, as a material instance does its parent's parameters. The component keeps
only what it overrides; everything else follows its source, also when the source changes later. The
editor's Details panel shows every value the instance has: an inherited one greyed, with where it
comes from, an overridden one with a reset arrow. Nyx knows nothing of where the values come from:
the component's type lists them.

The engine's side is `ValueOverride.h` (in `Runtime/Scene`) and the component registry; the
editor's is `OverrideRows` (in the editor folder `Properties`).

## The shape

Two pieces, one saved and one asked for:

- **The overrides**, saved with the component: a property `std::vector<Nyx::ValueOverride>`
  (`Name`, `Value`) with the metadata `UI = Overrides`. It is an ordinary reflected array, so
  scene files, copies, undo, live edits to a running game and NyxDump treat it as they treat any
  array of structs. An entry names the value it overrides; a value without an entry has none.
- **The provider**, registered for the component type in a game module's `RegisterTypes()`:
  `ComponentTypeRegistry::SetOverridableValues<T>(fn)`. `fn(component, out)` appends an
  `OverridableValue` for each value this instance can override: `Name` (what an entry saves),
  `DisplayName`, `Inherited` (its value without an override), `Min` and `Max` (`-FLT_MAX` and
  `FLT_MAX` for no range), `Tooltip` and `Source` (where `Inherited` comes from, e.g. a file name).

The provider is asked every time the panel draws the property, never cached: which values exist may
depend on the instance, e.g. on another of its fields naming which file it follows, and the source
may change at any time. It should be cheap, e.g. read values a game already holds in memory.

A game's systems use the same two pieces: `GetOverriddenValue(overrides, name, inherited)` is the
entry's value if there is one, else the inherited one.

Only floats, for now. A value of another kind would need its own entry type or a value that holds
several kinds; nothing about the provider or the rows depends on the value being a float apart from
the field drawn.

## In the Details panel

The property is drawn as a tree row, "*Values (2 of 5 overridden)*", with one row per listed value
in the provider's order:

- Not overridden: the inherited value, greyed. Its tooltip says where it comes from and its range.
- Overridden: the entry's value, in the normal colour, with a reset arrow at the row's end that
  takes the entry away.
- Editing a greyed value adds its entry; editing an overridden one changes it. An edit keeps the
  value in `Min..Max` when it has a range. Values stored outside a range (because the range changed
  later) are kept as they are until edited.
- An entry the provider doesn't list (the value was renamed, or belongs to another choice of the
  instance), or a second entry of a name, is shown as *not listed* in the colour of warnings, with a
  *Remove* button. It is never dropped by itself: the value may come back, e.g. when the instance's
  choice is set back.

Without a provider, or for a property that isn't an array of `ValueOverride`, the property is drawn
as any array. The provider is found by the type that owns the property, so the overrides must be
the component's own property, not a field of a struct inside it, and a type has one overrides
property: a second one would list the same values.

## Edits and undo

Every edit goes through the editor's ordinary changes to the array, as the array's own rows make
them, so undo, saving and live edits behave as for any array property:

- Reset and Remove replace the array as one undo step (`ReplaceArrayChange`), like removing an
  element by its right-click menu.
- Dragging or typing an overridden value is a value change of that element (`SetValueChange` with
  the element's location), previewed to a running game while it is dragged and one undo step when
  the drag ends, like dragging an element's field.
- Dragging or typing a greyed value adds the entry on the first change and previews the array to a
  running game at once; the drag goes on as a value change of the new element, and its end records
  the whole edit as one `ReplaceArrayChange` from the array before the entry was added. One undo
  takes the entry away again.

The edit in progress is the draw context's `OverrideEdit`, like the generic property edit beside
it, and `InspectorDrawContext::PreviewPendingEdits()` previews it each frame.
