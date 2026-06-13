#pragma once

#include "SceneDocument.h"
#include "TransactionSystem.h"
#include "TransformComponent.h"

namespace Nyx::Editor
{
	class SceneTransactionResolver final : public ITransactionObjectResolver
	{
	public:
		void* ResolveMutable(
			EditorTransactionContext& context,
			InspectorTargetId targetId,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;
	};
}