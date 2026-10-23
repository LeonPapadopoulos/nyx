// How long the lookup from guid to entity takes to make with 3000 guids, as a game world makes it
// each step, and to look every guid up through. Not a test (timing depends on the machine); run it in
// Release.
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "Log.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

namespace
{
	struct Timings
	{
		double Average = 0.0;
		double Median = 0.0;
		double Worst = 0.0;
	};

	// Microseconds a call of `run` takes, over many calls
	template<typename Func>
	Timings Measure(int calls, Func&& run)
	{
		std::vector<double> times;
		times.reserve(calls);
		for (int call = 0; call < calls; ++call)
		{
			const auto start = std::chrono::steady_clock::now();
			run();
			times.push_back(std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count());
		}

		Timings timings;
		for (const double time : times)
		{
			timings.Average += time;
		}
		timings.Average /= calls;
		std::sort(times.begin(), times.end());
		timings.Median = times[times.size() / 2];
		timings.Worst = times.back();
		return timings;
	}

	void Print(const char* what, const Timings& timings)
	{
		std::cout << what << ": " << timings.Average << " us on average, " << timings.Median << " us median, " << timings.Worst
				  << " us at most\n";
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	using namespace Nyx;

	constexpr int Guids = 3000;
	constexpr int Calls = 1000;

	Registry world;
	std::vector<EntityGuid> guids;
	for (int i = 0; i < Guids; ++i)
	{
		const Entity entity = world.CreateEntity();
		guids.push_back(EntityGuid::Generate());
		world.Add<GuidComponent>(entity, GuidComponent{ guids.back() });
	}

	std::cout << Guids << " guids, " << Calls << " calls each\n";

	const auto make = [&]
	{
		MakeEntityGuidLookup(world);
	};
	Print("MakeEntityGuidLookup()", Measure(Calls, make));

	EntityGuidLookup lookup = MakeEntityGuidLookup(world);
	const auto refresh = [&]
	{
		RefreshEntityGuidLookup(world, lookup);
	};
	Print("RefreshEntityGuidLookup()", Measure(Calls, refresh));

	size_t found = 0;
	const auto findAll = [&]
	{
		for (const EntityGuid guid : guids)
		{
			found += FindEntityByGuid(world, lookup, guid).has_value();
		}
	};
	Print("Every guid looked up", Measure(Calls, findAll));

	return found == size_t(Guids) * Calls ? 0 : 1;
}
