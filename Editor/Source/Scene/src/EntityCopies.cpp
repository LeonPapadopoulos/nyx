#include "EntityCopies.h"

#include "BinaryArchive.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "ParentComponent.h"
#include "Prefab.h"
#include "ReflectedArchiveSerializer.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "SceneSerializer.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransformParents.h"

#include <cctype>
#include <cstdint>
#include <set>
#include <unordered_map>

namespace
{
	// "Cube (3)" is "Cube" with number 3; "Cube" has none
	std::string StripCopyNumber(const std::string& name)
	{
		if (name.size() < 4 || name.back() != ')')
		{
			return name;
		}

		const size_t open = name.rfind(" (");
		if (open == std::string::npos || open + 2 >= name.size() - 1)
		{
			return name;
		}

		for (size_t i = open + 2; i < name.size() - 1; ++i)
		{
			if (!std::isdigit(static_cast<unsigned char>(name[i])))
			{
				return name;
			}
		}

		return name.substr(0, open);
	}

	// Makes the entities of one copy, the first with a name of its own; none if it can't be read
	std::vector<Nyx::Entity> ReadCopy(Nyx::Editor::SceneDocument& scene, const std::vector<std::byte>& copied)
	{
		if (copied.empty())
		{
			return {};
		}

		Nyx::Registry& world = scene.GetRegistry();
		Nyx::BinaryReader reader;
		reader.LoadFromMemory(copied);
		uint32_t count = 0;
		if (!reader.ReadUInt32(count) || count == 0)
		{
			LOG_ERROR("Paste: the copied entity can't be read");
			return {};
		}

		// Assets are loaded by ComponentPostLoadSubscriber when the paste is recorded below
		std::vector<Nyx::Entity> pasted;
		Nyx::ScenePostLoadContext noAssets{};
		Nyx::ReadWarnings warnings;
		bool bRead = true;
		for (uint32_t index = 0; index < count && bRead; ++index)
		{
			const Nyx::Entity entity = world.CreateEntity();
			pasted.push_back(entity);
			bRead = Nyx::SceneSerializer::ReadEntity(reader, world, entity, noAssets, warnings) && reader.IsValid();
		}
		warnings.Log("Paste");

		if (!bRead)
		{
			for (const Nyx::Entity entity : pasted)
			{
				world.DestroyEntity(entity);
			}
			LOG_ERROR("Paste: the copied entity can't be read");
			return {};
		}

		// Guids of their own: a guid names exactly one entity. The copies' children name their
		// copied parents; the first keeps the parent the original has.
		std::unordered_map<Nyx::EntityGuid, Nyx::EntityGuid> copies;
		for (const Nyx::Entity entity : pasted)
		{
			if (!world.Has<Nyx::GuidComponent>(entity))
			{
				world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{});
			}
			Nyx::EntityGuid& guid = world.Get<Nyx::GuidComponent>(entity).Guid;
			const Nyx::EntityGuid original = guid;
			guid = Nyx::EntityGuid::Generate();

			// A part of a copied instance: a part of the copy, its guid made from the copy's root's.
			// Copied without its instance: no part, as its instance would read it into its own.
			if (world.Has<Nyx::PrefabPartComponent>(entity))
			{
				Nyx::PrefabPartComponent& part = world.Get<Nyx::PrefabPartComponent>(entity);
				if (const auto instance = copies.find(part.Instance); instance != copies.end())
				{
					part.Instance = instance->second;
					guid = Nyx::GetPrefabPartGuid(part.Instance, part.Local);
				}
				else
				{
					world.Remove<Nyx::PrefabPartComponent>(entity);
				}
			}
			if (original.IsValid())
			{
				copies[original] = guid;
			}
		}
		for (size_t index = 1; index < pasted.size(); ++index)
		{
			if (world.Has<Nyx::ParentComponent>(pasted[index]))
			{
				Nyx::ParentComponent& link = world.Get<Nyx::ParentComponent>(pasted[index]);
				if (const auto copy = copies.find(link.Parent); copy != copies.end())
				{
					link.Parent = copy->second;
				}
			}
		}

		// A name that tells the copy apart; its children keep theirs
		const Nyx::Entity root = pasted.front();
		for (const Nyx::Entity entity : pasted)
		{
			if (!world.Has<Nyx::NameComponent>(entity))
			{
				world.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ "Entity" });
			}
		}
		Nyx::NameComponent& name = world.Get<Nyx::NameComponent>(root);
		name.Name = Nyx::Editor::MakeCopyName(scene, name.Name.empty() ? std::string("Entity") : name.Name);

		return pasted;
	}
}

namespace Nyx::Editor
{
	std::vector<std::byte> CopyEntity(const SceneDocument& scene, Nyx::Entity entity)
	{
		const Nyx::Registry& world = scene.GetRegistry();
		if (!world.IsAlive(entity))
		{
			return {};
		}

		// The entity, then everything below it, each parent before its children
		const std::vector<Nyx::Entity> entities = GetSelfAndBelow(world, entity);
		Nyx::BinaryWriter writer;
		writer.WriteUInt32(static_cast<uint32_t>(entities.size()));
		for (const Nyx::Entity copied : entities)
		{
			if (!Nyx::SceneSerializer::WriteEntity(world, copied, writer))
			{
				LOG_WARNING("Copy: the entity can't be copied, since a component can't be written");
				return {};
			}
		}

		return writer.GetBytes();
	}

	std::optional<Nyx::Entity> PasteEntity(SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::byte>& copied, const char* label,
		const std::function<void(Nyx::Entity pasted, Transaction& step)>& alsoDo)
	{
		const std::vector<Nyx::Entity> pasted = PasteEntities(scene, transactions, domain, context, { copied }, label, alsoDo);
		return pasted.empty() ? std::nullopt : std::optional<Nyx::Entity>(pasted.front());
	}

	std::vector<Nyx::Entity> PasteEntities(SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::vector<std::byte>>& copies, const char* label,
		const std::function<void(Nyx::Entity pasted, Transaction& step)>& alsoDo)
	{
		std::vector<Nyx::Entity> roots;
		Transaction transaction;
		transaction.Label = label;
		for (const std::vector<std::byte>& copied : copies)
		{
			const std::vector<Nyx::Entity> pasted = ReadCopy(scene, copied);
			if (pasted.empty())
			{
				continue;
			}
			for (const Nyx::Entity entity : pasted)
			{
				const ObjectRef target = MakeSceneEntityRef(scene, entity);
				transaction.Changes.push_back(Change{ EChangeKind::AddObject,
					AddObjectChange{ .Target = target, .AfterCreate = CaptureRootObjectSnapshot(domain, context, target) } });
			}
			if (alsoDo)
			{
				alsoDo(pasted.front(), transaction);
			}
			roots.push_back(pasted.front());
		}
		if (!roots.empty())
		{
			transactions.Push(std::move(transaction));
		}
		return roots;
	}

	std::string MakeCopyName(const SceneDocument& scene, const std::string& name)
	{
		std::set<std::string> taken;
		scene.GetRegistry().Each<Nyx::NameComponent>(
			[&](Nyx::Entity, const Nyx::NameComponent& other)
			{
				taken.insert(other.Name);
			});

		const std::string baseName = StripCopyNumber(name);
		for (int number = 2;; ++number)
		{
			std::string candidate = baseName + " (" + std::to_string(number) + ")";
			if (!taken.contains(candidate))
			{
				return candidate;
			}
		}
	}
}
