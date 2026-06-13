#pragma once

#include "ReflectedTransactionSystem.h"
#include "SceneDocument.h"
#include "TransformComponent.h"

namespace Nyx::Editor
{
	class ReflectedSceneResolver final : public IReflectedObjectResolver
	{
	public:
		void* ResolveMutable(
			EditorTransactionContext& context,
			InspectorTargetId targetId,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;
	};
}