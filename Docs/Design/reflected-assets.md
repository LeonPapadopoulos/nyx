# Reflected assets

A game's own data files, edited in the editor with no game code in Nyx. A game registers an asset
type: a file extension, a display name and a reflected struct whose properties are the file's
contents, and if it likes a check of its values. Nyx reads and writes the file as readable text
through reflection, the editor opens it in a window of its properties with undo, makes new ones,
shows what is wrong at the values it is about, reads it again when it changes on disk, and sends its
values to the running games as they change, where whoever registered the type takes them. Physics
materials (`.nyxphysmat`) are Nyx's own type on the same path.

The text and the registry are in `Engine/Source/Runtime/Assets` (`ReflectedAssetText`,
`ReflectedAssetTypes`), the windows in the editor folder `ReflectedAssets`
(`ReflectedAssetDocument`, `ReflectedAssetEditors`, `ReflectedAssetWindows`), and the link's side in
`Link` (`AssetValuesMessage`, `ApplyAssetValues()`).

## A worked example: a creature's stats

A game with creatures whose stats a designer tunes. The struct, in a folder of the game module's
`REFLECT_DIRS`:

```cpp
NYX_ENUM()
enum class EDiet : uint8_t { Plants, Meat, Anything };

// One attack, by name: "Attack.Bite.Damage = 12"
NYX_REFLECT()
struct CreatureAttack
{
    NYX_PROPERTY(Edit, Undo, Serialize)
    std::string Name;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Damage = 1.0f;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Reach = 1.0f;
};

// A number by name: "Resist.Fire = 0.5"
NYX_REFLECT()
struct CreatureResistance
{
    NYX_PROPERTY(Edit, Undo, Serialize)
    std::string Name;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Amount = 0.0f;
};

// A tunable range on one line, default, min and max: "Range.Sight = 30 10 60"
NYX_REFLECT(meta = (Inline))
struct CreatureRange
{
    NYX_PROPERTY(Edit, Undo, Serialize)
    std::string Name;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Default = 0.0f;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Min = 0.0f;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Max = 1.0f;
};

NYX_REFLECT()
struct CreatureStats
{
    NYX_PROPERTY(Edit, Undo, Serialize)
    std::string DisplayName;

    NYX_PROPERTY(Edit, Undo, Serialize)
    float Speed = 3.0f;

    NYX_PROPERTY(Edit, Undo, Serialize)
    EDiet Diet = EDiet::Plants;

    NYX_PROPERTY(Edit, Undo, Serialize, meta = (UI = Color))
    glm::vec3 Tint{ 1.0f };

    NYX_PROPERTY(Edit, Undo, Serialize, meta = (AssetType = Material))
    Nyx::AssetReference Skin;

    NYX_PROPERTY(Edit, Undo, Serialize)
    std::vector<std::string> Habitats;

    NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Attack))
    std::vector<CreatureAttack> Attacks;

    NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Resist))
    std::vector<CreatureResistance> Resistances;

    NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Range))
    std::vector<CreatureRange> Ranges;
};
```

Registered in the module's `RegisterTypes()` (`GameModule::RegisterTypes`), which the editor and
the game both run:

```cpp
Nyx::ReflectedAssetTypes::Get().Register(Nyx::ReflectedAssetDesc<CreatureStats>{
    .TypeId = "Creature",                 // what asset references and the editor call it
    .DisplayName = "Creature",            // "New Creature", the window's title
    .Extension = ".creature",
    .Description = "A creature's stats",  // New's tooltip, and a new file's first comment
    .Validate = [](const CreatureStats& creature, std::vector<Nyx::PropertyProblem>& outProblems)
    {
        for (const CreatureRange& range : creature.Ranges)
        {
            if (range.Min > range.Max)
            {
                outProblems.push_back({ "Ranges." + range.Name + ".Min", "is above Max" });
            }
        }
    } });
```

A file of it, as written by hand or by the editor:

```
# A creature's stats
DisplayName = Marsh Hound
Speed = 4.5
Diet = Meat
Tint = 0.4 0.3 0.2
Skin = Materials/Hound.nyxmat
Habitats = Swamp, Reeds
Attack.Bite.Damage = 12
Attack.Bite.Reach = 1.5
Resist.Fire = 0.5
Range.Sight = 30 10 60
```

The game reads its files with `ReflectedAssetText` (`Load()`, then `Read<CreatureStats>()`, or
`LoadReflectedAsset()` with the registered type), and takes the editor's live values by listening
for its type while it runs:

```cpp
// A member of what keeps the creatures, so it stops listening when that goes
Nyx::ReflectedAssetListener LiveCreatures{ ".creature", [this](const std::string& path, std::string_view text)
{
    Nyx::ReflectedAssetText values;
    std::string error;
    if (values.Parse(text, error))
    {
        std::vector<Nyx::PropertyProblem> problems;
        Creatures[path] = values.Read<CreatureStats>(&problems);
    }
} };
```

That is all: the asset browser lists `.creature` files, double-clicking one opens its window, the
folder menu has *New Creature*, and a value being dragged reaches the running game every frame.

## The text

A line `Name = value` for each value of a Serialize property; `#` starts a comment line. The name is
the property's, or its `TextName`.

| Shape | Line |
| --- | --- |
| Bool | `bNocturnal = true` (`true`, `false`, `1`, `0`) |
| Int32, UInt32, UInt64 | `Legs = 4` (UInt64 also `0x...`) |
| Float | `Speed = 4.5`, written as short as reads back as the same float |
| Vec2, Vec3, Vec4, colours | `Tint = 0.4 0.3 0.2`, numbers apart by spaces |
| Quat | `x y z w` |
| String | as it is; in quotes, with `\"` `\\` `\n` `\r` `\t`, when it has spaces at its ends, starts with a quote, holds a line break, or in a list holds a comma or a quote |
| Enum | its enumerator's name (a number reads too) |
| `AssetReference` | its path; its type is the property's `AssetType` |
| Array of anything but structs | one line, items apart by commas, empty for none: `Habitats = Swamp, "Reed, Bank"` |
| Struct | its values' lines, its name and a dot in front: `Senses.Smell = 0.9` |
| Struct `NYX_REFLECT(meta = (Inline))` | one line, its values apart by spaces in their order; missing ones keep their defaults: `Range.Sight = 30 10` |
| Named entries: an array of structs with a Serialize string `Name` | each entry by its name: `Attack.Bite.Damage = 12`; an entry with one value besides its name is one line, `Resist.Fire = 0.5`; an Inline one is one line too |
| Other arrays of structs | each element by its index: `Waypoints[1].Wait = 2` |

An entry's name is letters, digits and `_` (`IsEntryName()`), so it never holds a dot. A value whose
property has no line keeps its default; arrays of structs are what the lines list, none without.
For a name twice, the last line wins.

**Reading** (`Read()`) leaves a value that can't be read as it was and lists it, with what is wrong,
at its property (`PropertyProblem`). So is a line naming a value the type doesn't have below a
property it has (`Senses.Taste`, `Attack.Bite.Speed`); an entry only such a line names isn't made. A
line whose first name the type doesn't know at all is kept quietly, for a later version or a game's
own tools.

**Writing** (`Write()`) changes as little of the file as says the same:

- A value that reads the same keeps its line as written (`12.0` stays `12.0`).
- A changed value changes its own line; the first of a name twice takes it and the later ones go.
- A value without a line gets one only if it isn't at its default, after the last line of its
  entry, struct or array, or else at the end of the file.
- A new entry or element writes its values that aren't at their defaults, or its first value when
  all are, so it is there.
- The lines of entries and elements the values no longer have go; an entry renamed is one gone and
  one new.
- Entries moved or put in above others: the array's lines in the values' order, where its first
  line was; comments between them stay where they were, so they end up after them.
- Comments, blank lines and unknown lines stay.

So undoing an edit gives the file's text back, and a file written by hand stays as it was where it
wasn't edited. `WriteAll()` writes every value, defaults too: what *New* writes, for a file to be
changed by hand.

Why text: it diffs, merges and is written by hand, as materials and physics materials were already.
Why lines kept rather than a file written whole: a designer's comments and a later version's lines
survive the editor.

## What reflection gained

Reflection had arrays (`std::vector` of any kind but arrays) and no maps. Named entries are the
smallest general way to a map from name to struct, or to number: an array of structs with a string
`Name`, by convention, which the binary archive, undo, live edits and the details table already
handle, with no new property kind. A game that wants a map keeps a vector and finds by name, or
builds its own map after reading. Two metadata keys, both checked by `NyxHeaderTool`
(`SpecifierRegistry.cpp`, fixture `text_metadata`):

- `TextName = <identifier>` on a property: its name in the text, e.g. `Extras` written `Extra.<Name>`.
- `Inline` on a type: written on one line. Only for values a line can hold: numbers, bools, enums,
  vectors; a type with others is written value by value.

## Problems

`PropertyProblem` is a path and a message. The path names the property from the top, C++ names
apart by dots, an element by its entry name when it has one that can be written
(`Ranges.Sight.Min`), else by its index (`Attacks[2].Name`, `Habitats[1]`), as `GetElementPath()`
makes them. A type's `Validate` writes them for its rules; `ValidateReflectedAsset()` adds the
entries' names: missing, not a name, or two alike in one array (`CheckEntryNames()`). The editor's
details table shows each under the row it names (`InspectorDrawContext::Problems`); the window
lists those about no row drawn at its bottom.

## The editor

- `RegisterEditorAssetTypes()` registers an editor asset type for each reflected one: listed in the
  asset browser, opened on double click, made by *New \<DisplayName\>* in a folder's right-click
  menu (`AssetTypeDescriptor::Create`), which writes every value at its default
  (`ReflectedAssetEditors::Create()`, `New<TypeId>.<ext>`, numbered apart) and opens it.
- `ReflectedAssetEditors` keeps any number of assets open, each a `ReflectedAssetDocument`: its
  lines and an object of its struct. It is the undo domain `EObjectDomain::ReflectedAsset`, each
  asset an object by its own id, so the details table's edits, its array rows' adds, removes and
  moves, a rename through an entry's Name, their undo and redo and a value being dragged are
  recorded and previewed as a component's are, and Ctrl+Z undoes them in turn with the scene's. An
  asset opened again, or read again from its file, is another object: undo steps from before find
  nothing.
- Each change writes the values into the lines, checks them, and counts a revision.
- `ReflectedAssetWindows` draws a window per asset, titled *\<DisplayName\>: \<name\>*: a dirty
  marker, *Save* (Ctrl+S while it has the focus), the number of problems, the values, a problem under
  its value. An entry added through the details table's *Add* gets a name of its own, *Entry*,
  *Entry2*, ... (`InspectorDrawContext::bNameNewEntries`, `MakeEntryName()`). Closing a window drops
  changes not saved.
- Files changed on disk: the editor watches the asset folders (`Watch()`, `Update()` each frame).
  An open asset whose file now says something else is read again when it has no changes; with
  changes it keeps them, and its window says the file changed, with *Reload*. Its own save, seen on
  disk, changes nothing.

## Live

Every frame an asset's revision changed, the editor sends its whole text to the running games
(`AssetValuesMessage`, link message 17, which was `PhysicsMaterialValues` and now carries any
reflected asset), saved or not; a game that starts gets the unsaved ones first; one read again from
disk is sent too. The game's `EditorLinkLayer` hands it to `ApplyAssetValues()`, which gives it to
everyone listening for the path's extension (`ReflectedAssetTypes::Listen()`, or a
`ReflectedAssetListener` for as long as it lives); nobody listening is a warning. Sending each change
is cheap: the files are small, and only the asset that changed goes.

## Physics materials on it

`PhysicsMaterialData` is the registered struct (`RegisterPhysicsMaterialAssetType()`, in
`RegisterEngineAssetTypes()`, which the Application runs before a game module's `RegisterTypes()`):
its extras are named entries with `TextName = Extra`, so files keep reading `Extra.Flammability =
0.8`. Its check: friction and density not below 0, restitution from 0 to 1. `PhysicsMaterials`
(Sim) reads files with `ReflectedAssetText`; the game's `GameLayer` listens for `.nyxphysmat` and
hands the text to its world's `PhysicsMaterials::SetText()`. The *Physics Material* window, its
document and its own undo domain are gone: its window is the generic one.

## Tests

- `NyxReflectedAssetTests`, on the test module's creature (`TestCreatureAsset.h`, registered in
  `RegisterModule()`, every shape): written to the exact text and read back value by value, through
  a file, unchanged written again unchanged, defaults writing nothing and `WriteAll()` everything,
  words in quotes; a hand-written file's comments, unknown line and `12.0` kept, a value changed, an
  entry renamed and one taken away, one put in above another, entries moved, all taken away, a name
  twice; every kind of value that can't be read and of name it doesn't have, at its property, and
  entries not made for them; the creature's own problems and its entries' names; new entries'
  names; the registry by extension, path and type id, a type taking another's extension refused, one
  registered again replaced; a listener taking live text, and an `AssetValues` message over a real
  editor link reaching it, a type nobody listens for ignored.
- `NyxReflectedAssetEditorTests`: a physics material edited, undone, redone and dragged, a value out
  of range a problem; the creature's attacks added with a name of their own, renamed and taken away,
  undone back to the file's text and redone, names twice and missing problems at their entries;
  what reaches the games and what a starting game gets; saving; its own save seen on disk changing
  nothing; a file changed on disk read again, the old undo doing nothing; one with changes keeping
  them until Reload; the watcher finding a change by itself; closed and opened again untouched by
  the old undo; files that aren't there or of no type refused; *New* for both types; problems drawn
  under their values; the windows drawn headless and Ctrl+S saving.
- `NyxPhysicsMaterialTests`, `NyxLiveEditTests`, `NyxHeaderToolTests` (`text_metadata`).
