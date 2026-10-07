#pragma once

#include "SceneDocument.h"
#include "TransactionDomain.h"

namespace Nyx::Editor
{
	class SceneEntityTransactionDomain final : public ITransactionDomain
	{
	public:
		void* ResolveMutable(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		bool CreateRootObject(
			EditorTransactionContext& context,
			const ObjectRef& root) override;

		bool DeleteRootObject(
			EditorTransactionContext& context,
			const ObjectRef& root) override;

		void EnumerateSubobjects(
			EditorTransactionContext& context,
			const ObjectRef& root,
			std::vector<ReflectedObjectView>& outSubobjects) override;

		bool EnsureSubobject(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		bool RemoveSubobject(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;
	};
}