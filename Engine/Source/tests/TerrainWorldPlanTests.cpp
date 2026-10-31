// The world plan (TerrainWorldPlan.h): the same seed and plan make the same bits, another seed
// another world; each kind of ground takes about its share; pins steer it; rifts open the edge;
// rivers run down to an outlet, their beds never rising; lakes fill their basins; a box's bounds
// hold every blended value in it, and the slopes every change. Prints how long a world of 2 and of
// 20 km takes. --maps <folder> writes maps of a few seeds and plans as PNGs.
#include "Log.h"
#include "TerrainWorldPlan.h"
#include "TerrainWorldPlanMap.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::ESdfTerrainLandform;
	using Nyx::SdfTerrainPlanPin;
	using Nyx::SdfTerrainWorldPlan;
	using Nyx::TerrainWorldPlan;
	using Nyx::Tests::Require;

	float Spread(uint32_t n)
	{
		n ^= n >> 16;
		n *= 0x7FEB352Du;
		n ^= n >> 15;
		n *= 0x846CA68Bu;
		n ^= n >> 16;
		return float(n >> 8) / 16777216.0f;
	}

	// The share of the landmass's nodes of one kind
	float ShareOf(const TerrainWorldPlan& plan, ESdfTerrainLandform landform, bool bAboveSea = false)
	{
		size_t land = 0;
		size_t of = 0;
		for (const uint8_t at : plan.Landform)
		{
			if (at == TerrainWorldPlan::NotLand || (bAboveSea && at == uint8_t(ESdfTerrainLandform::Sea)))
			{
				continue;
			}
			++land;
			of += at == uint8_t(landform) ? 1 : 0;
		}
		return land > 0 ? float(of) / float(land) : 0.0f;
	}

	void TestSameBits()
	{
		const SdfTerrainWorldPlan made;
		const auto a = Nyx::MakeTerrainWorldPlan(5, made);
		const auto b = Nyx::MakeTerrainWorldPlan(5, made);
		const auto other = Nyx::MakeTerrainWorldPlan(6, made);
		using Grid = std::vector<float> TerrainWorldPlan::*;
		for (const Grid grid : { &TerrainWorldPlan::Height, &TerrainWorldPlan::PlainsAmplitude, &TerrainWorldPlan::RoughAmplitude, &TerrainWorldPlan::Rift,
				 &TerrainWorldPlan::Channel, &TerrainWorldPlan::ChannelBed, &TerrainWorldPlan::Tunnels })
		{
			Require(std::memcmp(((*a).*grid).data(), ((*b).*grid).data(), ((*a).*grid).size() * sizeof(float)) == 0, "The same seed and plan should make the same bits");
		}
		Require(a->Landform == b->Landform && a->Rivers.size() == b->Rivers.size() && a->Lakes.size() == b->Lakes.size(), "The same seed should make the same rivers and lakes");
		Require(a->Landform != other->Landform, "Another seed should make another world");
	}

	void TestShares()
	{
		for (const float sea : { 0.1f, 0.3f })
		{
			for (const float rough : { 0.2f, 0.6f })
			{
				SdfTerrainWorldPlan made;
				made.Sea = sea;
				made.Roughland = rough;
				made.Rifts = 0;
				const auto plan = Nyx::MakeTerrainWorldPlan(3, made);
				const float seaShare = ShareOf(*plan, ESdfTerrainLandform::Sea);
				const float roughShare = ShareOf(*plan, ESdfTerrainLandform::Roughland, true);
				std::cout << "Sea " << sea << ", rough land " << rough << ": " << seaShare << " of the land under the sea, " << roughShare << " of the rest rough\n";
				Require(std::fabs(seaShare - sea) < 0.08f, "The sea should take about its share");
				Require(std::fabs(roughShare - rough) < 0.1f, "Rough land should take about its share");
			}
		}
		SdfTerrainWorldPlan none;
		none.Sea = 0.0f;
		none.Roughland = 0.0f;
		const auto flat = Nyx::MakeTerrainWorldPlan(3, none);
		Require(ShareOf(*flat, ESdfTerrainLandform::Sea) == 0.0f && ShareOf(*flat, ESdfTerrainLandform::Roughland) == 0.0f, "None of a kind should make none");
	}

	ESdfTerrainLandform LandformAt(const TerrainWorldPlan& plan, const glm::vec2& point)
	{
		const glm::ivec2 node = plan.NodeOf(point);
		return ESdfTerrainLandform(plan.Landform[plan.IndexOf(node.x, node.y)]);
	}

	void TestPins()
	{
		SdfTerrainWorldPlan made;
		made.Rifts = 0;
		const glm::vec2 inland(-150.0f, 120.0f);
		for (const ESdfTerrainLandform landform : { ESdfTerrainLandform::Sea, ESdfTerrainLandform::Roughland, ESdfTerrainLandform::Plains })
		{
			for (uint32_t seed = 1; seed <= 4; ++seed)
			{
				made.Pins = { SdfTerrainPlanPin{ .Landform = landform, .Position = inland, .Radius = 200.0f, .Strength = 1.0f } };
				const auto plan = Nyx::MakeTerrainWorldPlan(seed, made);
				Require(LandformAt(*plan, inland) == landform, "A pin should make its kind of ground at its middle");
			}
		}

		made.Pins = { SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Rift, .Position = glm::vec2(200.0f, -100.0f) } };
		const auto rifted = Nyx::MakeTerrainWorldPlan(2, made);
		Require(rifted->At(rifted->Rift, 200.0f, -100.0f) < 0.0f, "A Rift pin should have a rift pass it");

		made.Pins = { SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Tunnels, .Position = inland, .Radius = 150.0f } };
		made.Tunnels = 0.0f;
		const auto tunnelled = Nyx::MakeTerrainWorldPlan(2, made);
		Require(tunnelled->At(tunnelled->Tunnels, inland.x, inland.y) > 0.5f, "A Tunnels pin should open tunnels round it");
		Require(tunnelled->At(tunnelled->Tunnels, -inland.x, -inland.y) == 0.0f, "No tunnels should be made away from it with none asked for");
	}

	void TestRifts()
	{
		for (uint32_t seed = 1; seed <= 6; ++seed)
		{
			SdfTerrainWorldPlan made;
			made.Rifts = 3;
			const auto plan = Nyx::MakeTerrainWorldPlan(seed, made);
			// Each rift's mouth is off the landmass: rift nodes next to nodes off it
			size_t mouths = 0;
			size_t inside = 0;
			for (int j = 1; j + 1 < plan->Nodes; ++j)
			{
				for (int i = 1; i + 1 < plan->Nodes; ++i)
				{
					if (plan->Landform[plan->IndexOf(i, j)] == TerrainWorldPlan::NotLand || plan->Rift[plan->IndexOf(i, j)] >= 0.0f)
					{
						continue;
					}
					++inside;
					const bool bByEdge = plan->Landform[plan->IndexOf(i + 1, j)] == TerrainWorldPlan::NotLand || plan->Landform[plan->IndexOf(i - 1, j)] == TerrainWorldPlan::NotLand
						|| plan->Landform[plan->IndexOf(i, j + 1)] == TerrainWorldPlan::NotLand || plan->Landform[plan->IndexOf(i, j - 1)] == TerrainWorldPlan::NotLand;
					mouths += bByEdge ? 1 : 0;
				}
			}
			Require(inside > 200, "Three rifts should cut through the land");
			Require(mouths >= 3, "Every rift should open the landmass's edge");
		}
	}

	void TestWater()
	{
		size_t lakes = 0;
		for (uint32_t seed = 1; seed <= 6; ++seed)
		{
			SdfTerrainWorldPlan made;
			made.Rivers = 3;
			made.Lakes = 2;
			const auto plan = Nyx::MakeTerrainWorldPlan(seed, made);
			Require(!plan->Rivers.empty(), "There should be rivers");
			for (const TerrainWorldPlan::River& river : plan->Rivers)
			{
				Require(river.Points.size() == river.Beds.size() && river.Points.size() > 8, "A river should have a course");
				for (size_t n = 1; n < river.Beds.size(); ++n)
				{
					Require(river.Beds[n] <= river.Beds[n - 1], "A river's bed should never rise downstream");
				}
				const glm::vec2 mouth = river.Points.back();
				const glm::ivec2 node = plan->NodeOf(mouth);
				const size_t index = plan->IndexOf(node.x, node.y);
				const bool bOutlet = plan->Landform[index] == TerrainWorldPlan::NotLand || plan->Landform[index] == uint8_t(ESdfTerrainLandform::Sea)
					|| plan->Rift[index] < 0.0f;
				Require(bOutlet || river.End == TerrainWorldPlan::River::EEnd::River, "A river should end in the sea, a rift, over the edge or in another river");
				Require(plan->At(plan->Channel, river.Points[river.Points.size() / 2].x, river.Points[river.Points.size() / 2].y) < 0.0f, "A river's bed should be cut along it");
			}
			Require(!plan->Lakes.empty() && plan->Lakes.size() <= made.Lakes, "Up to two lakes should be dug, one at least");
			lakes += plan->Lakes.size();
			for (const TerrainWorldPlan::Lake& lake : plan->Lakes)
			{
				Require(lake.Level > plan->At(plan->Height, lake.Center.x, lake.Center.y) + 1.0f, "A lake should hold water in its basin");
			}
		}
		std::cout << lakes << " lakes of 12 asked for hold water\n";
	}

	// Every blended value in a box within the box's bounds; every change between two points within
	// the slope times how far apart they are
	void TestBoundsAndSlopes()
	{
		SdfTerrainWorldPlan made;
		made.Pins = { SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Tunnels, .Position = glm::vec2(100.0f, 0.0f) } };
		const auto plan = Nyx::MakeTerrainWorldPlan(9, made);
		uint32_t n = 0;
		for (int box = 0; box < 400; ++box)
		{
			const glm::vec2 corner(Spread(n++) * 2400.0f - 1200.0f, Spread(n++) * 2400.0f - 1200.0f);
			const float size = 4.0f + Spread(n++) * (box % 10 == 0 ? 600.0f : 60.0f);
			const TerrainWorldPlan::Bounds range = plan->GetRange(corner, corner + size);
			for (int sample = 0; sample < 40; ++sample)
			{
				const glm::vec2 point = corner + glm::vec2(Spread(n++), Spread(n++)) * size;
				const float height = plan->At(plan->Height, point.x, point.y);
				const float rift = plan->At(plan->Rift, point.x, point.y);
				const float channel = plan->At(plan->Channel, point.x, point.y);
				const float bed = plan->At(plan->ChannelBed, point.x, point.y);
				Require(height >= range.LowHeight && height <= range.HighHeight, "A height should be within its box's bounds");
				Require(rift >= range.LowRift && rift <= range.HighRift, "A rift's distance should be within its box's bounds");
				Require(channel >= range.LowChannel && channel <= range.HighChannel && bed >= range.LowBed && bed <= range.HighBed, "A river's should be");
				Require(plan->At(plan->PlainsAmplitude, point.x, point.y) <= range.HighPlains && plan->At(plan->RoughAmplitude, point.x, point.y) <= range.HighRough
						&& plan->At(plan->Tunnels, point.x, point.y) <= range.HighTunnels,
					"The amplitudes and tunnels should be within their box's bounds");
			}
		}
		for (int pair = 0; pair < 20000; ++pair)
		{
			const glm::vec2 a(Spread(n++) * 2400.0f - 1200.0f, Spread(n++) * 2400.0f - 1200.0f);
			const glm::vec2 b = a + glm::vec2(Spread(n++) - 0.5f, Spread(n++) - 0.5f) * 20.0f;
			const float apart = glm::length(b - a);
			auto check = [&](const std::vector<float>& grid, float slope)
			{
				Require(std::fabs(plan->At(grid, a.x, a.y) - plan->At(grid, b.x, b.y)) <= slope * apart + 1e-4f, "A grid should change no faster than its slope");
			};
			check(plan->Height, plan->HeightSlope);
			check(plan->PlainsAmplitude, plan->PlainsSlope);
			check(plan->RoughAmplitude, plan->RoughSlope);
			check(plan->Rift, plan->RiftSlope);
			check(plan->Channel, plan->ChannelSlope);
			check(plan->ChannelBed, plan->BedSlope);
			check(plan->Tunnels, plan->TunnelsSlope);
		}
		std::cout << "Slopes: height " << plan->HeightSlope << ", rift " << plan->RiftSlope << ", channel " << plan->ChannelSlope << ", bed " << plan->BedSlope << "\n";
	}

	void PrintTimings()
	{
		for (const float size : { 2048.0f, 20000.0f })
		{
			SdfTerrainWorldPlan made;
			made.Size = size;
			made.Rifts = size > 10000.0f ? 12 : 2;
			made.Rivers = size > 10000.0f ? 30 : 3;
			made.Lakes = size > 10000.0f ? 20 : 2;
			const auto start = std::chrono::steady_clock::now();
			const auto plan = Nyx::MakeTerrainWorldPlan(1, made);
			const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
			std::cout << "A plan " << size << " m wide (" << plan->Nodes << " x " << plan->Nodes << " nodes, " << plan->Rivers.size() << " rivers, " << plan->Lakes.size()
					  << " lakes): " << milliseconds << " ms\n";
		}
	}

	// Maps of a few seeds with the default plan, and of plans with more of one kind
	void WriteMaps(const std::filesystem::path& folder)
	{
		struct Variant
		{
			std::string Name;
			SdfTerrainWorldPlan Made;
		};
		std::vector<Variant> variants;
		for (uint32_t seed = 1; seed <= 4; ++seed)
		{
			variants.push_back({ "default-seed" + std::to_string(seed), {} });
		}
		SdfTerrainWorldPlan seaward;
		seaward.Sea = 0.45f;
		variants.push_back({ "more-sea", seaward });
		SdfTerrainWorldPlan rough;
		rough.Roughland = 0.7f;
		rough.Rifts = 4;
		variants.push_back({ "rough-and-rifted", rough });
		SdfTerrainWorldPlan pinned;
		pinned.Pins = { SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Sea, .Position = glm::vec2(0.0f, 0.0f), .Radius = 300.0f },
			SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Rift, .Position = glm::vec2(-350.0f, 300.0f) },
			SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Roughland, .Position = glm::vec2(400.0f, -350.0f), .Radius = 350.0f } };
		variants.push_back({ "pinned-inner-sea", pinned });
		for (size_t n = 0; n < variants.size(); ++n)
		{
			const uint32_t seed = n < 4 ? uint32_t(n + 1) : 1;
			const auto plan = Nyx::MakeTerrainWorldPlan(seed, variants[n].Made);
			std::string error;
			const std::filesystem::path path = folder / (variants[n].Name + ".png");
			Require(Nyx::SavePng(path, Nyx::Tests::DrawTerrainWorldPlanMap(*plan), error), "The map can't be written: " + error);
			std::cout << "Wrote " << path.string() << "\n";
		}
	}
}

int main(int argc, char** argv)
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		if (argc > 2 && std::strcmp(argv[1], "--maps") == 0)
		{
			WriteMaps(argv[2]);
			return 0;
		}
		TestSameBits();
		TestShares();
		TestPins();
		TestRifts();
		TestWater();
		TestBoundsAndSlopes();
		PrintTimings();
	}
	catch (const std::exception& error)
	{
		std::cerr << "World plan test failed: " << error.what() << "\n";
		return 1;
	}
	std::cout << "World plan tests passed\n";
	return 0;
}
