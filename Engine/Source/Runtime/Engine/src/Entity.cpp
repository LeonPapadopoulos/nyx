#include "NyxPCH.h"
#include "Entity.h"

#include "Log.h"

namespace Nyx
{
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

				// Important:
				// Reusing a dead slot for a *new* entity advances generation.
				++slot.Generation;

				return Entity::Make(reusedIndex, slot.Generation);
			}

			const uint32_t newIndex = static_cast<uint32_t>(EntitySlots.size());

			// We reserve 0 as indicator for an invalid state
			EntitySlot newSlot;
			newSlot.Generation = 1;
			newSlot.NextFreeIndex = RegistryInvalidIndex;
			newSlot.bAlive = true;

			EntitySlots.push_back(newSlot);

			return Entity::Make(newIndex, newSlot.Generation);
		}

		bool Registry::RestoreEntity(Entity entity)
		{
			const uint32_t index = entity.Index();

			if (index >= EntitySlots.size())
			{
				return false;
			}

			EntitySlot& slot = EntitySlots[index];

			// Must currently be dead.
			if (slot.bAlive)
			{
				return false;
			}

			// Must still match the exact generation we want to restore.
			// If the slot was already reused by a new entity and then destroyed again,
			// the generation will differ and this restore should fail.
			if (slot.Generation != entity.Generation())
			{
				return false;
			}

			// Remove the slot from the free list so it becomes active again.
			if (!RemoveFromFreeList(index))
			{
				return false;
			}

			slot.NextFreeIndex = RegistryInvalidIndex;
			slot.bAlive = true;

			return true;
		}

		bool Registry::DestroyEntity(Entity entity)
		{
			if (!IsAlive(entity))
			{
				return false;
			}

			const uint32_t index = entity.Index();
			EntitySlot& slot = EntitySlots[index];

			// Remove all components on that entity.
			for (auto& [type, pool] : ComponentPools)
			{
				pool->RemoveIfPresent(entity);
			}

			slot.bAlive = false;
			slot.NextFreeIndex = FreeListHead;
			FreeListHead = index;

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

		bool Registry::RemoveFromFreeList(uint32_t index)
		{
			if (FreeListHead == RegistryInvalidIndex)
			{
				return false;
			}

			if (FreeListHead == index)
			{
				FreeListHead = EntitySlots[index].NextFreeIndex;
				return true;
			}

			uint32_t current = FreeListHead;
			while (current != RegistryInvalidIndex)
			{
				const uint32_t next = EntitySlots[current].NextFreeIndex;
				if (next == index)
				{
					EntitySlots[current].NextFreeIndex = EntitySlots[index].NextFreeIndex;
					return true;
				}

				current = next;
			}

			return false;
		}
	}
}