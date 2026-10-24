#pragma once

#include "ReflectionMacros.h"

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
	//
	// Its file (.nyxphysmat) is its values as text (ReflectedAssetText), every value not listed the
	// engine's, a game's own numbers "Extra." and their name:
	//
	//   # Assets/PhysicsMaterials/Wood.nyxphysmat
	//   Friction = 0.6
	//   Restitution = 0.1
	//   Density = 600
	//   Extra.Flammability = 0.8
	NYX_REFLECT()
	struct PhysicsMaterialData
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "How much it resists sliding: 0 ice, about 0.5 wood on wood, 1 rubber"))
		float Friction = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "0: no bounce, 1: bounces back as fast as it came"))
		float Restitution = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 10.0, Tooltip = "In kg per cubic metre: a body's mass is its shape's volume times this, unless its Rigid Body gives a mass. Water 1000, wood about 600, stone about 2500"))
		float Density = 1000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (TextName = Extra, Tooltip = "Numbers of the game's own, by name, e.g. Flammability"))
		std::vector<PhysicsMaterialExtra> Extras;
	};

	inline constexpr std::string_view PhysicsMaterialExtension = ".nyxphysmat";

	// The extra of that name; `fallback` when the material has none
	float GetExtra(const PhysicsMaterialData& material, std::string_view name, float fallback);

	// Physics materials as a reflected asset type (ReflectedAssetTypes): "PhysicsMaterial", checking
	// that friction and density aren't below 0 and restitution is from 0 to 1
	void RegisterPhysicsMaterialAssetType();
}

#include "Generated/Runtime/PhysicsMaterialAsset.reflect.h"
