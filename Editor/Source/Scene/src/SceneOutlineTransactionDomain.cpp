#include "SceneOutlineTransactionDomain.h"

#include "SceneDocument.h"

namespace
{
	Nyx::SceneOutline* FindOutline(Nyx::Editor::EditorTransactionContext& context, const Nyx::Editor::ObjectRef& root)
	{
		return context.ActiveScene && root == Nyx::Editor::SceneOutlineTransactionDomain::GetRef() ? &context.ActiveScene->GetOutline() : nullptr;
	}

	bool IsOutlineType(const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return &typeMetadata == &Nyx::Reflection::GetTypeMetadata<Nyx::SceneOutline>();
	}
}

namespace Nyx::Editor
{
	void* SceneOutlineTransactionDomain::ResolveMutable(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return IsOutlineType(typeMetadata) ? FindOutline(context, root) : nullptr;
	}

	// The outline is always there; it is never made or deleted
	bool SceneOutlineTransactionDomain::CreateRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	bool SceneOutlineTransactionDomain::DeleteRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	void SceneOutlineTransactionDomain::EnumerateSubobjects(
		EditorTransactionContext& context,
		const ObjectRef& root,
		std::vector<ReflectedObjectView>& outSubobjects)
	{
		outSubobjects.clear();
		if (Nyx::SceneOutline* outline = FindOutline(context, root))
		{
			outSubobjects.push_back({ &Nyx::Reflection::GetTypeMetadata<Nyx::SceneOutline>(), outline });
		}
	}

	bool SceneOutlineTransactionDomain::EnsureSubobject(
		EditorTransactionContext& context,
		const ObjectRef& root,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return IsOutlineType(typeMetadata) && FindOutline(context, root);
	}

	bool SceneOutlineTransactionDomain::RemoveSubobject(
		EditorTransactionContext& /*context*/,
		const ObjectRef& /*root*/,
		const Nyx::Reflection::TypeMetadata& /*typeMetadata*/)
	{
		return false;
	}
}
