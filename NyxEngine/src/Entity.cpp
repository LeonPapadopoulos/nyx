#include "NyxEnginePCH.h"
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
}