#include "PhysicsMaterialDocument.h"

#include "Log.h"
#include "Paths.h"
#include "TransactionSystem.h"

#include <algorithm>
#include <variant>

namespace Nyx::Editor
{
	namespace
	{
		bool Touches(const Transaction& transaction, const ObjectRef& ref)
		{
			return std::any_of(transaction.Changes.begin(), transaction.Changes.end(),
				[&ref](const Change& change)
				{
					const ObjectRef target = std::visit(
						[](const auto& payload)
						{
							return payload.Target;
						},
						change.Payload);
					return target == ref;
				});
		}
	}

	void PhysicsMaterialDocument::Attach(TransactionSystem& transactions)
	{
		transactions.RegisterDomain(EObjectDomain::PhysicsMaterial, this);
		transactions.Subscribe(this);
	}

	bool PhysicsMaterialDocument::Open(const std::vector<std::filesystem::path>& assetsFolders, std::string path, std::string& outError)
	{
		const std::filesystem::path file = Nyx::Paths::FindAssetDirIn(assetsFolders, path) / path;
		Nyx::PhysicsMaterialAsset asset;
		if (!asset.Load(file, outError))
		{
			return false;
		}

		std::vector<std::string> problems;
		Data = asset.Read(&problems);
		for (const std::string& problem : problems)
		{
			LOG_WARNING("Physics material '{0}': {1}", path, problem);
		}
		Asset = std::move(asset);
		Path = std::move(path);
		File = file;
		++OpenCount;
		// Another object: what was sent for the last one doesn't count for this one
		++Revision;
		SavedRevision = Revision;
		return true;
	}

	void PhysicsMaterialDocument::Close()
	{
		Path.clear();
		File.clear();
		Asset = {};
		Data = {};
		++Revision;
		SavedRevision = Revision;
	}

	bool PhysicsMaterialDocument::Save(std::string& outError)
	{
		if (!IsOpen() || !Asset.Save(File, outError))
		{
			return false;
		}
		SavedRevision = Revision;
		LOG_INFO("Saved physics material '{0}'", Path);
		return true;
	}

	void* PhysicsMaterialDocument::ResolveMutable(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return IsMine(root) && &typeMetadata == &Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>() ? &Data : nullptr;
	}

	bool PhysicsMaterialDocument::CreateRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	bool PhysicsMaterialDocument::DeleteRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	void PhysicsMaterialDocument::EnumerateSubobjects(
		EditorTransactionContext& /*context*/, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects)
	{
		if (IsMine(root))
		{
			outSubobjects.push_back({ &Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>(), &Data });
		}
	}

	bool PhysicsMaterialDocument::EnsureSubobject(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return IsMine(root) && &typeMetadata == &Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>();
	}

	bool PhysicsMaterialDocument::RemoveSubobject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/, const Nyx::Reflection::TypeMetadata& /*typeMetadata*/)
	{
		return false;
	}

	void PhysicsMaterialDocument::OnTransactionCommitted(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void PhysicsMaterialDocument::OnTransactionApplied(const Transaction& transaction, bool /*bWasUndo*/)
	{
		OnChanged(transaction);
	}

	void PhysicsMaterialDocument::OnTransactionPreviewed(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void PhysicsMaterialDocument::OnChanged(const Transaction& transaction)
	{
		if (IsOpen() && Touches(transaction, GetRef()))
		{
			Asset.Write(Data);
			++Revision;
		}
	}
}
