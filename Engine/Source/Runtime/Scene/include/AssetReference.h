#pragma once

#include "ReflectionMacros.h"

#include <string>

namespace Nyx::Engine
{
	NYX_REFLECT()
	struct AssetReference
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Type;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Path;

		bool IsValid() const
		{
			return !Type.empty() && !Path.empty();
		}

		void Reset()
		{
			Type.clear();
			Path.clear();
		}
	};
}

#include "Generated/Runtime/AssetReference.reflect.h"