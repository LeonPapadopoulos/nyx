#pragma once

#include "Entity.h"
#include "EntityGuid.h"
#include "PropertyWidgetRegistry.h"

#include <string>

namespace Nyx::Editor
{
	// An EntityGuid that names another entity (UI = EntityRef, which the header tool gives every
	// EntityGuid property): the entity's name, "None" or "Missing (0x...)", with the guid in its
	// tooltip. A combo lists the drawn scene's entities (InspectorDrawContext::Entities) by name, with a
	// filter; an entity dragged from the outliner can be dropped on it. The eyedropper button picks the
	// next entity clicked in a scene view (InspectorDrawContext::EntityPick), Esc cancels; the select
	// button selects the entity named. Each change is one undo step.
	bool DrawEntityReferenceWidget(const PropertyWidgetArgs& args);

	// What the field shows for the guid: the entity's name; "None" without a guid; "Missing (0x...)"
	// for a guid no entity of the world has, such as a deleted one's
	std::string DescribeEntityReference(const Nyx::Registry* world, Nyx::EntityGuid guid);
}
