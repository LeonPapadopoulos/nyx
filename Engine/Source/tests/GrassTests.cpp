// The grass without a GPU (Grass.h): its values in range; how many blades are drawn at a distance;
// the nested grid and the blades' ranks, so that those left at any distance lie evenly and a tile
// holds every blade drawn where it is; and the component saved in a scene and read back.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "Grass.h"
#include "GrassFieldComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(float a, float b, float tolerance = 1.0e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	void TestSettingsInRange()
	{
		GrassFieldComponent component;
		const GrassSettings defaults = MakeGrassSettings(component);
		Require(Near(defaults.CellSize, 1.0f / std::sqrt(60.0f)), "The grid has one cell for each blade a square metre");
		Require(defaults.MaterialMask == ~0u, "No materials chosen grows on all of them");

		component.Density = 1.0e6f;
		component.Range = 1.0e6f;
		component.FullDensityDistance = 1.0e6f;
		component.SlopeRange = glm::vec2(60.0f, 0.0f);
		component.HeightRange = glm::vec2(5.0f, -5.0f);
		component.TerrainMaterials = { 1, 3, 40 };
		const GrassSettings wild = MakeGrassSettings(component);
		Require(wild.CellSize > 0.07f, "The density has a top");
		Require(wild.Range == 500.0f && wild.FullDensityDistance == 500.0f, "The range has a top, and the full density ends within it");
		Require(Near(wild.NormalYRange.x, 0.5f) && Near(wild.NormalYRange.y, 1.0f), "The slopes in either order give the normal's y from the steepest");
		Require(wild.HeightRange == glm::vec2(-5.0f, 5.0f), "The heights in either order");
		Require(wild.MaterialMask == 0b1010u, "The materials chosen, those past the mask's bits left out");
	}

	void TestDrawnShare()
	{
		const GrassSettings settings = MakeGrassSettings(GrassFieldComponent{});
		Require(GetGrassDrawnShare(settings, 0.0f) == 1.0f && GetGrassDrawnShare(settings, 20.0f) == 1.0f, "All are drawn up to the full density's distance");
		Require(Near(GetGrassDrawnShare(settings, 40.0f), 0.5f), "Past it, the share falls as one over the distance");
		Require(GetGrassDrawnShare(settings, 100.0f) == 0.0f && GetGrassDrawnShare(settings, 200.0f) == 0.0f, "None at the range and past it");
		float last = 2.0f;
		for (float distance = 0.0f; distance < 110.0f; distance += 0.5f)
		{
			const float share = GetGrassDrawnShare(settings, distance);
			Require(share <= last, "The share never rises with the distance, so no blade comes back going away");
			last = share;
		}
	}

	void TestNestedGrid()
	{
		Require(GetGrassCellLevel(1, 0) == 0 && GetGrassCellLevel(2, 4) == 1 && GetGrassCellLevel(4, 8) == 2 && GetGrassCellLevel(8, 16) == 3 && GetGrassCellLevel(0, 0) == 3 && GetGrassCellLevel(-8, 16) == 3,
			"A cell's level is the widest grid it is on");

		// Over a square of the widest grid, each level holds its share of the cells
		uint32_t counts[GrassGridLevels]{};
		for (int32_t row = -64; row < 64; ++row)
		{
			for (int32_t column = -64; column < 64; ++column)
			{
				++counts[GetGrassCellLevel(column, row)];
			}
		}
		const uint32_t cells = 128 * 128;
		Require(counts[0] == cells * 3 / 4 && counts[1] == cells * 3 / 16 && counts[2] == cells * 3 / 64 && counts[3] == cells / 64, "Each level is a quarter of the one before");

		// The ranks cover 0 to 1 evenly: those below a share are as many as that share of the cells,
		// and the level a tile keeps for a share holds every blade drawn at it
		const float shares[] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.0625f, 0.03f, 0.015625f, 0.002f };
		for (const float share : shares)
		{
			const uint32_t level = GetGrassGridLevelForShare(share);
			uint32_t drawn = 0;
			uint32_t next = 1;
			for (int32_t row = 0; row < 512; ++row)
			{
				for (int32_t column = 0; column < 512; ++column)
				{
					next = next * 1664525u + 1013904223u;
					const float random = static_cast<float>(next >> 8) / 16777216.0f;
					const uint32_t cellLevel = GetGrassCellLevel(column, row);
					if (GetGrassRank(cellLevel, random) < share)
					{
						++drawn;
						Require(cellLevel >= level, "A blade drawn at a share is in the tile that keeps the level for it");
					}
				}
			}
			const float expected = share * 512.0f * 512.0f;
			Require(std::abs(static_cast<float>(drawn) - expected) < std::max(expected * 0.05f, 64.0f), "The blades drawn are that share of them");
		}
	}

	void TestSavedAndRead()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		world.Add<GuidComponent>(entity);
		GrassFieldComponent& grass = world.Add<GrassFieldComponent>(entity);
		grass.Seed = 7;
		grass.Density = 33.0f;
		grass.NormalRounding = 0.25f;
		grass.RootToGround = 0.75f;
		grass.TerrainMaterials = { 2, 5 };
		grass.TipColor = glm::vec3(0.1f, 0.2f, 0.3f);

		Nyx::Tests::TestDirectory directory("NyxGrassTests");
		const std::filesystem::path path = directory.Path / "Grass.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<GrassFieldComponent>([&](Entity, const GrassFieldComponent& read)
			{
				bFound = true;
				Require(read.Seed == 7 && read.Density == 33.0f && read.NormalRounding == 0.25f && read.RootToGround == 0.75f, "The numbers aren't read back as saved");
				Require(read.TerrainMaterials.size() == 2 && read.TerrainMaterials[1] == 5, "The materials aren't read back as saved");
				Require(read.TipColor == glm::vec3(0.1f, 0.2f, 0.3f), "The colours aren't read back as saved");
			});
		Require(bFound, "The grass isn't in the loaded scene");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestSettingsInRange();
		TestDrawnShare();
		TestNestedGrid();
		TestSavedAndRead();
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxGrassTests failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxGrassTests passed\n";
	return 0;
}
