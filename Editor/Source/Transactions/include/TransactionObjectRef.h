#pragma once

#include "InspectorTargetId.h"

#include <cstdint>

namespace Nyx::Editor
{
	enum class EObjectDomain : uint8_t
	{
		None = 0,
		SceneEntity,
		// The input asset the Input window edits (InputAssetDocument)
		InputAsset,
		// The physics material the Physics Material window edits (PhysicsMaterialDocument)
		PhysicsMaterial
	};

	struct ObjectRef
	{
		EObjectDomain Domain = EObjectDomain::None;
		InspectorTargetId Id{};

		bool IsValid() const
		{
			return Domain != EObjectDomain::None && Id.Value != 0;
		}

		bool operator==(const ObjectRef&) const = default;
	};
}