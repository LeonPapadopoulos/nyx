#pragma once

#include "PropertyValue.h"
#include "ReflectionTypes.h"

namespace Nyx::Reflection
{
	// Reads and writes a reflected property through a PropertyValue, without knowing its C++ type.
	// Used by the scene serializer, the game when it applies the editor's edits, and the editor.

	// The property's current value. A Struct property, or a kind this build doesn't know, gives
	// no value (std::monostate).
	PropertyValue GetPropertyValue(const void* object, const PropertyMetadata& property);

	// Sets the property. The value must hold the property's kind (as GetPropertyValue returns it),
	// or std::bad_variant_access is thrown. Struct properties are left as they are.
	void SetPropertyValue(void* object, const PropertyMetadata& property, const PropertyValue& value);
}
