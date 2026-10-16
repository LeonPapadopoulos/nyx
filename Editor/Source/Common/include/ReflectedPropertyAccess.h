#pragma once

#include "PropertyValue.h"
#include "PropertyValueUtils.h"
#include "ReflectionTypes.h"

namespace Nyx::Editor
{
	// Nyx::Reflection::GetPropertyValue and SetPropertyValue (PropertyValueAccess.h), with
	// rotations (Quat) normalized, as the editor compares and records them.
	Nyx::Reflection::PropertyValue ReadReflectedPropertyValue(
		const void* object,
		const Nyx::Reflection::PropertyMetadata& property);

	void WriteReflectedPropertyValue(
		void* object,
		const Nyx::Reflection::PropertyMetadata& property,
		const Nyx::Reflection::PropertyValue& value);
}