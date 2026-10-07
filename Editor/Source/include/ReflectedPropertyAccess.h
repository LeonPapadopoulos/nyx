#pragma once

#include "PropertyValue.h"
#include "PropertyValueUtils.h"
#include "ReflectionTypes.h"

namespace Nyx::Editor
{
	Nyx::Reflection::PropertyValue ReadReflectedPropertyValue(
		const void* object,
		const Nyx::Reflection::PropertyMetadata& property);

	void WriteReflectedPropertyValue(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::PropertyValue& value);
}