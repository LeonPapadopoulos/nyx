#pragma once

#include "ReflectionMacros.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	// One number of a game's own on a physics material, by name, e.g. how readily it burns. Nyx keeps
	// and serves it without knowing what it means.
	NYX_REFLECT()
	struct PhysicsMaterialExtra
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Letters, digits and _ only, as the file writes it: Extra.<Name> = value"))
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01))
		float Value = 0.0f;
	};

	// What a physics material is: how a body's surface slides and bounces, how heavy its stuff is,
	// and a game's own numbers. Its defaults are the engine's material, which a collider without one
	// has.
	NYX_REFLECT()
	struct PhysicsMaterialData
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "How much it resists sliding: 0 ice, about 0.5 wood on wood, 1 rubber"))
		float Friction = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "0: no bounce, 1: bounces back as fast as it came"))
		float Restitution = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 10.0, Tooltip = "In kg per cubic metre: a body's mass is its shape's volume times this, unless its Rigid Body gives a mass. Water 1000, wood about 600, stone about 2500"))
		float Density = 1000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "Numbers of the game's own, by name, e.g. Flammability"))
		std::vector<PhysicsMaterialExtra> Extras;
	};

	// The extra of that name; `fallback` when the material has none
	float GetExtra(const PhysicsMaterialData& material, std::string_view name, float fallback);

	// A physics material file (.nyxphysmat): its values, one per line; every value not listed takes
	// the engine's default, and a game's own numbers are "Extra." and their name.
	//
	//   # Assets/PhysicsMaterials/Wood.nyxphysmat
	//   Friction = 0.6
	//   Restitution = 0.1
	//   Density = 600
	//   Extra.Flammability = 0.8
	//
	// Text, so it diffs, merges and is written by hand. Like a material file, its lines are kept as
	// they are: comments, blank lines and lines it doesn't know. Writing values changes their own
	// lines alone.
	class PhysicsMaterialAsset
	{
	public:
		static constexpr std::string_view Extension = ".nyxphysmat";
		static constexpr std::string_view ExtraPrefix = "Extra.";

		// False for a line that is neither blank, a comment ('#') nor "Name = value", with its number
		// and what is wrong in `outError`; nothing is kept then
		bool Parse(std::string_view text, std::string& outError);
		std::string ToText() const;

		bool Load(const std::filesystem::path& file, std::string& outError);
		bool Save(const std::filesystem::path& file, std::string& outError) const;

		// The values it sets, over the defaults, with its extras in the file's order (a name twice:
		// the last line's value). A value that isn't a number is left out, and listed in `outProblems`.
		PhysicsMaterialData Read(std::vector<std::string>* outProblems = nullptr) const;
		// Writes the values into the lines: a value that reads differently changes its own line, or
		// gets one at the end; an extra the data no longer has loses its line; an extra whose name
		// isn't letters, digits and _ is left out. Other lines stay as they are.
		void Write(const PhysicsMaterialData& data);

		// A value as written, e.g. "0.6"; null when the file sets none
		const std::string* FindValue(std::string_view name) const;
		// Changes the value's line, or adds a line at the end
		void SetValue(std::string_view name, std::string value);
		bool RemoveValue(std::string_view name);

	private:
		struct Line
		{
			// Empty for a blank line or a comment, which Text holds as it is
			std::string Name;
			std::string Value;
			std::string Text;
		};
		std::vector<Line> Lines;
	};

	// Whether an extra's name can be written: letters, digits and _, at least one
	bool IsPhysicsMaterialExtraName(std::string_view name);
}

#include "Generated/Runtime/PhysicsMaterialAsset.reflect.h"
