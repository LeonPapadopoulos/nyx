// The SDF terrain's field (SdfTerrain.h): flat ground is its height exactly; batches are the same
// bits as single points; the values are those written down when it was made (golden hashes); it is
// never steeper than its slope bound; over a box it stays within its range, checked against dense
// sampling; rays hit where dense marching finds the surface, and never pass through a thin slab;
// materials follow the layer that made a surface; smooth blends stay within their offset; tunnels
// stay in their band; another seed, another terrain; placed shapes are their distances, turned
// exactly, carved into an arch, within their ranges. Prints how loose the bounds are and how fast it
// samples.
#include "FieldMesher.h"
#include "Log.h"
#include "BinaryArchive.h"
#include "SdfTerrain.h"
#include "TerrainSave.h"
#include "TerrainWorldPlan.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
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

	// Edits on the rich terrain near its surface: digs, heaps and paint of every shape, some blended
	SdfTerrainComponent MakeEdited()
	{
		SdfTerrainComponent terrain = MakeRich();
		Sequence sequence;
		for (int i = 0; i < 60; ++i)
		{
			Nyx::SdfTerrainEdit edit;
			edit.Shape = static_cast<Nyx::ESdfTerrainEditShape>(i % 3);
			edit.Mode = static_cast<Nyx::ESdfTerrainEditMode>((i / 3) % 3);
			edit.Position = glm::vec3(sequence.Between(-40.0f, 40.0f), sequence.Between(-15.0f, 25.0f), sequence.Between(-40.0f, 40.0f));
			edit.End = edit.Position + glm::vec3(sequence.Between(-6.0f, 6.0f), sequence.Between(-3.0f, 3.0f), sequence.Between(-6.0f, 6.0f));
			edit.HalfSize = glm::vec3(sequence.Between(0.5f, 4.0f), sequence.Between(0.5f, 4.0f), sequence.Between(0.5f, 4.0f));
			edit.Rotation = glm::vec3(sequence.Between(-40.0f, 40.0f), sequence.Between(0.0f, 360.0f), sequence.Between(-40.0f, 40.0f));
			edit.Radius = sequence.Between(0.5f, 5.0f);
			edit.BlendRadius = (i % 4 == 0) ? sequence.Between(0.2f, 2.0f) : 0.0f;
			edit.Material = 4 + uint32_t(i % 3);
			Nyx::AppendTerrainEdit(terrain, edit);
		}
		return terrain;
	}

	glm::vec3 PointNearEdits(Sequence& sequence)
	{
		return { sequence.Between(-50.0f, 50.0f), sequence.Between(-25.0f, 35.0f), sequence.Between(-50.0f, 50.0f) };
	}

	// Edits: a dig is air where it was solid, a heap solid where it was air and of its material, paint
	// the material alone; revisions count up; a batch the same bits as single points; boxes' ranges
	// and the slope bound hold over them; a terrain left only the edits reaching a box is the same bits
	// in that box, and an edit far away reaches nothing
	void TestEdits()
	{
		SdfTerrainComponent flat = MakeFlat(0.0f);
		Require(Nyx::GetTerrainRevision(flat) == 0, "A terrain without edits should be revision 0");
		Nyx::SdfTerrainEdit dig;
		dig.Position = glm::vec3(5.0f, 0.0f, 5.0f);
		dig.Radius = 3.0f;
		Require(Nyx::AppendTerrainEdit(flat, dig) == 1, "The first edit should be revision 1");
		Require(std::fabs(Nyx::SampleTerrainValue(flat, dig.Position) - 3.0f) < 1e-6f, "A dig's middle should be its radius out of the ground");
		Require(Nyx::SampleTerrainValue(flat, glm::vec3(5.0f, -2.0f, 5.0f)) > 0.0f, "Inside a dig should be air");
		Require(Nyx::SampleTerrainValue(flat, glm::vec3(20.0f, -2.0f, 5.0f)) == -2.0f, "Away from a dig the ground should be as it was");

		Nyx::SdfTerrainEdit heap;
		heap.Shape = Nyx::ESdfTerrainEditShape::Box;
		heap.Mode = Nyx::ESdfTerrainEditMode::Add;
		heap.Position = glm::vec3(-10.0f, 3.0f, 0.0f);
		heap.HalfSize = glm::vec3(2.0f, 3.0f, 2.0f);
		heap.Material = 7;
		Require(Nyx::AppendTerrainEdit(flat, heap) == 2 && Nyx::GetTerrainRevision(flat) == 2, "The second edit should be revision 2");
		const Nyx::TerrainSample onHeap = Nyx::SampleTerrain(flat, glm::vec3(-10.0f, 4.0f, 0.0f));
		Require(onHeap.Value < 0.0f && onHeap.Material == 7, "Inside a heap should be solid, of its material");

		Nyx::SdfTerrainEdit paint;
		paint.Shape = Nyx::ESdfTerrainEditShape::Capsule;
		paint.Mode = Nyx::ESdfTerrainEditMode::Paint;
		paint.Position = glm::vec3(30.0f, 0.0f, -5.0f);
		paint.End = glm::vec3(30.0f, 0.0f, 5.0f);
		paint.Radius = 2.0f;
		paint.Material = 9;
		Nyx::AppendTerrainEdit(flat, paint);
		const Nyx::TerrainSample painted = Nyx::SampleTerrain(flat, glm::vec3(30.0f, -1.0f, 0.0f));
		Require(painted.Value == -1.0f && painted.Material == 9, "Paint should change the material alone");
		Require(Nyx::SampleTerrain(flat, glm::vec3(30.0f, 3.0f, 0.0f)).Value == 3.0f, "Paint shouldn't add ground");

		const SdfTerrainComponent terrain = MakeEdited();
		Require(Nyx::GetTerrainRevision(terrain) == 60, "60 edits should be revision 60");
		Sequence sequence;
		constexpr size_t Count = 4099;
		std::vector<float> x(Count);
		std::vector<float> y(Count);
		std::vector<float> z(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			const glm::vec3 point = PointNearEdits(sequence);
			x[i] = point.x;
			y[i] = point.y;
			z[i] = point.z;
		}
		std::vector<float> values(Count);
		std::vector<uint32_t> materials(Count);
		Nyx::SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), Count, values.data(), materials.data());
		int differFromUnedited = 0;
		const SdfTerrainComponent unedited = MakeRich();
		for (size_t i = 0; i < Count; ++i)
		{
			const Nyx::TerrainSample single = Nyx::SampleTerrain(terrain, glm::vec3(x[i], y[i], z[i]));
			Require(BitsOf(single.Value) == BitsOf(values[i]) && single.Material == materials[i], "An edited batch should be the same bits as single points");
			if (Nyx::SampleTerrain(unedited, glm::vec3(x[i], y[i], z[i])).Value != single.Value)
			{
				++differFromUnedited;
			}
		}
		Require(differFromUnedited > 100, "Edits should change the terrain round them");

		const float bound = Nyx::GetTerrainMaxSlope(terrain);
		float steepest = 0.0f;
		for (int box = 0; box < 80; ++box)
		{
			const glm::vec3 low = PointNearEdits(sequence);
			const glm::vec3 high = low + glm::vec3(sequence.Between(0.5f, 12.0f));
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(terrain, low, high);
			for (int n = 0; n < 512; ++n)
			{
				const glm::vec3 point = low + (high - low) * glm::vec3(float(n % 8), float((n / 8) % 8), float(n / 64)) / 7.0f;
				const float value = Nyx::SampleTerrainValue(terrain, point);
				Require(value >= range.Lowest && value <= range.Highest, "An edited value should be within its box's range");
				const glm::vec3 near = point + glm::vec3(0.05f, -0.03f, 0.02f);
				steepest = std::max(steepest, std::fabs(Nyx::SampleTerrainValue(terrain, near) - value) / glm::length(near - point));
			}
		}
		Require(steepest <= bound * 1.001f, "Edits should never be steeper than the bound");

		// Chunk-sized boxes: the edits reaching one, kept alone, sample the same bits in it
		size_t keptMost = 0;
		size_t keptLeast = terrain.Edits.size();
		for (int box = 0; box < 40; ++box)
		{
			const glm::vec3 origin = PointNearEdits(sequence) - glm::vec3(8.0f);
			constexpr float CellSize = 0.5f;
			const glm::ivec3 size(33);
			const glm::vec3 boxMax = origin + glm::vec3(size - 1) * CellSize;
			SdfTerrainComponent kept = terrain;
			Nyx::KeepTerrainEditsReaching(kept, origin, boxMax);
			const Nyx::TerrainEditsReach asked = Nyx::GetTerrainEditsReaching(terrain, origin, boxMax);
			Require(asked.Count == kept.Edits.size(), "Asking which edits reach a box should be what keeping them keeps");
			Require(asked.Revision == (kept.Edits.empty() ? 0 : kept.Edits.back().Revision), "The reach's revision should be its latest edit's");
			keptMost = std::max(keptMost, kept.Edits.size());
			keptLeast = std::min(keptLeast, kept.Edits.size());
			const Nyx::FieldGrid all = Nyx::SampleTerrainGrid(terrain, origin, CellSize, size);
			const Nyx::FieldGrid some = Nyx::SampleTerrainGrid(kept, origin, CellSize, size);
			for (size_t i = 0; i < all.Values.size(); ++i)
			{
				Require(BitsOf(all.Values[i]) == BitsOf(some.Values[i]) && all.Materials[i] == some.Materials[i],
					"A terrain left the edits reaching a box should be the same bits in it");
			}
		}
		Require(keptMost < terrain.Edits.size() && keptLeast < keptMost, "A box should keep only some of the edits");
		std::cout << "Edits: a 16 m box keeps " << keptLeast << " to " << keptMost << " of " << terrain.Edits.size() << "\n";

		Nyx::SdfTerrainEdit far = dig;
		far.Position = glm::vec3(1000.0f, 0.0f, 0.0f);
		SdfTerrainComponent withFar = terrain;
		Nyx::AppendTerrainEdit(withFar, far);
		Require(Nyx::GetTerrainEditsReaching(withFar, glm::vec3(-10.0f), glm::vec3(10.0f)).Hash == Nyx::GetTerrainEditsReaching(terrain, glm::vec3(-10.0f), glm::vec3(10.0f)).Hash,
			"An edit far away shouldn't reach a box");
		Require(Nyx::ExtendsTerrainEdits(withFar, terrain) && !Nyx::ExtendsTerrainEdits(terrain, withFar), "An edit added should extend the edits");
		SdfTerrainComponent undone = withFar;
		undone.Edits.pop_back();
		Nyx::SdfTerrainEdit other = far;
		other.Position.x += 1.0f;
		Nyx::AppendTerrainEdit(undone, other);
		Require(!Nyx::ExtendsTerrainEdits(undone, withFar) && undone.Edits.back().Revision == withFar.Edits.back().Revision,
			"An edit undone and another made in its place shouldn't extend the edits, of the same revision");

		// A box carried across edits added one at a time is the box taken over them all at once
		SdfTerrainComponent growing = MakeRich();
		std::vector<Nyx::TerrainBoxEdits> boxes;
		for (int box = 0; box < 20; ++box)
		{
			const glm::vec3 low = PointNearEdits(sequence);
			boxes.push_back(Nyx::BeginTerrainBoxEdits(growing, low, low + glm::vec3(sequence.Between(2.0f, 30.0f))));
		}
		for (const Nyx::SdfTerrainEdit& edit : terrain.Edits)
		{
			growing.Edits.push_back(edit);
			for (Nyx::TerrainBoxEdits& box : boxes)
			{
				Nyx::ContinueTerrainBoxEdits(growing, box);
			}
		}
		for (const Nyx::TerrainBoxEdits& box : boxes)
		{
			const Nyx::TerrainEditsReach all = Nyx::GetTerrainEditsReaching(terrain, box.BoxMin, box.BoxMax);
			Require(box.Seen == terrain.Edits.size() && box.Reach.Hash == all.Hash && box.Reach.Count == all.Count, "A box carried across edits should be the box taken at once");
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(terrain, box.BoxMin, box.BoxMax);
			for (int n = 0; n < 125; ++n)
			{
				const glm::vec3 point = box.BoxMin + (box.BoxMax - box.BoxMin) * glm::vec3(float(n % 5), float((n / 5) % 5), float(n / 25)) / 4.0f;
				const float value = Nyx::SampleTerrainValue(terrain, point);
				Require(value >= box.Range.Lowest && value <= box.Range.Highest, "A box's range carried across edits should hold its values");
			}
		}
	}

	// Bakes: on the corners of the bricks' cells the same bits as without them, between them the
	// corners blended, and edits after a bake over it; many points at once, and a terrain left the
	// edits reaching a box, the same bits as single points; the slope bound and boxes' ranges hold;
	// bakes without their values the same bits, and made again the same; made again for other layers;
	// undone with the edits; due after Bake After edits near a brick, and never with 0
	void TestBakes()
	{
		const SdfTerrainComponent plain = MakeEdited();
		SdfTerrainComponent baked = plain;
		Sequence sequence;
		std::vector<glm::ivec3> bricks;
		for (int z = -2; z <= 1; ++z)
		{
			for (int y = -1; y <= 1; ++y)
			{
				for (int x = -2; x <= 1; ++x)
				{
					bricks.push_back({ x, y, z });
				}
			}
		}
		for (const glm::ivec3& brick : bricks)
		{
			const uint64_t revision = Nyx::AppendTerrainBake(baked, brick);
			Require(revision == Nyx::GetTerrainRevision(baked) && baked.Edits.back().Kind == Nyx::ESdfTerrainEditKind::Bake && baked.Edits.back().Baked,
				"A bake should be an edit of its own, with the next revision and its values");
		}
		// Edits after the bakes, then some bricks baked again over them
		SdfTerrainComponent after = plain;
		for (int i = 0; i < 20; ++i)
		{
			Nyx::SdfTerrainEdit edit;
			edit.Shape = static_cast<Nyx::ESdfTerrainEditShape>(i % 3);
			edit.Mode = static_cast<Nyx::ESdfTerrainEditMode>(i % 3);
			edit.Position = glm::vec3(sequence.Between(-14.0f, 14.0f), sequence.Between(-8.0f, 8.0f), sequence.Between(-14.0f, 14.0f));
			edit.End = edit.Position + glm::vec3(sequence.Between(-3.0f, 3.0f), 0.0f, sequence.Between(-3.0f, 3.0f));
			edit.HalfSize = glm::vec3(sequence.Between(0.5f, 2.0f));
			edit.Radius = sequence.Between(0.5f, 3.0f);
			edit.BlendRadius = i % 2 == 0 ? 0.5f : 0.0f;
			edit.Material = 8;
			Nyx::AppendTerrainEdit(baked, edit);
			Nyx::AppendTerrainEdit(after, edit);
		}
		for (size_t n = 0; n < bricks.size(); n += 3)
		{
			Nyx::AppendTerrainBake(baked, bricks[n]);
		}
		const size_t bakes = size_t(std::count_if(baked.Edits.begin(), baked.Edits.end(), [](const Nyx::SdfTerrainEdit& edit) { return edit.Kind == Nyx::ESdfTerrainEditKind::Bake; }));

		// On the corners, the same bits as without bakes: a grid of 1 m cells over the bricks and past
		// them, a batch, single points
		const glm::vec3 origin(-19.0f, -11.0f, -21.0f);
		const glm::ivec3 size(40, 26, 38);
		const Nyx::FieldGrid withBakes = Nyx::SampleTerrainGrid(baked, origin, 1.0f, size);
		const Nyx::FieldGrid without = Nyx::SampleTerrainGrid(after, origin, 1.0f, size);
		for (size_t i = 0; i < withBakes.Values.size(); ++i)
		{
			Require(BitsOf(withBakes.Values[i]) == BitsOf(without.Values[i]) && withBakes.Materials[i] == without.Materials[i],
				"On the corners of the bricks' cells, bakes should be the same bits as the edits they were made from");
		}
		for (int n = 0; n < 300; ++n)
		{
			const glm::vec3 corner = glm::floor(glm::vec3(sequence.Between(-20.0f, 20.0f), sequence.Between(-12.0f, 12.0f), sequence.Between(-20.0f, 20.0f)));
			const Nyx::TerrainSample a = Nyx::SampleTerrain(baked, corner);
			const Nyx::TerrainSample b = Nyx::SampleTerrain(after, corner);
			Require(BitsOf(a.Value) == BitsOf(b.Value) && a.Material == b.Material, "A single corner should be the same bits with bakes");
		}

		// Between the corners of a brick baked last with nothing after: the corners blended
		SdfTerrainComponent once = plain;
		Nyx::AppendTerrainBake(once, glm::ivec3(0, 0, 0));
		int between = 0;
		for (int n = 0; n < 400; ++n)
		{
			const glm::vec3 point(sequence.Between(0.0f, 8.0f), sequence.Between(0.0f, 8.0f), sequence.Between(0.0f, 8.0f));
			const glm::vec3 low = glm::min(glm::floor(point), glm::vec3(7.0f));
			const glm::vec3 t = point - low;
			float corners[8];
			for (int c = 0; c < 8; ++c)
			{
				corners[c] = Nyx::SampleTerrainValue(plain, low + glm::vec3(float(c & 1), float((c >> 1) & 1), float((c >> 2) & 1)));
			}
			auto mix = [](float a, float b, float along) { return a * (1.0f - along) + b * along; };
			const float expected = mix(mix(mix(corners[0], corners[1], t.x), mix(corners[2], corners[3], t.x), t.y),
				mix(mix(corners[4], corners[5], t.x), mix(corners[6], corners[7], t.x), t.y), t.z);
			const float value = Nyx::SampleTerrainValue(once, point);
			Require(BitsOf(value) == BitsOf(expected), "Between its corners a bake should be their values blended");
			between += value != Nyx::SampleTerrainValue(plain, point) ? 1 : 0;
		}
		Require(between > 100, "Between its corners a bake should differ from the edits it was made from");

		// Many points at once (each block's bakes looked up, the edits found near), and the edits
		// reaching a box kept alone, the same bits as single points
		constexpr size_t Count = 3001;
		std::vector<float> x(Count);
		std::vector<float> y(Count);
		std::vector<float> z(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			x[i] = sequence.Between(-22.0f, 22.0f);
			y[i] = sequence.Between(-14.0f, 14.0f);
			z[i] = sequence.Between(-22.0f, 22.0f);
		}
		std::vector<float> values(Count);
		std::vector<uint32_t> materials(Count);
		Nyx::SampleTerrainBatch(baked, x.data(), y.data(), z.data(), Count, values.data(), materials.data());
		for (size_t i = 0; i < Count; ++i)
		{
			const Nyx::TerrainSample single = Nyx::SampleTerrain(baked, glm::vec3(x[i], y[i], z[i]));
			Require(BitsOf(single.Value) == BitsOf(values[i]) && single.Material == materials[i], "A baked batch should be the same bits as single points");
		}
		for (int box = 0; box < 12; ++box)
		{
			const glm::vec3 low(sequence.Between(-24.0f, 8.0f), sequence.Between(-14.0f, 2.0f), sequence.Between(-24.0f, 8.0f));
			const glm::ivec3 cells(33);
			const glm::vec3 high = low + glm::vec3(cells - 1) * 0.5f;
			SdfTerrainComponent kept = baked;
			Nyx::KeepTerrainEditsReaching(kept, low, high);
			Require(kept.Edits.size() < baked.Edits.size(), "A box should keep only some of the edits and bakes");
			const Nyx::FieldGrid all = Nyx::SampleTerrainGrid(baked, low, 0.5f, cells);
			const Nyx::FieldGrid some = Nyx::SampleTerrainGrid(kept, low, 0.5f, cells);
			for (size_t i = 0; i < all.Values.size(); ++i)
			{
				const glm::ivec3 at(int(i % 33), int((i / 33) % 33), int(i / (33 * 33)));
				const Nyx::TerrainSample single = Nyx::SampleTerrain(baked, low + glm::vec3(at) * 0.5f);
				Require(BitsOf(all.Values[i]) == BitsOf(some.Values[i]) && all.Materials[i] == some.Materials[i] && BitsOf(single.Value) == BitsOf(all.Values[i]),
					"A baked terrain left the edits reaching a box should be the same bits in it");
			}
		}

		// The slope bound within bricks, and boxes' ranges anywhere
		const float bound = Nyx::GetTerrainMaxSlope(baked);
		Require(bound > Nyx::GetTerrainMaxSlope(after), "Bakes should widen the slope bound");
		float steepest = 0.0f;
		for (int n = 0; n < 2000; ++n)
		{
			const glm::vec3 brick(bricks[size_t(n) % bricks.size()]);
			const glm::vec3 point = brick * 8.0f + glm::vec3(sequence.Between(0.1f, 7.8f), sequence.Between(0.1f, 7.8f), sequence.Between(0.1f, 7.8f));
			const glm::vec3 near = point + glm::vec3(sequence.Between(-0.05f, 0.05f), sequence.Between(-0.05f, 0.05f), sequence.Between(-0.05f, 0.05f));
			steepest = std::max(steepest, std::fabs(Nyx::SampleTerrainValue(baked, near) - Nyx::SampleTerrainValue(baked, point)) / std::max(glm::length(near - point), 1e-4f));
		}
		Require(steepest <= bound * 1.001f, "A baked brick should never be steeper than the bound");
		for (int box = 0; box < 60; ++box)
		{
			const glm::vec3 low(sequence.Between(-22.0f, 14.0f), sequence.Between(-12.0f, 8.0f), sequence.Between(-22.0f, 14.0f));
			const glm::vec3 high = low + glm::vec3(sequence.Between(0.3f, 12.0f));
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(baked, low, high);
			Nyx::TerrainBoxEdits carried = Nyx::BeginTerrainBoxEdits(baked, low, high);
			Nyx::ContinueTerrainBoxEdits(baked, carried);
			for (int n = 0; n < 216; ++n)
			{
				const glm::vec3 point = low + (high - low) * glm::vec3(float(n % 6), float((n / 6) % 6), float(n / 36)) / 5.0f;
				const float value = Nyx::SampleTerrainValue(baked, point);
				Require(value >= range.Lowest && value <= range.Highest && value >= carried.Range.Lowest && value <= carried.Range.Highest,
					"A baked value should be within its box's range");
			}
		}

		// Without their values (loaded, or put back by an undo): the same bits, then made again the same
		SdfTerrainComponent lost = baked;
		for (Nyx::SdfTerrainEdit& edit : lost.Edits)
		{
			edit.Baked.reset();
		}
		for (int n = 0; n < 200; ++n)
		{
			const glm::vec3 point(sequence.Between(-18.0f, 18.0f), sequence.Between(-10.0f, 10.0f), sequence.Between(-18.0f, 18.0f));
			const Nyx::TerrainSample a = Nyx::SampleTerrain(baked, point);
			const Nyx::TerrainSample b = Nyx::SampleTerrain(lost, point);
			Require(BitsOf(a.Value) == BitsOf(b.Value) && a.Material == b.Material, "A bake without its values should sample the same bits");
		}
		const Nyx::FieldGrid lostGrid = Nyx::SampleTerrainGrid(lost, origin, 1.0f, size);
		Require(lostGrid.Values == withBakes.Values, "Bakes without their values should sample the same corners");
		SdfTerrainComponent fromEarlier = lost;
		Require(Nyx::RestoreTerrainBakes(fromEarlier, &baked) == 0, "Bakes should take the values of the same edits before");
		Require(Nyx::RestoreTerrainBakes(lost, nullptr, 5) == 5, "At most so many bakes should be made");
		Require(Nyx::RestoreTerrainBakes(lost) == bakes - 5 && Nyx::RestoreTerrainBakes(lost) == 0, "Every bake without values should be made");
		for (size_t at = 0; at < baked.Edits.size(); ++at)
		{
			if (baked.Edits[at].Kind == Nyx::ESdfTerrainEditKind::Bake)
			{
				Require(lost.Edits[at].Baked->Values == baked.Edits[at].Baked->Values && lost.Edits[at].Baked->Materials == baked.Edits[at].Baked->Materials &&
							lost.Edits[at].Baked->Lowest == baked.Edits[at].Baked->Lowest && fromEarlier.Edits[at].Baked == baked.Edits[at].Baked,
					"A bake made again should be the same bits");
			}
		}
		SdfTerrainComponent otherLayers = baked;
		otherLayers.Layers[0].Amplitude += 1.0f;
		Require(Nyx::RestoreTerrainBakes(otherLayers, &baked) == bakes, "Bakes made with other layers should be made again, not taken");

		// Undone with the edits: back to those before them
		SdfTerrainComponent undone = baked;
		undone.Edits.resize(plain.Edits.size());
		const Nyx::FieldGrid undoneGrid = Nyx::SampleTerrainGrid(undone, origin, 0.5f, size);
		const Nyx::FieldGrid plainGrid = Nyx::SampleTerrainGrid(plain, origin, 0.5f, size);
		Require(undoneGrid.Values == plainGrid.Values && undoneGrid.Materials == plainGrid.Materials && Nyx::HashTerrainEdits(undone) == Nyx::HashTerrainEdits(plain),
			"Bakes undone with the edits should leave the terrain as it was");

		// Due after Bake After edits near a brick, once each; never with 0
		SdfTerrainComponent flat = MakeFlat(0.0f);
		Nyx::TerrainBakeCounts counts;
		Nyx::SdfTerrainEdit dab;
		dab.Position = glm::vec3(4.0f, 0.0f, 4.0f);
		dab.Radius = 1.0f;
		for (uint32_t n = 1; n < flat.BakeAfter; ++n)
		{
			Nyx::AppendTerrainEdit(flat, dab);
			Require(Nyx::BakeTerrainBricks(flat, counts) == 0, "No brick should be baked before Bake After edits near it");
		}
		Nyx::AppendTerrainEdit(flat, dab);
		const size_t made = Nyx::BakeTerrainBricks(flat, counts);
		Require(made >= 1 && flat.Edits.back().Kind == Nyx::ESdfTerrainEditKind::Bake && Nyx::BakeTerrainBricks(flat, counts) == 0,
			"Bake After edits near a brick should bake it once");
		bool bBrickBaked = false;
		for (const Nyx::SdfTerrainEdit& edit : flat.Edits)
		{
			bBrickBaked = bBrickBaked || (edit.Kind == Nyx::ESdfTerrainEditKind::Bake && edit.Position == glm::vec3(0.0f, -8.0f, 0.0f));
		}
		Require(bBrickBaked, "The brick the edits were in should be baked");
		flat.Edits.resize(flat.Edits.size() - made - 3);
		Nyx::AppendTerrainEdit(flat, dab);
		Require(Nyx::BakeTerrainBricks(flat, counts) == 0, "After an undo, the edits should be counted from there");
		SdfTerrainComponent never = MakeFlat(0.0f);
		never.BakeAfter = 0;
		Nyx::TerrainBakeCounts neverCounts;
		for (int n = 0; n < 40; ++n)
		{
			Nyx::AppendTerrainEdit(never, dab);
			Require(Nyx::BakeTerrainBricks(never, neverCounts) == 0, "Bake After 0 should never bake");
		}
		std::cout << "Bakes: " << bakes << " over " << after.Edits.size() << " edits, slope bound " << bound << " (" << Nyx::GetTerrainMaxSlope(after)
				  << " without), steepest in a brick " << steepest << "\n";
	}

	// The edits' canonical form: written and read back, the same edits and bakes, the same bits
	// sampled; the same bytes whether bakes have their values; the bytes of a set of edits as written
	// on every machine (golden); edits not whole, of a later version or that no terrain makes not read.
	// Region hashes: the same with bakes' values or without; an edit changing only its regions'; at a
	// revision, those of the edits up to it; other layers, every one's
	void TestSaveFormat()
	{
		SdfTerrainComponent terrain = MakeEdited();
		for (const glm::ivec3& brick : { glm::ivec3(0, 0, 0), glm::ivec3(-1, 0, 2), glm::ivec3(1, -1, -1) })
		{
			Nyx::AppendTerrainBake(terrain, brick);
		}
		Nyx::SdfTerrainEdit late;
		late.Position = glm::vec3(3.0f, 2.0f, 4.0f);
		late.Radius = 2.0f;
		Nyx::AppendTerrainEdit(terrain, late);

		Nyx::BinaryWriter writer;
		Nyx::WriteTerrainEdits(terrain, writer);
		Nyx::BinaryReader reader;
		reader.LoadFromMemory(writer.GetBytes());
		SdfTerrainComponent loaded = terrain;
		Require(Nyx::ReadTerrainEdits(reader, loaded.Edits) && reader.GetRemainingSize() == 0, "Edits written should be read back whole");
		Require(Nyx::HashTerrainEdits(loaded) == Nyx::HashTerrainEdits(terrain) && Nyx::CountSameTerrainEdits(loaded, terrain) == terrain.Edits.size(),
			"Edits read back should be the edits written");
		for (size_t at = 0; at < terrain.Edits.size(); ++at)
		{
			if (terrain.Edits[at].Kind == Nyx::ESdfTerrainEditKind::Bake)
			{
				Require(loaded.Edits[at].Baked && loaded.Edits[at].Baked->Values == terrain.Edits[at].Baked->Values &&
							loaded.Edits[at].Baked->Materials == terrain.Edits[at].Baked->Materials && loaded.Edits[at].Baked->Lowest == terrain.Edits[at].Baked->Lowest &&
							loaded.Edits[at].Baked->Layers == terrain.Edits[at].Baked->Layers,
					"A bake read back should have its values, the same bits");
			}
		}
		Require(Nyx::RestoreTerrainBakes(loaded) == 0, "Bakes read back should need nothing made again");
		const Nyx::FieldGrid written = Nyx::SampleTerrainGrid(terrain, glm::vec3(-12.3f, -9.1f, -14.7f), 0.7f, glm::ivec3(36));
		const Nyx::FieldGrid read = Nyx::SampleTerrainGrid(loaded, glm::vec3(-12.3f, -9.1f, -14.7f), 0.7f, glm::ivec3(36));
		Require(written.Values == read.Values && written.Materials == read.Materials, "Edits read back should sample the same bits");

		SdfTerrainComponent lost = terrain;
		for (Nyx::SdfTerrainEdit& edit : lost.Edits)
		{
			edit.Baked.reset();
		}
		Nyx::BinaryWriter lostWriter;
		Nyx::WriteTerrainEdits(lost, lostWriter);
		Require(lostWriter.GetBytes() == writer.GetBytes(), "Bakes without their values should write the same bytes");

		// Golden: the bytes of these edits, as on every machine
		uint64_t hash = 0xCBF29CE484222325ull;
		for (const std::byte byte : writer.GetBytes())
		{
			hash = (hash ^ uint64_t(byte)) * 0x100000001B3ull;
		}
		std::cout << "Save format: " << terrain.Edits.size() << " edits in " << writer.GetBytes().size() << " bytes, their hash 0x" << std::hex << hash << std::dec << "\n";
		constexpr uint64_t Golden = 0x786200d6eb32b17bull;
		Require(hash == Golden, "The edits' bytes should be as on every machine");

		// Not read: cut short, a later version, an unknown kind, a value not finite, revisions not
		// counting up
		auto rejects = [&](std::vector<std::byte> bytes, const char* what)
		{
			Nyx::BinaryReader bad;
			bad.LoadFromMemory(std::move(bytes));
			std::vector<Nyx::SdfTerrainEdit> edits{ Nyx::SdfTerrainEdit{} };
			Require(!Nyx::ReadTerrainEdits(bad, edits) && edits.empty(), what);
		};
		std::vector<std::byte> bytes = writer.GetBytes();
		rejects(std::vector<std::byte>(bytes.begin(), bytes.end() - 5), "Edits cut short shouldn't be read");
		std::vector<std::byte> later = bytes;
		later[4] = std::byte(Nyx::TerrainEditsFormatVersion + 1);
		rejects(later, "Edits of a later version shouldn't be read");
		constexpr size_t First = 16;
		std::vector<std::byte> kind = bytes;
		kind[First + 8] = std::byte(7);
		rejects(kind, "An edit of an unknown kind shouldn't be read");
		std::vector<std::byte> infinite = bytes;
		const float notFinite = std::numeric_limits<float>::infinity();
		std::memcpy(infinite.data() + First + 11, &notFinite, sizeof(notFinite));
		rejects(infinite, "An edit with a value not finite shouldn't be read");
		std::vector<std::byte> backwards = bytes;
		const uint64_t big = 1000;
		std::memcpy(backwards.data() + First, &big, sizeof(big));
		rejects(backwards, "Edits whose revisions don't count up shouldn't be read");

		// Region hashes
		const std::vector<Nyx::TerrainRegionHash> regions = Nyx::HashTerrainRegions(terrain);
		Require(regions.size() >= 2 && regions == Nyx::HashTerrainRegions(lost), "Regions should hash the same whether bakes have their values");
		SdfTerrainComponent moved = terrain;
		moved.Edits[5].Material += 1;
		const std::vector<Nyx::TerrainRegionHash> movedRegions = Nyx::HashTerrainRegions(moved);
		int differ = 0;
		for (size_t n = 0; n < regions.size(); ++n)
		{
			glm::vec3 editLow;
			glm::vec3 editHigh;
			Nyx::GetTerrainEditBounds(terrain.Edits[5], editLow, editHigh);
			const glm::vec3 regionLow = glm::vec3(regions[n].Region) * Nyx::TerrainRegionSize;
			const bool bReached = glm::all(glm::lessThanEqual(regionLow, editHigh + 1.0f)) && glm::all(glm::lessThanEqual(editLow - 1.0f, regionLow + Nyx::TerrainRegionSize));
			Require(movedRegions[n].Region == regions[n].Region, "An edit of another material should leave the regions listed");
			if (movedRegions[n].Hash != regions[n].Hash)
			{
				Require(bReached, "An edit changed should change only its regions' hashes");
				++differ;
			}
		}
		Require(differ > 0, "An edit changed should change its regions' hashes");
		SdfTerrainComponent earlier = terrain;
		earlier.Edits.resize(30);
		Require(Nyx::HashTerrainRegions(terrain, earlier.Edits.back().Revision) == Nyx::HashTerrainRegions(earlier), "Regions at a revision should hash the edits up to it");
		SdfTerrainComponent otherLayers = terrain;
		otherLayers.Layers[0].Seed += 1;
		const std::vector<Nyx::TerrainRegionHash> otherRegions = Nyx::HashTerrainRegions(otherLayers);
		for (size_t n = 0; n < regions.size(); ++n)
		{
			Require(otherRegions[n].Hash != regions[n].Hash, "Other layers should change every region's hash");
		}
		std::cout << "Save format: " << regions.size() << " regions hashed, " << differ << " changed by an edit of another material\n";
	}

	// Checkpoints: saved and loaded, the same edits, bakes, revision and region hashes; a save over an
	// old one replaces it whole, one that can't be written leaves nothing; another generator's or
	// another terrain's not opened, the terrain left as it was; one changed after it was written
	// read with the regions that differ listed. Joining: the edits after a checkpoint taken, only
	// after the latest and in order. Repair: a region planted with other values found, and the
	// host's bakes there taken; a region without a bake baked by the host first.
	void TestCheckpoints()
	{
		SdfTerrainComponent host = MakeEdited();
		Nyx::TerrainBakeCounts counts;
		host.BakeAfter = 4;
		Sequence sequence;
		for (int n = 0; n < 24; ++n)
		{
			Nyx::SdfTerrainEdit dab;
			dab.Position = glm::vec3(sequence.Between(-6.0f, 6.0f), sequence.Between(-2.0f, 4.0f), sequence.Between(-6.0f, 6.0f));
			dab.Radius = sequence.Between(1.0f, 2.5f);
			dab.Mode = n % 3 == 0 ? Nyx::ESdfTerrainEditMode::Add : Nyx::ESdfTerrainEditMode::Subtract;
			Nyx::AppendTerrainEdit(host, dab);
			Nyx::BakeTerrainBricks(host, counts);
		}
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxSdfTerrainTests.nyxterrain";
		std::filesystem::remove(path);
		Require(Nyx::SaveTerrainCheckpoint(MakeEdited(), path) && Nyx::SaveTerrainCheckpoint(host, path), "A checkpoint should be saved over an old one");
		Require(!Nyx::SaveTerrainCheckpoint(host, path.parent_path() / "No Such Folder" / "x.nyxterrain") &&
					!std::filesystem::exists(path.parent_path() / "No Such Folder"),
			"A checkpoint that can't be written should leave nothing");

		SdfTerrainComponent loaded = MakeEdited();
		loaded.Edits.clear();
		const Nyx::TerrainCheckpointRead read = Nyx::LoadTerrainCheckpoint(path, loaded);
		Require(read.Result == Nyx::ETerrainCheckpointRead::Read && read.Revision == Nyx::GetTerrainRevision(host) && read.Mismatched.empty(),
			"A checkpoint saved should be loaded whole, at its revision");
		Require(Nyx::HashTerrainEdits(loaded) == Nyx::HashTerrainEdits(host) && Nyx::HashTerrainRegions(loaded) == Nyx::HashTerrainRegions(host),
			"A checkpoint loaded should be the same edits and region hashes");
		const Nyx::FieldGrid hostGrid = Nyx::SampleTerrainGrid(host, glm::vec3(-10.5f, -6.25f, -9.75f), 0.5f, glm::ivec3(42));
		const Nyx::FieldGrid loadedGrid = Nyx::SampleTerrainGrid(loaded, glm::vec3(-10.5f, -6.25f, -9.75f), 0.5f, glm::ivec3(42));
		Require(hostGrid.Values == loadedGrid.Values && hostGrid.Materials == loadedGrid.Materials, "A checkpoint loaded should sample the same bits");

		// Not opened: another generator, another terrain; changed after it was written: read, listed
		Nyx::BinaryWriter written;
		Nyx::WriteTerrainCheckpoint(host, written);
		auto readBytes = [&](std::vector<std::byte> bytes, SdfTerrainComponent& into)
		{
			Nyx::BinaryReader in;
			in.LoadFromMemory(std::move(bytes));
			return Nyx::ReadTerrainCheckpoint(in, into);
		};
		std::vector<std::byte> otherGenerator = written.GetBytes();
		otherGenerator[8] ^= std::byte(1);
		SdfTerrainComponent untouched = MakeEdited();
		Require(readBytes(otherGenerator, untouched).Result == Nyx::ETerrainCheckpointRead::OtherGenerator && Nyx::HashTerrainEdits(untouched) == Nyx::HashTerrainEdits(MakeEdited()),
			"Another generator's checkpoint shouldn't be opened, the terrain left as it was");
		SdfTerrainComponent otherTerrain = MakeEdited();
		otherTerrain.Seed += 1;
		Require(readBytes(written.GetBytes(), otherTerrain).Result == Nyx::ETerrainCheckpointRead::OtherLayers, "Another terrain's checkpoint shouldn't be opened");
		Require(readBytes(std::vector<std::byte>(written.GetBytes().begin(), written.GetBytes().end() - 3), untouched).Result == Nyx::ETerrainCheckpointRead::Unreadable,
			"A checkpoint cut short shouldn't be opened");
		std::vector<std::byte> changed = written.GetBytes();
		changed[changed.size() - 1] ^= std::byte(1);
		SdfTerrainComponent planted = MakeEdited();
		const Nyx::TerrainCheckpointRead changedRead = readBytes(changed, planted);
		Require(changedRead.Result == Nyx::ETerrainCheckpointRead::Read && changedRead.Mismatched.size() == 1, "A checkpoint changed after it was written should list the region");

		// Joining: a checkpoint at a revision, then the edits after it
		SdfTerrainComponent joiner = MakeEdited();
		SdfTerrainComponent early = host;
		early.Edits.resize(host.Edits.size() / 2);
		Nyx::BinaryWriter checkpoint;
		Nyx::WriteTerrainCheckpoint(early, checkpoint);
		Require(readBytes(checkpoint.GetBytes(), joiner).Result == Nyx::ETerrainCheckpointRead::Read, "A joiner should read the host's checkpoint");
		Nyx::BinaryWriter afterBytes;
		Nyx::WriteTerrainEdits(host, afterBytes, Nyx::GetTerrainRevision(joiner));
		Nyx::BinaryReader afterReader;
		afterReader.LoadFromMemory(afterBytes.GetBytes());
		std::vector<Nyx::SdfTerrainEdit> after;
		Require(Nyx::ReadTerrainEdits(afterReader, after) && after.size() == host.Edits.size() - early.Edits.size(), "The edits after a checkpoint should be those after its revision");
		for (Nyx::SdfTerrainEdit& edit : after)
		{
			edit.Baked.reset();
		}
		SdfTerrainComponent gap = joiner;
		Require(Nyx::TakeTerrainEdits(gap, std::span<const Nyx::SdfTerrainEdit>(after).subspan(1)) && gap.Edits.size() == joiner.Edits.size() + after.size() - 1,
			"Edits after a gap should be taken as they come");
		Require(!Nyx::TakeTerrainEdits(gap, after), "Edits before the latest shouldn't be taken");
		Require(Nyx::TakeTerrainEdits(joiner, after) && Nyx::HashTerrainRegions(joiner) == Nyx::HashTerrainRegions(host),
			"A joiner should come to the host's region hashes, its bakes made by itself");

		// Repair: a bake's values planted other; found, the host's taken
		size_t bakeAt = 0;
		while (joiner.Edits[bakeAt].Kind != Nyx::ESdfTerrainEditKind::Bake)
		{
			++bakeAt;
		}
		std::array<float, Nyx::SdfTerrainBakedBrick::Count> other = Nyx::GetTerrainBakeValues(joiner, bakeAt)->Values;
		other[400] += 0.01f;
		joiner.Edits[bakeAt].Baked = Nyx::MakeTerrainBakedBrick(other, joiner.Edits[bakeAt].Baked->Materials, joiner.Edits[bakeAt].Baked->Layers);
		const std::vector<glm::ivec3> mismatched = Nyx::FindTerrainRegionMismatches(Nyx::HashTerrainRegions(host), Nyx::HashTerrainRegions(joiner));
		const glm::ivec3 region(glm::floor(joiner.Edits[bakeAt].Position / Nyx::TerrainRegionSize));
		Require(mismatched.size() == 1 && mismatched[0] == region, "A planted mismatch should be found in its region alone");
		Nyx::BinaryWriter repair;
		Nyx::WriteTerrainRegionRepair(host, region, repair);
		Nyx::BinaryReader repairReader;
		repairReader.LoadFromMemory(repair.GetBytes());
		Require(Nyx::ApplyTerrainRegionRepair(joiner, region, repairReader) && Nyx::HashTerrainRegions(joiner) == Nyx::HashTerrainRegions(host),
			"A region repaired should take the host's values");
		Nyx::BinaryReader wrongRegion;
		wrongRegion.LoadFromMemory(repair.GetBytes());
		Require(!Nyx::ApplyTerrainRegionRepair(joiner, region + glm::ivec3(1, 0, 0), wrongRegion), "A repair of another region shouldn't be taken");

		// A region without a bake is baked by the host first
		SdfTerrainComponent bare = MakeEdited();
		const size_t baked = Nyx::BakeTerrainRegion(bare, glm::ivec3(0, 0, 0));
		Require(baked > 0 && bare.Edits.back().Kind == Nyx::ESdfTerrainEditKind::Bake && Nyx::BakeTerrainRegion(bare, glm::ivec3(0, 0, 0)) == 0,
			"A region without a bake should be baked by the host, once");
		std::filesystem::remove(path);
		std::cout << "Checkpoints: " << host.Edits.size() << " edits saved and loaded, " << baked << " bricks baked for a region's repair\n";
	}

	bool SameBox(const Nyx::TerrainBoxEdits& a, const Nyx::TerrainBoxEdits& b)
	{
		return a.Seen == b.Seen && a.Reach.Hash == b.Reach.Hash && a.Reach.Count == b.Reach.Count && a.Reaching == b.Reaching &&
			   BitsOf(a.Range.Lowest) == BitsOf(b.Range.Lowest) && BitsOf(a.Range.Highest) == BitsOf(b.Range.Highest);
	}

	// The edits' index: a box continued through it is the box continued through every edit, the same
	// bits, as edits are added one at a time, with edits large and small and far apart; a box rewound
	// to fewer edits is the box taken over those; an edit changed in the middle, the index brought up
	// from there and the boxes rewound to it and continued, are as if made anew
	void TestEditIndex()
	{
		SdfTerrainComponent growing = MakeRich();
		const SdfTerrainComponent edited = MakeEdited();
		Nyx::TerrainEditIndex index;
		Sequence sequence;
		for (size_t n = 0; n < edited.Edits.size(); ++n)
		{
			Nyx::SdfTerrainEdit edit = edited.Edits[n];
			// Some far off, one huge
			if (n % 7 == 3)
			{
				edit.Position += glm::vec3(400.0f, 0.0f, -300.0f);
				edit.End += glm::vec3(400.0f, 0.0f, -300.0f);
			}
			if (n == 11)
			{
				edit.Radius = 300.0f;
				edit.Shape = Nyx::ESdfTerrainEditShape::Sphere;
			}
			growing.Edits.push_back(edit);
			index.Update(growing, growing.Edits.size() - 1);
		}
		Require(index.GetCount() == growing.Edits.size(), "The index should have every edit");

		std::vector<Nyx::TerrainBoxEdits> plain;
		std::vector<Nyx::TerrainBoxEdits> indexed;
		for (int box = 0; box < 60; ++box)
		{
			const glm::vec3 low = PointNearEdits(sequence) + glm::vec3(box % 4 == 0 ? 400.0f : 0.0f, 0.0f, box % 4 == 0 ? -300.0f : 0.0f);
			const glm::vec3 high = low + glm::vec3(sequence.Between(1.0f, 80.0f));
			plain.push_back(Nyx::BeginTerrainBoxEdits(growing, low, high));
			Nyx::ContinueTerrainBoxEdits(growing, plain.back());
			indexed.push_back(Nyx::BeginTerrainBoxEdits(growing, low, high));
			Nyx::ContinueTerrainBoxEdits(growing, indexed.back(), &index);
			Require(SameBox(plain.back(), indexed.back()), "A box continued through the index should be the box continued through every edit");
		}

		// Rewound to the first 25: as if taken over those alone
		SdfTerrainComponent first = growing;
		first.Edits.resize(25);
		for (size_t n = 0; n < plain.size(); ++n)
		{
			Nyx::TerrainBoxEdits rewound = indexed[n];
			Nyx::RewindTerrainBoxEdits(growing, rewound, 25);
			Nyx::TerrainBoxEdits fresh = Nyx::BeginTerrainBoxEdits(first, plain[n].BoxMin, plain[n].BoxMax);
			Nyx::ContinueTerrainBoxEdits(first, fresh);
			Require(SameBox(rewound, fresh), "A box rewound to fewer edits should be the box taken over those");
		}

		// The tenth changed: the same before it, the index and boxes brought up from there
		SdfTerrainComponent changed = growing;
		changed.Edits[10].Position += glm::vec3(3.0f, -1.0f, 2.0f);
		changed.Edits[10].Mode = Nyx::ESdfTerrainEditMode::Add;
		const size_t same = Nyx::CountSameTerrainEdits(changed, growing);
		Require(same == 10, "Edits changed at the tenth should be the same for ten");
		Nyx::TerrainEditIndex moved = index;
		moved.Update(changed, same);
		Nyx::TerrainEditIndex anew;
		anew.Update(changed, 0);
		for (size_t n = 0; n < plain.size(); ++n)
		{
			Nyx::TerrainBoxEdits carried = indexed[n];
			Nyx::RewindTerrainBoxEdits(growing, carried, same);
			Nyx::ContinueTerrainBoxEdits(changed, carried, &moved);
			Nyx::TerrainBoxEdits fresh = Nyx::BeginTerrainBoxEdits(changed, plain[n].BoxMin, plain[n].BoxMax);
			Nyx::ContinueTerrainBoxEdits(changed, fresh, &anew);
			Require(SameBox(carried, fresh), "A box rewound to an edit changed and continued should be as if made anew");
		}
	}

	// Not a test: a chunk's samples near the surface, in a batch
	// A world plan made into ground by a Plan layer, with tunnels pinned in and a rift pinned through
	SdfTerrainComponent MakePlanned()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 4;
		terrain.Layers = { MakeLayer(ESdfTerrainLayerKind::Plan) };
		terrain.Layers[0].Frequency = 0.006f;
		terrain.Layers[0].Octaves = 4;
		terrain.Plan.Pins = { Nyx::SdfTerrainPlanPin{ .Landform = Nyx::ESdfTerrainLandform::Tunnels, .Position = glm::vec2(150.0f, 150.0f), .Radius = 200.0f },
			Nyx::SdfTerrainPlanPin{ .Landform = Nyx::ESdfTerrainLandform::Rift, .Position = glm::vec2(-300.0f, -200.0f) } };
		return terrain;
	}

	// A Plan layer: batches the same bits as single points, never steeper than its bound, within box
	// ranges; the ground where the plan says: the plains at their height, the sea's floor below its
	// level, a rift open down to its floor, air under the underside and past the edge, a river's bed
	// open, tunnels where they were pinned; golden values
	void TestPlanLayer()
	{
		using Nyx::TerrainWorldPlan;
		const SdfTerrainComponent terrain = MakePlanned();
		const std::shared_ptr<const TerrainWorldPlan> plan = Nyx::GetTerrainWorldPlan(terrain);
		const Nyx::SdfTerrainWorldPlan& made = terrain.Plan;
		Sequence sequence;
		auto randomPoint = [&] { return glm::vec3(sequence.Between(-1100.0f, 1100.0f), sequence.Between(-320.0f, 140.0f), sequence.Between(-1100.0f, 1100.0f)); };

		constexpr size_t Count = 4096;
		std::vector<float> x(Count);
		std::vector<float> y(Count);
		std::vector<float> z(Count);
		for (size_t i = 0; i < Count; ++i)
		{
			const glm::vec3 point = randomPoint();
			x[i] = point.x;
			y[i] = point.y;
			z[i] = point.z;
		}
		std::vector<float> values(Count);
		Nyx::SampleTerrainBatch(terrain, x.data(), y.data(), z.data(), Count, values.data());
		uint64_t hash = 0xCBF29CE484222325ull;
		for (size_t i = 0; i < Count; ++i)
		{
			Require(BitsOf(Nyx::SampleTerrainValue(terrain, glm::vec3(x[i], y[i], z[i]))) == BitsOf(values[i]), "A Plan layer's batch should be the same bits as one point");
			hash = HashBits(hash, BitsOf(values[i]));
		}
		constexpr uint64_t Golden = 0xa7cc4a7a53bf3c6eull;
		std::cout << std::hex << "Plan layer golden hash: 0x" << hash << std::dec << "\n";
		Require(hash == Golden, "A Plan layer's values changed");

		const float bound = Nyx::GetTerrainMaxSlope(terrain);
		float steepest = 0.0f;
		for (int i = 0; i < 20000; ++i)
		{
			const glm::vec3 a = randomPoint();
			const glm::vec3 b = a + glm::vec3(sequence.Between(-1.0f, 1.0f), sequence.Between(-1.0f, 1.0f), sequence.Between(-1.0f, 1.0f));
			const float apart = glm::length(b - a);
			if (apart > 1e-3f)
			{
				steepest = std::max(steepest, std::fabs(Nyx::SampleTerrainValue(terrain, b) - Nyx::SampleTerrainValue(terrain, a)) / apart);
			}
		}
		Require(steepest <= bound * 1.001f, "A Plan layer should never be steeper than its bound: " + std::to_string(steepest) + " > " + std::to_string(bound));
		std::cout << "Plan layer slope: steepest measured " << steepest << " a metre, bound " << bound << "\n";

		int settled = 0;
		for (int box = 0; box < 120; ++box)
		{
			// Half the boxes near the surface, where the carves are
			glm::vec3 low = randomPoint();
			if (box % 2 == 0)
			{
				low.y = plan->At(plan->Height, low.x, low.z) - sequence.Between(0.0f, 40.0f);
			}
			const glm::vec3 high = low + glm::vec3(sequence.Between(1.0f, 24.0f));
			const Nyx::TerrainValueRange range = Nyx::GetTerrainValueRange(terrain, low, high);
			settled += range.IsAllAir() || range.IsAllSolid() ? 1 : 0;
			constexpr int Steps = 10;
			for (int i = 0; i <= Steps; ++i)
			{
				for (int j = 0; j <= Steps; ++j)
				{
					for (int k = 0; k <= Steps; ++k)
					{
						const float value = Nyx::SampleTerrainValue(terrain, low + (high - low) * glm::vec3(float(i), float(j), float(k)) / float(Steps));
						Require(value >= range.Lowest && value <= range.Highest, "A Plan layer's value should be within its box's range");
					}
				}
			}
		}
		std::cout << "Plan layer ranges: " << settled << " of 120 boxes settled\n";

		// Nodes of each kind, well inside it
		auto findNode = [&](auto&& wanted) -> std::optional<glm::vec2>
		{
			for (int j = 0; j < plan->Nodes; ++j)
			{
				for (int i = 0; i < plan->Nodes; ++i)
				{
					if (wanted(plan->IndexOf(i, j)))
					{
						return plan->PointOf(i, j);
					}
				}
			}
			return std::nullopt;
		};
		auto value = [&](const glm::vec2& at, float height) { return Nyx::SampleTerrainValue(terrain, glm::vec3(at.x, height, at.y)); };
		const std::optional<glm::vec2> plains = findNode([&](size_t index)
			{ return plan->Landform[index] == uint8_t(Nyx::ESdfTerrainLandform::Plains) && plan->Channel[index] >= TerrainWorldPlan::MaxReach && plan->Rift[index] >= TerrainWorldPlan::MaxReach && plan->Tunnels[index] == 0.0f
				  && plan->Height[index] == made.Height && std::fabs(plan->PointOf(int(index % size_t(plan->Nodes)), int(index / size_t(plan->Nodes))).x) < 500.0f; });
		Require(plains.has_value(), "There should be open plains");
		Require(value(*plains, made.Height + made.PlainsHeight * 2.0f + 1.0f) > 0.0f && value(*plains, made.Height - made.PlainsHeight * 2.0f - 1.0f) < 0.0f, "The plains should be at their height");
		Require(value(*plains, made.Height - made.Thickness + 5.0f) < 0.0f && value(*plains, made.Height - made.Thickness - made.UndersideDepth - 5.0f) > 0.0f,
			"The landmass should be solid down to its underside and open below it");
		const std::optional<glm::vec2> sea = findNode([&](size_t index) { return plan->Landform[index] == uint8_t(Nyx::ESdfTerrainLandform::Sea) && plan->Height[index] < made.SeaLevel - made.SeaDepth * 0.5f && plan->Rift[index] >= TerrainWorldPlan::MaxReach
				  && plan->EdgeValue(plan->PointOf(int(index % size_t(plan->Nodes)), int(index / size_t(plan->Nodes))).x, plan->PointOf(int(index % size_t(plan->Nodes)), int(index / size_t(plan->Nodes))).y) < -40.0f; });
		Require(sea.has_value(), "There should be sea away from the edge");
		const float seaFloor = plan->At(plan->Height, sea->x, sea->y) - plan->At(plan->PlainsAmplitude, sea->x, sea->y) * 2.0f - 2.0f;
		Require(value(*sea, made.SeaLevel - 1.0f) > 0.0f && value(*sea, seaFloor) < 0.0f, "The sea's floor should be below its level");
		const std::optional<glm::vec2> rift = findNode([&](size_t index) { return plan->Rift[index] < -15.0f && plan->Landform[index] != uint8_t(Nyx::ESdfTerrainLandform::Sea); });
		Require(rift.has_value(), "There should be a rift");
		const float riftHeight = plan->At(plan->Height, rift->x, rift->y);
		Require(value(*rift, riftHeight - 10.0f) > 0.0f && value(*rift, riftHeight - made.Thickness * 0.5f) > 0.0f, "A rift should be open down through the landmass");
		Require(value(glm::vec2(made.Size, 0.0f), made.Height) > 0.0f, "Past the edge should be air");
		const TerrainWorldPlan::River& river = plan->Rivers.front();
		const size_t middle = river.Points.size() / 2;
		Require(value(river.Points[middle], river.Beds[middle] + 1.0f) > 0.0f, "A river's bed should be open above it");
		int open = 0;
		for (int i = 0; i < 2000; ++i)
		{
			const glm::vec2 at(150.0f + sequence.Between(-100.0f, 100.0f), 150.0f + sequence.Between(-100.0f, 100.0f));
			const float height = plan->At(plan->Height, at.x, at.y);
			open += value(at, height - sequence.Between(8.0f, 70.0f)) > 0.0f ? 1 : 0;
		}
		std::cout << "Plan layer tunnels: " << open << " of 2000 points in their band open\n";
		Require(open > 40, "Tunnels should be open where they were pinned");
	}

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

		// A Plan layer's finest chunk on the plains' surface, with tunnels pinned under it
		const SdfTerrainComponent planned = MakePlanned();
		Nyx::SampleTerrainValue(planned, glm::vec3(0.0f));
		const auto planStart = std::chrono::steady_clock::now();
		const Nyx::FieldGrid grid = Nyx::SampleTerrainGrid(planned, glm::vec3(130.0f, planned.Plan.Height - 20.0f, 130.0f), 1.0f, glm::ivec3(Side));
		const double planMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - planStart).count();
		std::cout << "A Plan layer's chunk of " << grid.Values.size() << " samples over tunnels: " << planMilliseconds << " ms\n";
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
		// Each line out at once, so a crash leaves what came before it
		std::cout << std::unitbuf;
		// Logged: a checkpoint's mismatch, a repair
		Nyx::Logger::Get().Init();
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
		TestPlanLayer();
		TestEdits();
		TestEditIndex();
		TestBakes();
		TestSaveFormat();
		TestCheckpoints();
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
