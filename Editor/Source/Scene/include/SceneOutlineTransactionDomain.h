#pragma once

#include "TransactionDomain.h"

namespace Nyx::Editor
{
	// The scene's outline in undo steps (EObjectDomain::SceneOutline): one root object, the active
	// scene's SceneOutline, with itself as its only subobject. Its edits replace its arrays
	// (ReplaceArrayChange), so a step holds the outline before and after, whatever entity slots
	// the entities it names are in.
	class SceneOutlineTransactionDomain final : public ITransactionDomain
	{
	public:
		static ObjectRef GetRef()
		{
			return ObjectRef{ EObjectDomain::SceneOutline, InspectorTargetId{ 1 } };
		}

		void* ResolveMutable(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		bool CreateRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		bool DeleteRootObject(EditorTransactionContext& context, const ObjectRef& root) override;

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
