// An SDF terrain's occlusion volume (TerrainOcclusion.h) without a GPU: its cells are how solid the
// terrain is at their middles, at every level; moving the viewpoint fills only the cells the windows
// come to, the same as a fill made there from nothing; an edit fills again only what it reaches, the
// same as a fill made with it from nothing; the sky's visibility as the shader works it out is
// nearly all of it over open ground, less down a trench the deeper, little at its bottom, none in a closed
// tunnel, less under a roof than beside it; Sky Occlusion 0 keeps no volume. Prints how long a fill
// from nothing takes.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TerrainOcclusion.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// Flat ground at 0 with, under it, a closed tunnel along x 10 m down, and a trench 12 m wide and
	// 30 m deep, open to the sky, along x farther on; and a roof: a slab 2 m thick held 4 m over the
	// ground on the other side
	SdfTerrainComponent MakeGround()
	{
		SdfTerrainComponent terrain;
		terrain.Layers[0].Height = 0.0f;
		terrain.Layers[0].Amplitude = 0.0f;
		SdfTerrainEdit tunnel;
		tunnel.Shape = ESdfTerrainEditShape::Capsule;
		tunnel.Position = glm::vec3(-40.0f, -10.0f, 20.0f);
		tunnel.End = glm::vec3(40.0f, -10.0f, 20.0f);
		tunnel.Radius = 3.0f;
		AppendTerrainEdit(terrain, tunnel);
		SdfTerrainEdit trench;
		trench.Shape = ESdfTerrainEditShape::Box;
		trench.Position = glm::vec3(0.0f, -15.0f, 50.0f);
		trench.HalfSize = glm::vec3(30.0f, 15.0f, 6.0f);
		AppendTerrainEdit(terrain, trench);
		SdfTerrainEdit roof;
		roof.Shape = ESdfTerrainEditShape::Box;
		roof.Mode = ESdfTerrainEditMode::Add;
		roof.Position = glm::vec3(0.0f, 5.0f, -20.0f);
		roof.HalfSize = glm::vec3(15.0f, 1.0f, 15.0f);
		AppendTerrainEdit(terrain, roof);
		return terrain;
	}

	Entity AddTerrain(Registry& world, const SdfTerrainComponent& terrain)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<SdfTerrainComponent>(entity, terrain);
		return entity;
	}

	// Until a fill asked for has gone in, at most 60 s
	const TerrainOcclusionComponent& Settle(Registry& world, Entity terrain, const glm::vec3& viewpoint)
	{
		const auto start = std::chrono::steady_clock::now();
		const uint32_t fills = world.Has<TerrainOcclusionComponent>(terrain) ? world.Get<TerrainOcclusionComponent>(terrain).Fills : 0;
		for (;;)
		{
			UpdateTerrainOcclusion(world, { &viewpoint, 1 }, JobSystem::Get());
			const TerrainOcclusionComponent& occlusion = world.Get<TerrainOcclusionComponent>(terrain);
			if (occlusion.Fills > fills)
			{
				// What it asked for since going in, if anything
				UpdateTerrainOcclusion(world, { &viewpoint, 1 }, JobSystem::Get());
				if (!occlusion.bFilling)
				{
					return occlusion;
				}
			}
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(60), "The volume should be filled within a minute");
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
	}

	float SolidOf(float value, float cell)
	{
		return std::clamp(0.5f - value / cell, 0.0f, 1.0f);
	}

	// Every cell of every level as a fill from nothing would make it, at the cells' middles
	void RequireAsSampled(const TerrainOcclusionComponent& occlusion, const SdfTerrainComponent& terrain, const std::string& what)
	{
		for (int level = 0; level < TerrainOcclusion::Levels; ++level)
		{
			const TerrainOcclusionLevel& in = occlusion.Levels[size_t(level)];
			// Every 7th cell along each axis, through the whole window
			for (int k = 0; k < TerrainOcclusion::Size; k += 7)
			{
				for (int j = 0; j < TerrainOcclusion::Size; j += 7)
				{
					for (int i = 0; i < TerrainOcclusion::Size; i += 7)
					{
						const glm::ivec3 cell = in.Origin + glm::ivec3(i, j, k);
						const glm::vec3 middle = (glm::vec3(cell) + 0.5f) * in.CellSize;
						const auto wrap = [](int value) { return ((value % TerrainOcclusion::Size) + TerrainOcclusion::Size) % TerrainOcclusion::Size; };
						const size_t index = size_t(wrap(cell.x)) + size_t(wrap(cell.y)) * TerrainOcclusion::Size + size_t(wrap(cell.z)) * TerrainOcclusion::Size * TerrainOcclusion::Size;
						const float expected = SolidOf(SampleTerrainValue(terrain, middle), in.CellSize);
						const float got = float(in.Solid[index]) / 255.0f;
						Require(std::fabs(expected - got) <= 1.0f / 255.0f + 1e-4f,
							what + ": level " + std::to_string(level) + "'s cell at " + Describe(middle) + " should be " + std::to_string(expected) + " solid, not " + std::to_string(got));
					}
				}
			}
		}
	}

	// A fill from nothing: its windows round the viewpoint, the cells as sampled; how long it takes
	void TestAFill()
	{
		Registry world;
		const Entity terrain = AddTerrain(world, MakeGround());
		const glm::vec3 viewpoint(3.0f, 2.0f, 4.0f);
		const auto start = std::chrono::steady_clock::now();
		const TerrainOcclusionComponent& occlusion = Settle(world, terrain, viewpoint);
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		Require(occlusion.bReady, "The volume should be ready");
		for (int level = 0; level < TerrainOcclusion::Levels; ++level)
		{
			const TerrainOcclusionLevel& in = occlusion.Levels[size_t(level)];
			Require(std::fabs(in.CellSize - TerrainOcclusion::FinestCell * float(1 << level)) < 1e-6f, "Each level's cells should be twice the size of the one before's");
			const glm::vec3 low = in.GetWindowMin();
			const glm::vec3 high = in.GetWindowMax();
			Require(glm::all(glm::greaterThan(viewpoint - low, (high - low) * 0.3f)) && glm::all(glm::greaterThan(high - viewpoint, (high - low) * 0.3f)),
				"Each level's window should be round the viewpoint");
		}
		RequireAsSampled(occlusion, world.Get<SdfTerrainComponent>(terrain), "From nothing");
		std::cout << std::fixed << std::setprecision(1) << "A fill from nothing: " << occlusion.LastSampled << " cells sampled of "
				  << TerrainOcclusion::Levels * TerrainOcclusion::Size * TerrainOcclusion::Size * TerrainOcclusion::Size << " in " << occlusion.LastMilliseconds
				  << " ms, ready after " << seconds * 1000.0 << " ms\n";
	}

	// Moving 20 m and 300 m: what it fills is what the windows came to, and the cells are as a fill
	// from nothing there makes them
	void TestMoving()
	{
		Registry world;
		const Entity terrain = AddTerrain(world, MakeGround());
		Settle(world, terrain, glm::vec3(0.0f, 2.0f, 0.0f));
		const TerrainOcclusionComponent& near = Settle(world, terrain, glm::vec3(20.0f, 2.0f, -3.0f));
		Require(near.LastSampled < uint64_t(TerrainOcclusion::Size) * TerrainOcclusion::Size * TerrainOcclusion::Size,
			"Moving 20 m should fill only the cells the windows come to, not " + std::to_string(near.LastSampled));
		RequireAsSampled(near, world.Get<SdfTerrainComponent>(terrain), "Moved 20 m");
		const TerrainOcclusionComponent& far = Settle(world, terrain, glm::vec3(300.0f, 40.0f, 120.0f));
		RequireAsSampled(far, world.Get<SdfTerrainComponent>(terrain), "Moved 300 m");
		std::cout << "Moving 20 m filled " << near.LastSampled << " cells\n";
	}

	// A dig across the trench and a heap on the ground: the cells they reach filled again, as a fill
	// with them from nothing makes them; undone, as before
	void TestEdits()
	{
		Registry world;
		const Entity terrain = AddTerrain(world, MakeGround());
		const glm::vec3 viewpoint(0.0f, 2.0f, 30.0f);
		Settle(world, terrain, viewpoint);
		SdfTerrainComponent& values = world.Get<SdfTerrainComponent>(terrain);
		SdfTerrainEdit dig;
		dig.Position = glm::vec3(5.0f, 0.0f, 35.0f);
		dig.Radius = 6.0f;
		AppendTerrainEdit(values, dig);
		SdfTerrainEdit heap;
		heap.Mode = ESdfTerrainEditMode::Add;
		heap.Position = glm::vec3(-12.0f, 0.0f, 30.0f);
		heap.Radius = 4.0f;
		AppendTerrainEdit(values, heap);
		const TerrainOcclusionComponent& edited = Settle(world, terrain, viewpoint);
		Require(edited.LastSampled > 0 && edited.LastSampled < uint64_t(TerrainOcclusion::Size) * TerrainOcclusion::Size * TerrainOcclusion::Size,
			"Edits should fill again only the cells they reach, not " + std::to_string(edited.LastSampled));
		RequireAsSampled(edited, values, "Edited");
		Require(SampleTerrainOcclusion(edited, 0, glm::vec3(5.0f, -3.0f, 35.0f)) < 0.1f && SampleTerrainOcclusion(edited, 0, glm::vec3(-12.0f, 2.0f, 30.0f)) > 0.9f,
			"The dig should be air in the volume and the heap solid");
		values.Edits.resize(values.Edits.size() - 2);
		RequireAsSampled(Settle(world, terrain, viewpoint), values, "Undone");
		std::cout << "Two edits filled " << edited.LastSampled << " cells again\n";
	}

	// The sky as the shader sees it: open ground nearly all, the trench's bottom little, a closed
	// tunnel none, under the roof less than beside it; Sky Occlusion 0 keeps no volume
	void TestTheSky()
	{
		Registry world;
		const Entity terrain = AddTerrain(world, MakeGround());
		const TerrainOcclusionComponent& occlusion = Settle(world, terrain, glm::vec3(0.0f, 2.0f, 20.0f));
		const glm::vec3 up(0.0f, 1.0f, 0.0f);
		const float open = GetTerrainSkyVisibility(occlusion, glm::vec3(-30.0f, 0.0f, 80.0f), up);
		const float trench = GetTerrainSkyVisibility(occlusion, glm::vec3(0.0f, -30.0f, 50.0f), up);
		const float tunnel = GetTerrainSkyVisibility(occlusion, glm::vec3(0.0f, -13.0f, 20.0f), up);
		const float roofed = GetTerrainSkyVisibility(occlusion, glm::vec3(0.0f, 0.0f, -20.0f), up);
		const float beside = GetTerrainSkyVisibility(occlusion, glm::vec3(0.0f, 0.0f, -45.0f), up);
		const float halfway = GetTerrainSkyVisibility(occlusion, glm::vec3(0.0f, -10.0f, 50.0f), up);
		std::cout << std::fixed << std::setprecision(2) << "The sky seen: open ground " << open << ", beside the roof " << beside << ", under it " << roofed
				  << ", 10 m down a trench " << halfway << ", at its bottom " << trench << ", a closed tunnel " << tunnel << "\n";
		Require(open > 0.9f, "Open ground should see nearly all the sky, not " + std::to_string(open));
		Require(beside > 0.8f && roofed < beside - 0.3f, "Under a roof should see less of the sky than beside it");
		Require(trench < 0.4f && trench < halfway, "A deep trench's bottom should see little of the sky, less than higher up it");
		Require(tunnel < 0.05f, "A closed tunnel should see none of the sky, not " + std::to_string(tunnel));

		world.Get<SdfTerrainComponent>(terrain).SkyOcclusion = 0.0f;
		const glm::vec3 viewpoint(0.0f, 2.0f, 20.0f);
		UpdateTerrainOcclusion(world, { &viewpoint, 1 }, JobSystem::Get());
		Require(!world.Has<TerrainOcclusionComponent>(terrain), "Sky Occlusion 0 should keep no volume");
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestAFill();
		TestMoving();
		TestEdits();
		TestTheSky();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All terrain occlusion tests passed\n";
	return 0;
}
