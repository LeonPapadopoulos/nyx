#pragma once

#include "SceneDocument.h"
#include "SceneEntitySnapshot.h"
#include "TransactionDomain.h"

namespace Nyx::Editor
{
	class SceneEntityTransactionDomain final : public ITransactionDomain
	{
	public:
		void* ResolveMutable(
			EditorTransactionContext& context,
			const ObjectRef& objectRef,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		bool CreateObject(
			EditorTransactionContext& context,
			const AddObjectChange& change) override;

		bool DeleteObject(
			EditorTransactionContext& context,
			const DeleteObjectChange& change) override;

		SceneEntitySnapshot CaptureSnapshot(
			Nyx::SceneDocument& scene,
			Nyx::Engine::Entity entity) const;

	private:
		void RestoreSnapshot(
			Nyx::SceneDocument& scene,
			Nyx::Engine::Entity entity,
			const SceneEntitySnapshot& snapshot) const;
	};
}