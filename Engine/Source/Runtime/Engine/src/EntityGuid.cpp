#include "NyxPCH.h"
#include "EntityGuid.h"

#include <random>

namespace Nyx::Engine
{
	EntityGuid EntityGuid::Generate()
	{
		// One generator per thread, seeded with 128 bits from the operating system, so separate runs
		// of the editor and the game don't produce the same guids.
		thread_local std::mt19937_64 generator = []
		{
			std::random_device device;
			std::seed_seq seed{ device(), device(), device(), device() };
			return std::mt19937_64(seed);
		}();

		EntityGuid guid;
		while (!guid.IsValid())
		{
			guid.Value = generator();
		}

		return guid;
	}
}
