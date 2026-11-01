// Batch seed tests of an SDF terrain's world, offline (TerrainSeedTests.h): many seeds made and
// checked one after another on the workers, then a report of how many passed, what failed most, how
// the landforms' shares spread, the worst chunk at the places and how long each part took. The
// terrain from a scene (its first with a Plan layer, its Place Rules and Route Checks), or Gate 3's
// test region with none.
//
//   NyxTerrainSeeds [--scene <file>] [--first <seed>] [--count <seeds>] [--out <report.md>] [--no-checks]
#include "ComponentRegistration.h"
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "SdfTerrainComponent.h"
#include "TerrainGate3Region.h"
#include "TerrainSeedTests.h"
#include "TerrainWorldPlan.h"

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

int main(int argc, char** argv)
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		std::optional<std::filesystem::path> scene;
		uint32_t first = 1;
		uint32_t count = 32;
		std::filesystem::path out = "TerrainSeeds.md";
		Nyx::TerrainSeedTestOptions options;
		for (int index = 1; index < argc; ++index)
		{
			const bool bValue = index + 1 < argc;
			if (std::strcmp(argv[index], "--scene") == 0 && bValue)
			{
				scene = argv[++index];
			}
			else if (std::strcmp(argv[index], "--first") == 0 && bValue)
			{
				first = uint32_t(std::stoul(argv[++index]));
			}
			else if (std::strcmp(argv[index], "--count") == 0 && bValue)
			{
				count = uint32_t(std::stoul(argv[++index]));
			}
			else if (std::strcmp(argv[index], "--out") == 0 && bValue)
			{
				out = argv[++index];
			}
			else if (std::strcmp(argv[index], "--no-checks") == 0)
			{
				options.bChecks = false;
			}
			else
			{
				std::cout << "NyxTerrainSeeds [--scene <file>] [--first <seed>] [--count <seeds>] [--out <report.md>] [--no-checks]\n";
				return 1;
			}
		}

		Nyx::SdfTerrainComponent terrain = Nyx::TerrainGate3::MakeRegion();
		if (scene)
		{
			Nyx::Registry world;
			if (!Nyx::SceneSerializer::LoadFromFile(*scene, world))
			{
				std::cerr << "Can't load " << scene->string() << "\n";
				return 1;
			}
			bool bFound = false;
			world.Each<Nyx::SdfTerrainComponent>([&](Nyx::Entity, Nyx::SdfTerrainComponent& found)
				{
					if (!bFound && Nyx::UsesTerrainWorldPlan(found))
					{
						terrain = found;
						bFound = true;
					}
				});
			if (!bFound)
			{
				std::cerr << scene->string() << " has no SDF terrain with a Plan layer\n";
				return 1;
			}
		}

		std::vector<Nyx::TerrainSeedTest> tests;
		for (uint32_t index = 0; index < count; ++index)
		{
			tests.push_back(Nyx::TestTerrainSeed(terrain, first + index, options, &Nyx::JobSystem::Get()));
			const Nyx::TerrainSeedTest& test = tests.back();
			std::cout << "Seed " << test.Seed << ": " << (!test.bChecked ? "not checked" : (test.bPassed ? "passed" : "failed")) << " in " << test.Milliseconds << " ms\n";
			for (const std::string& failure : test.Failures)
			{
				std::cout << "  " << failure << "\n";
			}
		}
		const Nyx::TerrainSeedSummary summary = Nyx::SummariseTerrainSeeds(tests);
		std::ofstream file(out, std::ios::binary);
		file << Nyx::WriteTerrainSeedReport(terrain, tests, summary);
		std::cout << summary.Passed << " of " << summary.Checked << " checked seeds passed; the report is " << std::filesystem::absolute(out).string() << "\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
}
