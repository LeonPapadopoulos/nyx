#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx
{
	// Enumerators count up from 0, or from a number given to one
	NYX_ENUM(meta = (DisplayName = "Dead Zone Shape"))
	enum class EDeadZoneShape : uint8_t
	{
		Axial,
		Radial,
		ScaledRadial = 5,
		Hybrid,
	};

	NYX_ENUM()
	enum class ESigned : int16_t
	{
		Below = -2,
		Zero = 0,
	};

	NYX_REFLECT()
	struct ArrayElement
	{
		NYX_PROPERTY(Edit, Serialize)
		std::string Name;
	};

	NYX_REFLECT()
	struct EnumAndArrayTest
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Debug"))
		EDeadZoneShape Shape = EDeadZoneShape::ScaledRadial;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<float> Floats;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<glm::vec2> Points;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<ESigned> Signs;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<ArrayElement> Elements;
	};
}
