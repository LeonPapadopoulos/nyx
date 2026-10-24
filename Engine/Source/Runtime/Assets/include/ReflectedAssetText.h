#pragma once

#include "ReflectionTypes.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	// What is wrong with a value, said at its property. Path names the property from the top: its
	// names apart by '.', an array's element by its name if it is a named entry ("Moves.Bite.Damage"),
	// else by its index ("Points[2].X", "Tags[1]"), as GetElementPath() makes them.
	struct PropertyProblem
	{
		std::string Path;
		std::string Message;
	};

	// A reflected struct type, with what it takes to make, copy and destroy one without knowing it
	struct ReflectedStruct
	{
		const Reflection::TypeMetadata* Metadata = nullptr;
		void* (*Create)() = nullptr;
		void (*Destroy)(void* object) = nullptr;
		void (*Copy)(void* to, const void* from) = nullptr;

		template <typename T>
		static ReflectedStruct Of()
		{
			return ReflectedStruct{
				.Metadata = &Reflection::GetTypeMetadata<T>(),
				.Create = []() -> void* { return new T(); },
				.Destroy = [](void* object) { delete static_cast<T*>(object); },
				.Copy = [](void* to, const void* from) { *static_cast<T*>(to) = *static_cast<const T*>(from); },
			};
		}
	};

	// An object of a ReflectedStruct, with its default values
	using ReflectedObjectPtr = std::unique_ptr<void, void (*)(void*)>;
	ReflectedObjectPtr MakeReflectedObject(const ReflectedStruct& type);

	// An asset's values as text, read and written through reflection: a line "Name = value" for each
	// value of a Serialize property, lines starting with '#' are comments.
	//
	//   # A creature's stats
	//   DisplayName = Marsh Hound                    (a string, as it is; "quoted" with \" and \\ when
	//   Speed = 4.5                                   it has spaces at its ends, starts with a quote,
	//   bNocturnal = true                             or holds a comma in a list)
	//   Diet = Meat                                  (an enum: its enumerator's name)
	//   Tint = 0.4 0.3 0.2                           (vectors and colours: numbers apart by spaces)
	//   Portrait = Textures/Hound.png                (an AssetReference: its path; its type is the
	//                                                 property's AssetType)
	//   Tags = Swamp, Pack                           (an array of anything but structs: one line,
	//                                                 apart by commas; empty for none)
	//   Senses.Smell = 0.9                           (a struct's values: its name, '.', theirs)
	//   Move.Bite.Damage = 12                        (named entries: an array of structs with a string
	//   Move.Bite.Reach = 1.5                         Name, each by its name; TextName = Move gives
	//                                                 the property the name its lines use)
	//   Resist.Fire = 0.5                            (an entry with one value besides its name: the
	//                                                 entry is the line)
	//   Range.Sight = 30 5 60                        (a struct NYX_REFLECT(meta = (Inline)): its values
	//                                                 on one line, apart by spaces; those missing keep
	//                                                 their defaults)
	//   Waypoints[0].Wait = 2                        (other arrays of structs: each element by index)
	//
	// Text, so it diffs, merges and is written by hand. Its lines are kept as they are: comments,
	// blank lines and lines it doesn't know. Writing values changes the lines of the values that read
	// differently, adds lines at the end of their struct's or array's lines (or of the file), and
	// takes away the lines of entries and elements the values no longer have; every value not
	// written keeps its default. Entry names are letters, digits and _ (IsEntryName()).
	class ReflectedAssetText
	{
	public:
		// False for a line that is neither blank, a comment ('#') nor "Name = value", with its number
		// and what is wrong in `outError`; nothing is kept then
		bool Parse(std::string_view text, std::string& outError);
		std::string ToText() const;

		bool Load(const std::filesystem::path& file, std::string& outError);
		bool Save(const std::filesystem::path& file, std::string& outError) const;

		// The values the lines set, into `object`, of `type`, which must have its defaults: its arrays
		// of structs are what the lines list, the rest keeps its defaults where no line sets it. A
		// line the type doesn't know at all is kept quietly, for a later Nyx or a game's own tools; one
		// whose value can't be read, or that names a value the type doesn't have, is left out and
		// listed in `outProblems` (the last line wins for a name twice).
		void Read(void* object, const Reflection::TypeMetadata& type, std::vector<PropertyProblem>* outProblems = nullptr) const;

		template <typename T>
		T Read(std::vector<PropertyProblem>* outProblems = nullptr) const
		{
			T object{};
			Read(&object, Reflection::GetTypeMetadata<T>(), outProblems);
			return object;
		}

		// Writes `object`'s values into the lines, changing as little as reads the same (see above)
		void Write(const void* object, const ReflectedStruct& type);
		// Writes every value, also those at their defaults, e.g. for a new file to be changed by hand
		void WriteAll(const void* object, const ReflectedStruct& type);

		template <typename T>
		void Write(const T& object)
		{
			Write(&object, ReflectedStruct::Of<T>());
		}

		// A value as written, e.g. "0.6"; null when no line sets it
		const std::string* FindValue(std::string_view name) const;
		// Changes the value's line, or adds a line at the end
		void SetValue(std::string_view name, std::string value);
		bool RemoveValue(std::string_view name);
		// "# comment" at the end
		void AddComment(std::string_view comment);

	private:
		struct Line
		{
			// Empty for a blank line or a comment, which Text holds as it is
			std::string Name;
			std::string Value;
			// The line as it was read; empty once its value changed
			std::string Text;
		};

		void Write(const void* object, const ReflectedStruct& type, bool bAll);
		// Where a new line for `key` goes: after the last line of its struct, entry or array
		size_t FindInsertPosition(std::string_view key) const;

		std::vector<Line> Lines;
	};

	// Whether a named entry's name can be written: letters, digits and _, at least one
	bool IsEntryName(std::string_view name);

	// A named entry's name property: the type's Serialize string property "Name"; null for a type
	// whose arrays are elements by index
	const Reflection::PropertyMetadata* FindEntryNameProperty(const Reflection::TypeMetadata& type);

	// The array's named entries' name property (FindEntryNameProperty()); null for other arrays
	const Reflection::PropertyMetadata* FindEntryNameProperty(const Reflection::ArrayMetadata& array);

	// A name none of the array's entries has: `base`, else base2, base3, ...
	std::string MakeEntryName(const void* array, const Reflection::ArrayMetadata& arrayMetadata, std::string_view base);

	// The path of an array's element (see PropertyProblem): "<array>.<name>" for a named entry whose
	// name can be written, else "<array>[<index>]". elementType: the element's struct, or null.
	std::string GetElementPath(std::string_view arrayPath, const void* element, const Reflection::TypeMetadata* elementType, size_t index);

	// The names of every named entry in `object`: missing, not a name, or two entries of the same
	// array named alike, each at the entry's Name
	void CheckEntryNames(const void* object, const Reflection::TypeMetadata& type, std::vector<PropertyProblem>& outProblems);
}
