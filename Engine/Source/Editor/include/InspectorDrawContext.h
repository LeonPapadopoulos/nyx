#pragma once

#include "Entity.h"
#include <cstdint>
#include <glm/glm.hpp>

namespace Nyx::Editor
{
	struct InspectorTargetId
	{
		uint64_t Value = 0;

		bool operator==(const InspectorTargetId&) const = default;
		bool operator!=(const InspectorTargetId&) const = default;
	};

	struct TransformRotationEditState
	{
		bool bEditing = false;
		InspectorTargetId TargetId{};
		glm::vec3 CachedDegrees{ 0.0f };
	};

	struct InspectorDrawContext
	{
		InspectorTargetId CurrentTargetId{};
		TransformRotationEditState TransformRotationState;
	};
}

namespace Nyx::Editor
{
	inline InspectorTargetId MakeInspectorTargetId(Nyx::Engine::Entity entity)
	{
		return InspectorTargetId{ static_cast<uint64_t>(entity.Value) };
	}

	inline InspectorTargetId MakeInspectorTargetId(const void* objectPtr)
	{
		return InspectorTargetId{ static_cast<uint64_t>(reinterpret_cast<uintptr_t>(objectPtr)) };
	}
}