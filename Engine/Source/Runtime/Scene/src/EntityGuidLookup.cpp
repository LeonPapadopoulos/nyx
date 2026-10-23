#include "NyxPCH.h"
#include "EntityGuidLookup.h"

#include "GuidComponent.h"

#include <algorithm>
#include <bit>

namespace Nyx
{
	namespace
	{
		// Never fewer slots, so a small world can grow a while before the table needs more memory
		constexpr size_t MinimumSlots = 64;

		// The slot a guid's search starts at. Generated guids are random, but tests and hand-written
		// scenes use small ones too: a multiply spreads those over the table rather than into one run
		// of slots.
		size_t FirstSlot(EntityGuid guid, size_t slotCount)
		{
			return static_cast<size_t>((guid.Value * 0x9E3779B97F4A7C15ull) >> 32) & (slotCount - 1);
		}
	}

	EntityGuidLookup MakeEntityGuidLookup(const Registry& world)
	{
		EntityGuidLookup lookup;
		RefreshEntityGuidLookup(world, lookup);
		return lookup;
	}

	void RefreshEntityGuidLookup(const Registry& world, EntityGuidLookup& lookup)
	{
		size_t guidCount = 0;
		world.Each<GuidComponent>([&](Entity, const GuidComponent&)
			{
				++guidCount;
			});

		// At most half full; assign() keeps the vector's memory when it has enough
		const size_t slotCount = std::max(MinimumSlots, std::bit_ceil(guidCount * 2));
		lookup.Slots.assign(slotCount, EntityGuidLookup::Slot{});

		// The same order as FindEntityByGuid(), and the first of a guid keeps its slot
		world.Each<GuidComponent>([&](Entity entity, const GuidComponent& component)
			{
				if (!component.Guid.IsValid())
				{
					return;
				}

				for (size_t index = FirstSlot(component.Guid, slotCount);; index = (index + 1) & (slotCount - 1))
				{
					EntityGuidLookup::Slot& slot = lookup.Slots[index];
					if (!slot.Guid.IsValid())
					{
						slot = EntityGuidLookup::Slot{ component.Guid, entity };
						return;
					}
					if (slot.Guid == component.Guid)
					{
						return;
					}
				}
			});
	}

	std::optional<Entity> FindEntityByGuid(const Registry& world, const EntityGuidLookup& lookup, EntityGuid guid)
	{
		const size_t slotCount = lookup.Slots.size();
		if (!guid.IsValid() || slotCount == 0)
		{
			return std::nullopt;
		}

		// Free slots are left, so the search ends
		for (size_t index = FirstSlot(guid, slotCount);; index = (index + 1) & (slotCount - 1))
		{
			const EntityGuidLookup::Slot& slot = lookup.Slots[index];
			if (!slot.Guid.IsValid())
			{
				return std::nullopt;
			}
			if (slot.Guid == guid)
			{
				return world.IsAlive(slot.Owner) ? std::optional<Entity>(slot.Owner) : std::nullopt;
			}
		}
	}
}
