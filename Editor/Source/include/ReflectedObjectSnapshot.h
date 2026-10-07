#pragma once

#include "PropertyValue.h"
#include "ReflectionTypes.h"

#include <vector>

namespace Nyx::Editor
{
	struct ReflectedPropertySnapshot
	{
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Value;
	};

	struct ReflectedObjectSnapshot
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		std::vector<ReflectedPropertySnapshot> Properties;
	};

	struct RootObjectSnapshot
	{
		bool bAlive = false;
		std::vector<ReflectedObjectSnapshot> Subobjects;
	};
}