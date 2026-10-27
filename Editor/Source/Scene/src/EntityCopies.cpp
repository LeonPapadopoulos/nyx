#include "EntityCopies.h"

#include "BinaryArchive.h"
#include "GuidComponent.h"
#include "Log.h"
#include "NameComponent.h"
#include "ReflectedArchiveSerializer.h"
#include "RootObjectSnapshotUtils.h"
#include "SceneDocument.h"
#include "SceneSerializer.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"

#include <cctype>
#include <set>

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

		Nyx::BinaryWriter writer;
		if (!Nyx::SceneSerializer::WriteEntity(world, entity, writer))
		{
			LOG_WARNING("Copy: the entity can't be copied, since a component can't be written");
			return {};
		}

		return writer.GetBytes();
	}

	std::optional<Nyx::Entity> PasteEntity(SceneDocument& scene, TransactionSystem& transactions, ITransactionDomain& domain,
		EditorTransactionContext& context, const std::vector<std::byte>& copied, const char* label,
		const std::function<void(Nyx::Entity pasted, Transaction& step)>& alsoDo)
	{
		if (copied.empty())
		{
			return std::nullopt;
		}

		Nyx::Registry& world = scene.GetRegistry();
		const Nyx::Entity entity = world.CreateEntity();

		// Assets are loaded by ComponentPostLoadSubscriber when the paste is recorded below
		Nyx::BinaryReader reader;
		reader.LoadFromMemory(copied);
		Nyx::ScenePostLoadContext noAssets{};
		Nyx::ReadWarnings warnings;
		const bool bRead = Nyx::SceneSerializer::ReadEntity(reader, world, entity, noAssets, warnings) && reader.IsValid();
		warnings.Log("Paste");

		if (!bRead)
		{
			world.DestroyEntity(entity);
			LOG_ERROR("Paste: the copied entity can't be read");
			return std::nullopt;
		}

		// A guid of its own, and a name that tells it apart. The copy normally has both already.
		if (!world.Has<Nyx::GuidComponent>(entity))
		{
			world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{});
		}
		world.Get<Nyx::GuidComponent>(entity).Guid = Nyx::EntityGuid::Generate();

		if (!world.Has<Nyx::NameComponent>(entity))
		{
			world.Add<Nyx::NameComponent>(entity, Nyx::NameComponent{ "Entity" });
		}
		Nyx::NameComponent& name = world.Get<Nyx::NameComponent>(entity);
		name.Name = MakeCopyName(scene, name.Name.empty() ? std::string("Entity") : name.Name);

		const ObjectRef target = MakeSceneEntityRef(scene, entity);

		Transaction transaction;
		transaction.Label = label;
		transaction.Changes.push_back(Change{ EChangeKind::AddObject,
			AddObjectChange{ .Target = target, .AfterCreate = CaptureRootObjectSnapshot(domain, context, target) } });
		if (alsoDo)
		{
			alsoDo(entity, transaction);
		}
		transactions.Push(std::move(transaction));

		return entity;
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
