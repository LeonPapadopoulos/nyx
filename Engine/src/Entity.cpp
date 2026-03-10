#include "Enginepch.h"
#include "Entity.h"

#include "Log.h"
namespace Engine
{
	void TestECS()
	{
		using namespace Engine;

		Registry registry;
		Entity player = registry.CreateEntity();
		Entity enemy = registry.CreateEntity();
		
		LOG_INFO("Entity: Generation: {0}; Index: {1}", player.Generation(), player.Index());

		registry.Add<Name>(player, Name{ "Player" });
		registry.Add<Transform>(player, Transform{ 100.0f, 200.0f, 0.0f });

		registry.Add<Transform>(enemy, Transform{ 99.9f, 111.1f, 3.1415f });

		if (registry.Has<Transform>(player))
		{
			auto& transform = registry.Get<Transform>(player);
			transform.X += 10.0f;
			LOG_INFO("Entity: Transform: ( {0}, {1}, {2} )", transform.X, transform.Y, transform.Rotation);
		}

		registry.Each<Transform>([](Entity entity, Transform& transform)
			{
				LOG_INFO("Entity {0}:{1} -> Transform({2}, {3}, {4})",
					entity.Generation(),
					entity.Index(),
					transform.X,
					transform.Y,
					transform.Rotation);
			});

		registry.DestroyEntity(player);
		registry.DestroyEntity(enemy);

		player = registry.CreateEntity();
		enemy = registry.CreateEntity();
		LOG_INFO("Player: Generation: {0}; Index: {1}", player.Generation(), player.Index());
		LOG_INFO("Enemy: Generation: {0}; Index: {1}", enemy.Generation(), enemy.Index());
	}
}

namespace Engine
{
	Registry::~Registry() = default;

	Entity Registry::CreateEntity()
	{
		if (FreeListHead != RegistryInvalidIndex)
		{
			const uint32_t reusedIndex = FreeListHead;
			EntitySlot& slot = EntitySlots[reusedIndex];

			FreeListHead = slot.NextFreeIndex;

			slot.NextFreeIndex = RegistryInvalidIndex;
			slot.bAlive = true;

			return Entity::Make(reusedIndex, slot.Generation);
		}

		const uint32_t newIndex = static_cast<uint32_t>(EntitySlots.size());

		EntitySlot newSlot;
		newSlot.Generation = 1;
		newSlot.NextFreeIndex = RegistryInvalidIndex;
		newSlot.bAlive = true;

		EntitySlots.push_back(newSlot);

		return Entity::Make(newIndex, newSlot.Generation);
	}

	bool Registry::DestroyEntity(Entity entity)
	{
		if (!IsAlive(entity))
		{
			return false;
		}

		for (auto& pair : ComponentPools)
		{
			pair.second->RemoveIfPresent(entity);
		}

		EntitySlot& slot = EntitySlots[entity.Index()];
		slot.bAlive = false;

		++slot.Generation;

		// Keep generation 0 reserved so a zero handle can stay invalid if desired.
		if (slot.Generation == 0)
		{
			++slot.Generation;
		}

		slot.NextFreeIndex = FreeListHead;
		FreeListHead = entity.Index();

		return true;
	}

	bool Registry::IsAlive(Entity entity) const
	{
		if (!entity.IsValid())
		{
			return false;
		}

		const uint32_t index = entity.Index();
		if (index >= EntitySlots.size())
		{
			return false;
		}

		const EntitySlot& slot = EntitySlots[index];
		return slot.bAlive && slot.Generation == entity.Generation();
	}

	void Registry::Clear()
	{
		for (auto& pair : ComponentPools)
		{
			pair.second->Clear();
		}

		ComponentPools.clear();
		EntitySlots.clear();
		FreeListHead = RegistryInvalidIndex;
	}

	void Registry::ValidateEntityAlive(Entity entity) const
	{
		ASSERT(IsAlive(entity) && "Entity is invalid or stale.");
	}
}