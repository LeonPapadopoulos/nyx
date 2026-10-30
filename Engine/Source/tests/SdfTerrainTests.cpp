// The SDF terrain's field (SdfTerrain.h): flat ground is its height exactly; batches are the same
// bits as single points; the values are those written down when it was made (golden hashes); it is
// never steeper than its slope bound; over a box it stays within its range, checked against dense
// sampling; rays hit where dense marching finds the surface, and never pass through a thin slab;
// materials follow the layer that made a surface; smooth blends stay within their offset; tunnels
// stay in their band; another seed, another terrain; placed shapes are their distances, turned
// exactly, carved into an arch, within their ranges. Prints how loose the bounds are and how fast it
// samples.
#include "SdfTerrain.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::ESdfTerrainCombine;
	using Nyx::ESdfTerrainLayerKind;
	using Nyx::SdfTerrainComponent;
	using Nyx::SdfTerrainLayer;
	using Nyx::Tests::Require;

	// Numbers spread over [0, 1), from integers only, the same everywhere
	struct Sequence
	{
		uint32_t State = 0x2468ACE1u;

		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) * (1.0f / 16777216.0f);
		}

		float Between(float low, float high) { return low + (high - low) * Next(); }
	};

	uint64_t HashBits(uint64_t hash, uint32_t bits)
	{
		for (int byte = 0; byte < 4; ++byte)
		{
			hash ^= (bits >> (byte * 8)) & 0xFFu;
			hash *= 0x100000001B3ull;
		}
		return hash;
	}

	uint32_t BitsOf(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	SdfTerrainLayer MakeLayer(ESdfTerrainLayerKind kind)
	{
		SdfTerrainLayer layer;
		layer.Kind = kind;
		return layer;
	}

	SdfTerrainComponent MakeFlat(float height)
	{
		SdfTerrainComponent terrain;
		terrain.Layers[0].Height = height;
		terrain.Layers[0].Amplitude = 0.0f;
		return terrain;
	}

	// Every kind, terraces, smooth blends, ridged noise and detail: hills with tunnels under them, and
	// a floating landmass with chambers in it above them
	SdfTerrainComponent MakeRich()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 3;
		terrain.Layers.clear();

		SdfTerrainLayer hills = MakeLayer(ESdfTerrainLayerKind::Heightfield);
		hills.Amplitude = 20.0f;
		hills.Frequency = 0.01f;
		hills.TerraceHeight = 6.0f;
		hills.TerraceSharpness = 0.6f;
		hills.Material = 1;
		terrain.Layers.push_back(hills);

		SdfTerrainLayer ridges = MakeLayer(ESdfTerrainLayerKind::Heightfield);
		ridges.Seed = 2;
		ridges.Height = -5.0f;
		ridges.Amplitude = 30.0f;
		ridges.Frequency = 0.008f;
		ridges.bRidged = true;
		ridges.BlendRadius = 8.0f;
		ridges.Material = 3;
		terrain.Layers.push_back(ridges);

		SdfTerrainLayer island = MakeLayer(ESdfTerrainLayerKind::Landmass);
		island.Height = 150.0f;
		island.Depth = 30.0f;
		island.Amplitude = 6.0f;
		island.Radius = 120.0f;
		island.UndersideDepth = 40.0f;
		island.EdgeRoughness = 25.0f;
		island.Frequency = 0.02f;
		island.Material = 2;
		terrain.Layers.push_back(island);

		SdfTerrainLayer tunnels = MakeLayer(ESdfTerrainLayerKind::Tunnels);
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = 10.0f;
		tunnels.Depth = 70.0f;
		tunnels.Radius = 6.0f;
		tunnels.Frequency = 0.02f;
		tunnels.Octaves = 3;
		tunnels.BlendRadius = 1.0f;
		terrain.Layers.push_back(tunnels);

		SdfTerrainLayer chambers = MakeLayer(ESdfTerrainLayerKind::Chambers);
		chambers.Combine = ESdfTerrainCombine::Carve;
		chambers.Height = 148.0f;
		chambers.Depth = 25.0f;
		chambers.Openness = 0.6f;
		chambers.Frequency = 0.03f;
		chambers.Octaves = 3;
		terrain.Layers.push_back(chambers);

		SdfTerrainLayer detail = MakeLayer(ESdfTerrainLayerKind::Detail);
		detail.Amplitude = 0.5f;
		detail.Frequency = 0.2f;
		detail.Octaves = 3;
		terrain.Layers.push_back(detail);
		return terrain;
	}

	glm::vec3 RandomPoint(Sequence& sequence)
	{
		return { sequence.Between(-250.0f, 250.0f), sequence.Between(-100.0f, 220.0f), sequence.Between(-250.0f, 250.0f) };
	}

	// Flat ground is its height exactly: the value is the height above it, the normal up, the slope
	// bound 1, a ray down hits it, and boxes above and below are all air and all solid
	void TestFlatGround()
	{
		const SdfTerrainComponent terrain = MakeFlat(12.5f);
		Require(Nyx::SampleTerrainValue(terrain, glm::vec3(3.0f, 20.0f, -7.0f)) == 7.5f, "Flat ground's value should be the height above it");
		Require(Nyx::SampleTerrainValue(terrain, glm::vec3(100.0f, 2.5f, 40.0f)) == -10.0f, "Below flat ground the value should be the depth");
		Require(Nyx::GetTerrainMaxSlope(terrain) == 1.0f, "Flat ground's slope bound should be 1");
		const glm::vec3 normal = Nyx::GetTerrainNormal(terrain, glm::vec3(1.0f, 12.5f, 2.0f));
		Require(glm::length(normal - glm::vec3(0.0f, 1.0f, 0.0f)) < 1e-5f, "Flat ground's normal should point up");

		const std::optional<Nyx::TerrainRayHit> hit = Nyx::RayCastTerrain(terrain, glm::vec3(3.0f, 50.0f, 4.0f), glm::vec3(0.2f, -1.0f, 0.1f), 1000.0f);
		Require(hit.has_value() && std::fabs(hit->Point.y - 12.5f) < 0.01f, "A ray down should hit flat ground at its height");
		Require(!Nyx::RayCastTerrain(terrain, glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1000.0f).has_value(),
			"A ray up from above the ground should hit nothing");
		const std::optional<Nyx::TerrainRayHit> inside = Nyx::RayCastTerrain(terrain, glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 10.0f);
		Require(inside.has_value() && inside->Distance == 0.0f, "A ray starting inside should hit at once");

		Require(Nyx::GetTerrainValueRange(terrain, glm::vec3(-10.0f, 13.0f, -10.0f), glm::vec3(10.0f, 30.0f, 10.0f)).IsAllAir(),
			"A box above flat ground should be all air");
		Require(Nyx::GetTerrainValueRange(terrain, glm::vec3(-10.0f, -30.0f, -10.0f), glm::vec3(10.0f, 12.0f, 10.0f)).IsAllSolid(),
			"A box below flat ground should be all solid");
		const Nyx::TerrainValueRange across = Nyx::GetTerrainValueRange(terrain, glm::vec3(-1.0f, 10.0f, -1.0f), glm::vec3(1.0f, 15.0f, 1.0f));
		Require(!across.IsAllAir() && !across.IsAllSolid(), "A box across the surface should be neither");

		SdfTerrainComponent empty;
		empty.Layers.clear();
		Require(Nyx::SampleTerrainValue(empty, glm::vec3(0.0f)) == Nyx::SdfTerrainEmptyValue, "A terrain without layers should be all air");
		Require(Nyx::GetTerrainMaxSlope(empty) == 0.0f, "A terrain without layers shouldn't change");
	}

	// A batch is the same bits as one point at a time, values and materials, for every kind
	void TestBatchSameBits()
	{
		const SdfTerrainComponent terrain = MakeRich();
		Sequence sequence;
		constexpr size_t Count = 5003;
		std::vector<float> x(Count);
		std::vector<float> y(Count);
		std::vector<float> z(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			const glm::vec3 point = RandomPoint(sequence);
			x[i] = point.x;
			y[i] = point.y;
			z[i] = point.z;
		}
		std::vector<float> values(Count);
		std::vector<uint32_t> materials(Count);
		Nyx::SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), Count, values.data(), materials.data());
		for (size_t i = 0; i < Count; ++i)
		{
			const Nyx::TerrainSample single = Nyx::SampleTerrain(terrain, glm::vec3(x[i], y[i], z[i]));
			Require(BitsOf(single.Value) == BitsOf(values[i]) && single.Material == materials[i],
				"Point " + std::to_string(i) + ": a batch should be the same bits as one point");
		}
	}

	// The values as they were when the terrain was written: if this fails, every world made with it
	// changed. On purpose only.
	void TestGoldenValues()
	{
		const SdfTerrainComponent terrain = MakeRich();
		uint64_t hash = 0xCBF29CE484222325ull;
		for (int i = 0; i < 20; ++i)
		{
			for (int j = 0; j < 20; ++j)
			{
				for (int k = 0; k < 20; ++k)
				{
					const glm::vec3 point(float(i) * 24.3f - 240.0f, float(j) * 16.1f - 95.0f, float(k) * 24.7f - 240.0f);
					const Nyx::TerrainSample sample = Nyx::SampleTerrain(terrain, point);
					hash = HashBits(HashBits(hash, BitsOf(sample.Value)), sample.Material);
				}
			}
		}
		constexpr uint64_t Golden = 0x05d99547b5ec4d88ull;
		std::cout << std::hex << "Golden hash: 0x" << hash << std::dec << "\n";
		Require(hash == Golden, "The terrain's values changed");
	}

	// Never steeper than the slope bound, between points up to 2 m apart
	void TestSlopeBound()
	{
		const SdfTerrainComponent terrain = MakeRich();
		const float bound = Nyx::GetTerrainMaxSlope(terrain);
		Sequence sequence;
		float steepest = 0.0f;
		for (int i = 0; i < 20000; ++i)
		{
			const glm::vec3 a = RandomPoint(sequence);
			const glm::vec3 offset(sequence.Between(-1.0f, 1.0f), sequence.Between(-1.0f, 1.0f), sequence.Between(-1.0f, 1.0f));
			const glm::vec3 b = a + offset;
			const float apart = glm::length(b - a);
			if (apart < 1e-3f)
			{
				continue;
			}
			const float change = std::fabs(Nyx::SampleTerrainValue(terrain, b) - Nyx::SampleTerrainValue(terrain, a));
			steepest = std::max(steepest, change / apart);
		}
		Require(steepest <= bound * 1.001f, "The terrain should never be steeper than its bound: " + std::to_string(steepest) + " > " + std::to_string(bound));
		std::cout << "Slope: steepest measured " << steepest << " a metre, bound " << bound << "\n";
	}

	// Every value densely sampled in a box lies within the box's range; a box said to be all air or
	// all solid is; prints how many boxes the range settles and how many could be
	void TestRangesAgainstDenseSampling()
	{
		const SdfTerrainComponent terrain = MakeRich();
		Sequence sequence;
		int settled = 0;
		int uniform = 0;
		constexpr int Boxes = 150;
		constexpr int Steps = 16;
		for (int box = 0; box < Boxes; ++box)
		{
			const glm::vec3 low = RandomPoint(sequence);
			const float size = sequence.Between(1.0f, 16.0f);
			const glm::vec3 high = low + glm::vec3(size);
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(terrain, low, high);

			bool bAnyAir = false;
			bool bAnySolid = false;
			for (int i = 0; i <= Steps; ++i)
			{
				for (int j = 0; j <= Steps; ++j)
				{
					for (int k = 0; k <= Steps; ++k)
					{
						const glm::vec3 point = low + (high - low) * glm::vec3(float(i), float(j), float(k)) / float(Steps);
						const float value = Nyx::SampleTerrainValue(terrain, point);
						Require(value >= range.Lowest && value <= range.Highest,
							"Box " + std::to_string(box) + ": a value " + std::to_string(value) + " outside its range " + std::to_string(range.Lowest) + " to " +
								std::to_string(range.Highest));
						bAnyAir = bAnyAir || value > 0.0f;
						bAnySolid = bAnySolid || value <= 0.0f;
					}
				}
			}
			if (range.IsAllAir() || range.IsAllSolid())
			{
				++settled;
			}
			if (!(bAnyAir && bAnySolid))
			{
				++uniform;
			}
		}
		std::cout << "Ranges: " << settled << " of " << Boxes << " boxes settled as all air or all solid, of " << uniform << " that are\n";
	}

	// Where a ray hits, against marching it densely: no crossing before the hit, the crossing within
	// a centimetre of it
	void TestRaysAgainstDenseMarching()
	{
		const SdfTerrainComponent terrain = MakeRich();
		Sequence sequence;
		int hits = 0;
		for (int ray = 0; ray < 40; ++ray)
		{
			const bool bFromBelow = ray % 2 == 1;
			const glm::vec3 origin(sequence.Between(-150.0f, 150.0f), bFromBelow ? 85.0f : 260.0f, sequence.Between(-150.0f, 150.0f));
			const glm::vec3 direction = glm::normalize(glm::vec3(sequence.Between(-0.5f, 0.5f), bFromBelow ? 1.0f : -1.0f, sequence.Between(-0.5f, 0.5f)));
			constexpr float MaxDistance = 400.0f;
			if (Nyx::SampleTerrainValue(terrain, origin) <= 0.0f)
			{
				continue;
			}
			const std::optional<Nyx::TerrainRayHit> hit = Nyx::RayCastTerrain(terrain, origin, direction, MaxDistance);

			// The first crossing, marching 5 cm at a time, then a millimetre at a time
			std::optional<float> crossing;
			for (float distance = 0.0f; distance <= MaxDistance; distance += 0.05f)
			{
				if (Nyx::SampleTerrainValue(terrain, origin + direction * distance) <= 0.0f)
				{
					float fine = std::max(distance - 0.05f, 0.0f);
					while (Nyx::SampleTerrainValue(terrain, origin + direction * fine) > 0.0f)
					{
						fine += 0.001f;
					}
					crossing = fine;
					break;
				}
			}

			Require(hit.has_value() == crossing.has_value(), "Ray " + std::to_string(ray) + ": the ray and dense marching should agree on a hit");
			if (hit.has_value())
			{
				++hits;
				Require(std::fabs(hit->Distance - *crossing) <= 0.01f,
					"Ray " + std::to_string(ray) + ": the hit should be within a centimetre of the crossing: " + std::to_string(hit->Distance) + " against " +
						std::to_string(*crossing));
				Require(glm::dot(hit->Normal, direction) < 0.0f, "Ray " + std::to_string(ray) + ": the normal should face the ray");
			}
		}
		std::cout << "Rays: " << hits << " of 40 hit, each within a centimetre of dense marching\n";
		Require(hits >= 20, "Every ray from above, and those from below under the landmass, should hit");
	}

	// A slab 2 m thick, hit from above, below and the side, never passed through
	void TestThinSlab()
	{
		SdfTerrainComponent terrain;
		terrain.Layers.clear();
		SdfTerrainLayer slab = MakeLayer(ESdfTerrainLayerKind::Landmass);
		slab.Height = 10.0f;
		slab.Depth = 2.0f;
		slab.Amplitude = 0.0f;
		slab.UndersideDepth = 0.0f;
		slab.EdgeRoughness = 0.0f;
		slab.Rounding = 0.0f;
		slab.Radius = 50.0f;
		terrain.Layers.push_back(slab);

		const auto top = Nyx::RayCastTerrain(terrain, glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1000.0f);
		const auto bottom = Nyx::RayCastTerrain(terrain, glm::vec3(0.0f, -50.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), 1000.0f);
		const auto side = Nyx::RayCastTerrain(terrain, glm::vec3(-200.0f, 9.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f), 1000.0f);
		Require(top.has_value() && std::fabs(top->Point.y - 10.0f) < 0.01f, "A ray down should hit the slab's top");
		Require(bottom.has_value() && std::fabs(bottom->Point.y - 8.0f) < 0.01f, "A ray up should hit the slab's underside");
		Require(side.has_value() && std::fabs(side->Point.x + 50.0f) < 0.01f, "A ray from the side should hit the slab's edge");
		Require(Nyx::SampleTerrainValue(terrain, glm::vec3(0.0f, 9.0f, 0.0f)) < 0.0f, "The slab should be solid inside");
		Require(Nyx::SampleTerrainValue(terrain, glm::vec3(60.0f, 9.0f, 0.0f)) > 0.0f, "Past its edge should be air");
	}

	// The material of the layer that made the surface; carving keeps what it cuts
	void TestMaterials()
	{
		SdfTerrainComponent terrain = MakeFlat(0.0f);
		terrain.Layers[0].Material = 1;
		SdfTerrainLayer island = MakeLayer(ESdfTerrainLayerKind::Landmass);
		island.Height = 100.0f;
		island.Depth = 20.0f;
		island.Amplitude = 0.0f;
		island.UndersideDepth = 0.0f;
		island.EdgeRoughness = 0.0f;
		island.Radius = 50.0f;
		island.Material = 2;
		terrain.Layers.push_back(island);
		SdfTerrainLayer shaft = MakeLayer(ESdfTerrainLayerKind::Chambers);
		shaft.Combine = ESdfTerrainCombine::Carve;
		shaft.Height = 0.0f;
		shaft.Depth = 10.0f;
		shaft.Openness = 3.0f;
		shaft.Material = 7;
		terrain.Layers.push_back(shaft);

		Require(Nyx::SampleTerrain(terrain, glm::vec3(300.0f, -30.0f, 0.0f)).Material == 1, "Deep ground should be the ground's material");
		Require(Nyx::SampleTerrain(terrain, glm::vec3(0.0f, 90.0f, 0.0f)).Material == 2, "Inside the island should be its material");
		const Nyx::TerrainSample carved = Nyx::SampleTerrain(terrain, glm::vec3(10.0f, -5.0f, 10.0f));
		Require(carved.Value > 0.0f && carved.Material == 1, "A carved place should be air and keep the ground's material");
	}

	// Two grounds blended: never above the lower, at most a quarter of the radius below it
	void TestSmoothBlend()
	{
		SdfTerrainComponent terrain = MakeFlat(0.0f);
		SdfTerrainLayer second = terrain.Layers[0];
		second.Height = 3.0f;
		second.BlendRadius = 8.0f;
		terrain.Layers.push_back(second);
		for (float y = -20.0f; y <= 20.0f; y += 0.5f)
		{
			const float plain = std::min(y - 0.0f, y - 3.0f);
			const float blended = Nyx::SampleTerrainValue(terrain, glm::vec3(1.0f, y, 2.0f));
			Require(blended <= plain && blended >= plain - 2.0f, "A smooth blend should be within a quarter of its radius below the plain one");
		}
	}

	// Tunnels open air only within their band
	void TestTunnelsStayInTheirBand()
	{
		SdfTerrainComponent terrain = MakeFlat(100.0f);
		SdfTerrainLayer tunnels = MakeLayer(ESdfTerrainLayerKind::Tunnels);
		tunnels.Combine = ESdfTerrainCombine::Carve;
		tunnels.Height = 50.0f;
		tunnels.Depth = 30.0f;
		tunnels.Radius = 8.0f;
		tunnels.Frequency = 0.02f;
		tunnels.Octaves = 2;
		terrain.Layers.push_back(tunnels);

		Sequence sequence;
		int open = 0;
		for (int i = 0; i < 20000; ++i)
		{
			const glm::vec3 point(sequence.Between(-300.0f, 300.0f), sequence.Between(0.0f, 99.0f), sequence.Between(-300.0f, 300.0f));
			if (Nyx::SampleTerrainValue(terrain, point) > 0.0f)
			{
				Require(point.y >= 20.0f && point.y <= 50.0f, "Tunnels should open air only within their band");
				++open;
			}
		}
		Require(open > 100, "Tunnels should open some air in their band");
	}

	// Another seed for the terrain, another terrain
	void TestSeeds()
	{
		SdfTerrainComponent a = MakeRich();
		SdfTerrainComponent b = a;
		b.Seed = a.Seed + 1;
		Sequence sequence;
		int differ = 0;
		for (int i = 0; i < 200; ++i)
		{
			const glm::vec3 point = RandomPoint(sequence);
			if (Nyx::SampleTerrainValue(a, point) != Nyx::SampleTerrainValue(b, point))
			{
				++differ;
			}
		}
		Require(differ > 150, "Another seed should give another terrain");
	}

	// Not a test: a chunk's samples near the surface, in a batch
	void PrintTimings()
	{
		const SdfTerrainComponent terrain = MakeRich();
		constexpr int Side = 35;
		std::vector<float> x;
		std::vector<float> y;
		std::vector<float> z;
		for (int i = 0; i < Side; ++i)
		{
			for (int j = 0; j < Side; ++j)
			{
				for (int k = 0; k < Side; ++k)
				{
					x.push_back(float(k) + 40.0f);
					y.push_back(float(j) - 20.0f);
					z.push_back(float(i) - 70.0f);
				}
			}
		}
		std::vector<float> values(x.size());
		std::vector<uint32_t> materials(x.size());
		const auto start = std::chrono::steady_clock::now();
		Nyx::SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), x.size(), values.data(), materials.data());
		const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		std::cout << "A chunk's " << x.size() << " samples of every kind of layer: " << milliseconds << " ms, " << milliseconds * 1e6 / double(x.size())
				  << " ns a sample\n";
	}

	// Placed shapes: a ball, a rod and a box, turned and rounded, their values the distances to them
	// (negative inside); a box turned a quarter is the box with its sides swapped (to the rounding of
	// adding its sides' squares in another order); a
	// wall carved by a rod is an arch; within their ranges and slope bound against dense sampling
	void TestShapes()
	{
		SdfTerrainComponent terrain;
		terrain.Layers.clear();
		SdfTerrainLayer ball = MakeLayer(ESdfTerrainLayerKind::Sphere);
		ball.Position = glm::vec3(10.0f, 5.0f, 0.0f);
		ball.Radius = 3.0f;
		terrain.Layers.push_back(ball);
		Require(std::fabs(Nyx::SampleTerrainValue(terrain, ball.Position) + 3.0f) < 1e-6f, "A ball's middle should be its radius inside");
		Require(std::fabs(Nyx::SampleTerrainValue(terrain, ball.Position + glm::vec3(0.0f, 7.0f, 0.0f)) - 4.0f) < 1e-5f, "4 m off a ball should be 4");

		SdfTerrainComponent rodOnly;
		rodOnly.Layers.clear();
		SdfTerrainLayer rod = MakeLayer(ESdfTerrainLayerKind::Capsule);
		rod.Position = glm::vec3(0.0f, 0.0f, 0.0f);
		rod.End = glm::vec3(0.0f, 0.0f, 10.0f);
		rod.Radius = 1.0f;
		rodOnly.Layers.push_back(rod);
		Require(std::fabs(Nyx::SampleTerrainValue(rodOnly, glm::vec3(3.0f, 0.0f, 5.0f)) - 2.0f) < 1e-5f, "3 m from a rod's middle should be 2 off it");
		Require(std::fabs(Nyx::SampleTerrainValue(rodOnly, glm::vec3(0.0f, 0.0f, 13.0f)) - 2.0f) < 1e-5f, "3 m past a rod's end should be 2 off it");

		SdfTerrainComponent boxOnly;
		boxOnly.Layers.clear();
		SdfTerrainLayer box = MakeLayer(ESdfTerrainLayerKind::Box);
		box.Position = glm::vec3(0.0f, 2.0f, 0.0f);
		box.HalfSize = glm::vec3(4.0f, 2.0f, 1.0f);
		box.Rounding = 0.0f;
		boxOnly.Layers.push_back(box);
		Require(std::fabs(Nyx::SampleTerrainValue(boxOnly, glm::vec3(6.0f, 2.0f, 0.0f)) - 2.0f) < 1e-6f, "2 m off a box's side should be 2");
		Require(std::fabs(Nyx::SampleTerrainValue(boxOnly, glm::vec3(0.0f, 2.0f, 0.0f)) + 1.0f) < 1e-6f, "A box's middle should be its least half size inside");
		// Turned 30 degrees about Y: 2 m off its side along its own X
		boxOnly.Layers[0].Rotation = glm::vec3(0.0f, 30.0f, 0.0f);
		const glm::vec3 alongX(0.8660254f, 0.0f, -0.5f);
		Require(std::fabs(Nyx::SampleTerrainValue(boxOnly, box.Position + alongX * 6.0f) - 2.0f) < 1e-4f, "Turned, 2 m off its side should still be 2");
		// A quarter turn: its sides swapped
		boxOnly.Layers[0].Rotation = glm::vec3(0.0f, 90.0f, 0.0f);
		SdfTerrainComponent swapped = boxOnly;
		swapped.Layers[0].Rotation = glm::vec3(0.0f);
		swapped.Layers[0].HalfSize = glm::vec3(1.0f, 2.0f, 4.0f);
		Sequence sequence;
		for (int i = 0; i < 1000; ++i)
		{
			const glm::vec3 point(sequence.Between(-8.0f, 8.0f), sequence.Between(-4.0f, 8.0f), sequence.Between(-8.0f, 8.0f));
			Require(std::fabs(Nyx::SampleTerrainValue(boxOnly, point) - Nyx::SampleTerrainValue(swapped, point)) < 1e-5f, "A box turned a quarter should be its sides swapped");
		}

		// An arch: a wall carved by a rod through it, open under the rod, solid above
		SdfTerrainComponent arch;
		arch.Layers.clear();
		SdfTerrainLayer wall = MakeLayer(ESdfTerrainLayerKind::Box);
		wall.Position = glm::vec3(0.0f, 4.0f, 0.0f);
		wall.HalfSize = glm::vec3(6.0f, 4.0f, 1.0f);
		wall.Rounding = 0.25f;
		arch.Layers.push_back(wall);
		SdfTerrainLayer opening = MakeLayer(ESdfTerrainLayerKind::Capsule);
		opening.Combine = ESdfTerrainCombine::Carve;
		opening.Position = glm::vec3(0.0f, 2.0f, -3.0f);
		opening.End = glm::vec3(0.0f, 2.0f, 3.0f);
		opening.Radius = 2.0f;
		opening.Material = 1;
		arch.Layers.push_back(opening);
		Require(Nyx::SampleTerrainValue(arch, glm::vec3(0.0f, 2.0f, 0.0f)) > 0.0f, "An arch should be open under it");
		Require(Nyx::SampleTerrainValue(arch, glm::vec3(0.0f, 6.0f, 0.0f)) < 0.0f && Nyx::SampleTerrainValue(arch, glm::vec3(4.5f, 2.0f, 0.0f)) < 0.0f,
			"An arch should be solid above and beside its opening");

		// Ranges and the slope bound, shapes blended into hills
		SdfTerrainComponent mixed = MakeFlat(0.0f);
		SdfTerrainLayer turned = wall;
		turned.Rotation = glm::vec3(10.0f, 35.0f, -20.0f);
		turned.BlendRadius = 1.0f;
		mixed.Layers.push_back(turned);
		mixed.Layers.push_back(opening);
		SdfTerrainLayer pillar = rod;
		pillar.End = glm::vec3(5.0f, 9.0f, 3.0f);
		mixed.Layers.push_back(pillar);
		const float bound = Nyx::GetTerrainMaxSlope(mixed);
		float steepest = 0.0f;
		for (int box = 0; box < 60; ++box)
		{
			const glm::vec3 low(sequence.Between(-10.0f, 8.0f), sequence.Between(-4.0f, 8.0f), sequence.Between(-10.0f, 8.0f));
			const glm::vec3 high = low + glm::vec3(sequence.Between(0.5f, 6.0f));
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(mixed, low, high);
			for (int n = 0; n < 512; ++n)
			{
				const glm::vec3 point = low + (high - low) * glm::vec3(float(n % 8), float((n / 8) % 8), float(n / 64)) / 7.0f;
				const float value = Nyx::SampleTerrainValue(mixed, point);
				Require(value >= range.Lowest && value <= range.Highest, "A shape's value should be within its box's range");
				const glm::vec3 near = point + glm::vec3(0.05f, -0.03f, 0.02f);
				steepest = std::max(steepest, std::fabs(Nyx::SampleTerrainValue(mixed, near) - value) / glm::length(near - point));
			}
		}
		Require(steepest <= bound * 1.001f, "Shapes should never be steeper than the bound");
		std::cout << "Shapes: steepest " << steepest << " a metre, bound " << bound << "\n";
	}
}

int main()
{
	try
	{
		TestFlatGround();
		TestBatchSameBits();
		TestGoldenValues();
		TestSlopeBound();
		TestRangesAgainstDenseSampling();
		TestRaysAgainstDenseMarching();
		TestThinSlab();
		TestMaterials();
		TestSmoothBlend();
		TestTunnelsStayInTheirBand();
		TestSeeds();
		TestShapes();
		PrintTimings();
	}
	catch (const std::exception& error)
	{
		std::cerr << "SDF terrain test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All SDF terrain tests passed.\n";
	return 0;
}
