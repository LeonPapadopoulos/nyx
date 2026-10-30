#pragma once

// The Gate 1 test volume of the SDF terrain, shared by NyxTerrainGate and NyxTerrainGateGpu: gentle
// ground and on it, along X, a mound with a 2 m tunnel through it, a 2 m wall, an arch, columns 2 m
// thick and 2 m apart under a mass they hold up, a cliff with a 1 m ledge and a cave going into it,
// and a floating landmass high above with a hanging underside. Each feature has probes: rays that
// must meet it (what is solid) or pass (what is open), and a character's walk where it is walkable.
#include "SdfTerrainComponent.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx::TerrainGate
{
	inline SdfTerrainLayer MakeShape(ESdfTerrainLayerKind kind, const glm::vec3& position, uint32_t material)
	{
		SdfTerrainLayer layer;
		layer.Kind = kind;
		layer.Position = position;
		layer.Material = material;
		layer.Rounding = 0.0f;
		return layer;
	}

	inline SdfTerrainLayer MakeBox(const glm::vec3& position, const glm::vec3& halfSize, uint32_t material, float rounding = 0.0f)
	{
		SdfTerrainLayer layer = MakeShape(ESdfTerrainLayerKind::Box, position, material);
		layer.HalfSize = halfSize;
		layer.Rounding = rounding;
		return layer;
	}

	inline SdfTerrainLayer MakeCapsule(const glm::vec3& from, const glm::vec3& to, float radius, uint32_t material)
	{
		SdfTerrainLayer layer = MakeShape(ESdfTerrainLayerKind::Capsule, from, material);
		layer.End = to;
		layer.Radius = radius;
		return layer;
	}

	inline SdfTerrainLayer Carving(SdfTerrainLayer layer)
	{
		layer.Combine = ESdfTerrainCombine::Carve;
		return layer;
	}

	// How far everything is from whole metres: faces on the samples are only touched, so a feature a
	// cell thick there may not show, which no real content is
	constexpr glm::vec3 Offset{ 0.3f, 0.15f, 0.4f };

	// The cliff's face, along X; the landmass's middle and radius
	constexpr float CliffFace = 40.0f;
	constexpr float IslandX = 0.0f;
	constexpr float IslandZ = 80.0f;
	constexpr float IslandRadius = 30.0f;
	constexpr float IslandTop = 220.0f;

	inline SdfTerrainComponent MakeVolume()
	{
		SdfTerrainComponent terrain;
		terrain.Seed = 11;
		terrain.ViewDistance = 512.0f;
		terrain.Layers.clear();

		// Ground within a tenth of a metre of 0, well under a character's step
		SdfTerrainLayer ground;
		ground.Amplitude = 0.1f;
		ground.Frequency = 0.01f;
		ground.Octaves = 3;
		ground.Material = 0;
		terrain.Layers.push_back(ground);

		// A mound with a tunnel 2 m wide and high through it along Z
		SdfTerrainLayer mound = MakeBox(glm::vec3(-80.0f, 0.0f, 0.0f), glm::vec3(10.0f, 6.0f, 12.0f), 1, 3.0f);
		mound.BlendRadius = 2.0f;
		terrain.Layers.push_back(mound);
		terrain.Layers.push_back(Carving(MakeBox(glm::vec3(-80.0f, 1.0f, 0.0f), glm::vec3(1.0f, 1.0f, 14.0f), 1, 0.25f)));

		// A wall 2 m thick, 20 m long and 6 m high
		terrain.Layers.push_back(MakeBox(glm::vec3(-50.0f, 2.0f, 0.0f), glm::vec3(10.0f, 4.0f, 1.0f), 2));

		// An arch: a wall 4 m thick with an opening 5 m wide and high through it along X
		terrain.Layers.push_back(MakeBox(glm::vec3(-20.0f, 4.0f, 0.0f), glm::vec3(2.0f, 6.0f, 8.0f), 2, 0.5f));
		terrain.Layers.push_back(Carving(MakeCapsule(glm::vec3(-25.0f, 2.5f, 0.0f), glm::vec3(-15.0f, 2.5f, 0.0f), 2.5f, 2)));

		// Five columns 2 m thick, 2 m apart, under a mass they hold up, blended into it like roots
		for (int column = 0; column < 5; ++column)
		{
			const float x = 5.0f + 4.0f * float(column);
			terrain.Layers.push_back(MakeCapsule(glm::vec3(x, -2.0f, 0.0f), glm::vec3(x, 10.0f, 0.0f), 1.0f, 1));
		}
		SdfTerrainLayer mass = MakeBox(glm::vec3(13.0f, 11.5f, 0.0f), glm::vec3(11.0f, 1.5f, 3.0f), 1, 1.0f);
		mass.BlendRadius = 1.0f;
		terrain.Layers.push_back(mass);

		// A cliff of stone, kept sharp, its face at x = 40: a ledge 1 m deep and 2 m thick on it, and a
		// cave going in from a mouth at its foot
		terrain.Layers.push_back(MakeBox(glm::vec3(CliffFace + 15.0f, 13.0f, 0.0f), glm::vec3(15.0f, 15.0f, 20.0f), 2));
		terrain.Layers.push_back(MakeBox(glm::vec3(CliffFace - 0.5f, 12.0f, 6.0f), glm::vec3(0.5f, 1.0f, 4.0f), 2));
		SdfTerrainLayer mouth = Carving(MakeShape(ESdfTerrainLayerKind::Sphere, glm::vec3(CliffFace, 3.5f, -8.0f), 2));
		mouth.Radius = 3.5f;
		mouth.BlendRadius = 1.0f;
		terrain.Layers.push_back(mouth);
		terrain.Layers.push_back(Carving(MakeCapsule(glm::vec3(CliffFace, 2.5f, -8.0f), glm::vec3(CliffFace + 18.0f, 2.5f, -8.0f), 2.5f, 2)));

		// A floating landmass high above, its underside hanging
		SdfTerrainLayer island;
		island.Kind = ESdfTerrainLayerKind::Landmass;
		island.Height = IslandTop;
		island.Depth = 20.0f;
		island.Amplitude = 2.0f;
		island.Center = glm::vec2(IslandX, IslandZ);
		island.Radius = IslandRadius;
		island.UndersideDepth = 25.0f;
		island.EdgeRoughness = 4.0f;
		island.Rounding = 2.0f;
		island.Frequency = 0.02f;
		island.Material = 2;
		terrain.Layers.push_back(island);

		terrain.SharpMaterials = { 2 };
		for (SdfTerrainLayer& layer : terrain.Layers)
		{
			if (layer.Kind == ESdfTerrainLayerKind::Landmass)
			{
				layer.Center += glm::vec2(Offset.x, Offset.z);
				layer.Height += Offset.y;
			}
			else if (layer.Kind != ESdfTerrainLayerKind::Heightfield)
			{
				layer.Position += Offset;
				layer.End += Offset;
			}
		}
		return terrain;
	}

	// A ray through a feature: it must meet it within [HitFrom, HitTo] metres along, or, with
	// bOpen, meet nothing within Length
	struct Probe
	{
		glm::vec3 Origin{ 0.0f };
		glm::vec3 Direction{ 0.0f, -1.0f, 0.0f };
		float Length = 10.0f;
		bool bOpen = false;
		float HitFrom = 0.0f;
		float HitTo = 0.0f;
	};

	// A character's walk: from Start (its feet, above the ground there; dropped from it for a stand)
	// along Move, for at most Seconds. Done when it is at least Distance along Move from where it
	// started, on the ground; or, with bBlocked, if it never gets that far
	struct Walk
	{
		glm::vec3 Start{ 0.0f };
		glm::vec3 Move{ 0.0f };
		float Distance = 0.0f;
		float Seconds = 10.0f;
		bool bBlocked = false;
		// For a stand: where its feet should come to rest, give or take Tolerance
		bool bStand = false;
		float StandHeight = 0.0f;
		float Tolerance = 0.3f;
	};

	struct Feature
	{
		std::string Name;
		// The size that decides whether a level can show it: a solid's thickness or an opening's
		// width, in metres
		float Size = 0.0f;
		std::vector<Probe> Probes;
		std::vector<Walk> Walks;
	};

	inline std::vector<Feature> MakeFeatures()
	{
		std::vector<Feature> features;
		features.push_back({ "2 m tunnel", 2.0f,
			{ Probe{ .Origin = glm::vec3(-80.0f, 1.0f, -9.0f), .Direction = glm::vec3(0.0f, 0.0f, 1.0f), .Length = 18.0f, .bOpen = true },
				Probe{ .Origin = glm::vec3(-80.0f, 8.0f, 0.0f), .Direction = glm::vec3(0.0f, -1.0f, 0.0f), .Length = 8.0f, .HitFrom = 1.5f, .HitTo = 6.5f } },
			{ Walk{ .Start = glm::vec3(-80.0f, 0.0f, -17.0f), .Move = glm::vec3(0.0f, 0.0f, 1.0f), .Distance = 34.0f, .Seconds = 15.0f } } });
		features.push_back({ "2 m wall", 2.0f,
			{ Probe{ .Origin = glm::vec3(-50.0f, 3.0f, -6.0f), .Direction = glm::vec3(0.0f, 0.0f, 1.0f), .Length = 12.0f, .HitFrom = 4.5f, .HitTo = 5.5f },
				Probe{ .Origin = glm::vec3(-44.0f, 4.0f, -6.0f), .Direction = glm::normalize(glm::vec3(-0.3f, -0.1f, 1.0f)), .Length = 12.0f, .HitFrom = 4.0f, .HitTo = 6.5f } },
			{ Walk{ .Start = glm::vec3(-50.0f, 0.0f, -7.0f), .Move = glm::vec3(0.0f, 0.0f, 1.0f), .Distance = 7.0f, .Seconds = 6.0f, .bBlocked = true } } });
		features.push_back({ "Arch opening", 5.0f,
			{ Probe{ .Origin = glm::vec3(-27.0f, 2.5f, 0.0f), .Direction = glm::vec3(1.0f, 0.0f, 0.0f), .Length = 14.0f, .bOpen = true },
				Probe{ .Origin = glm::vec3(-27.0f, 7.5f, 0.0f), .Direction = glm::vec3(1.0f, 0.0f, 0.0f), .Length = 14.0f, .HitFrom = 4.5f, .HitTo = 5.5f } },
			{ Walk{ .Start = glm::vec3(-30.0f, 0.0f, 0.0f), .Move = glm::vec3(1.0f, 0.0f, 0.0f), .Distance = 20.0f, .Seconds = 10.0f } } });
		features.push_back({ "Columns 2 m apart", 2.0f,
			{ Probe{ .Origin = glm::vec3(9.0f, 4.0f, -6.0f), .Direction = glm::vec3(0.0f, 0.0f, 1.0f), .Length = 12.0f, .HitFrom = 4.5f, .HitTo = 5.5f },
				Probe{ .Origin = glm::vec3(11.0f, 4.0f, -6.0f), .Direction = glm::vec3(0.0f, 0.0f, 1.0f), .Length = 12.0f, .bOpen = true },
				Probe{ .Origin = glm::vec3(2.0f, 4.0f, 0.0f), .Direction = glm::vec3(1.0f, 0.0f, 0.0f), .Length = 20.0f, .HitFrom = 1.5f, .HitTo = 2.5f } },
			{ Walk{ .Start = glm::vec3(11.0f, 0.0f, -8.0f), .Move = glm::vec3(0.0f, 0.0f, 1.0f), .Distance = 16.0f, .Seconds = 10.0f } } });
		features.push_back({ "1 m ledge", 2.0f,
			{ Probe{ .Origin = glm::vec3(CliffFace - 0.5f, 20.0f, 6.0f), .Direction = glm::vec3(0.0f, -1.0f, 0.0f), .Length = 25.0f, .HitFrom = 6.5f, .HitTo = 7.5f } },
			{ Walk{ .Start = glm::vec3(CliffFace - 0.5f, 15.0f, 6.0f), .Seconds = 4.0f, .bStand = true, .StandHeight = 13.0f } } });
		features.push_back({ "Cave mouth", 5.0f,
			{ Probe{ .Origin = glm::vec3(CliffFace - 6.0f, 2.5f, -8.0f), .Direction = glm::vec3(1.0f, 0.0f, 0.0f), .Length = 20.0f, .bOpen = true } },
			{ Walk{ .Start = glm::vec3(CliffFace - 6.0f, 0.0f, -8.0f), .Move = glm::vec3(1.0f, 0.0f, 0.0f), .Distance = 14.0f, .Seconds = 10.0f } } });
		features.push_back({ "Hanging island", 20.0f,
			{ Probe{ .Origin = glm::vec3(IslandX, 150.0f, IslandZ), .Direction = glm::vec3(0.0f, 1.0f, 0.0f), .Length = 100.0f, .HitFrom = 20.0f, .HitTo = 60.0f },
				Probe{ .Origin = glm::vec3(IslandX, 260.0f, IslandZ), .Direction = glm::vec3(0.0f, -1.0f, 0.0f), .Length = 100.0f, .HitFrom = 36.0f, .HitTo = 44.0f } },
			{ Walk{ .Start = glm::vec3(IslandX + 5.0f, 230.0f, IslandZ - 5.0f), .Seconds = 4.0f, .bStand = true, .StandHeight = IslandTop, .Tolerance = 3.0f } } });
		for (Feature& feature : features)
		{
			for (Probe& probe : feature.Probes)
			{
				probe.Origin += Offset;
			}
			for (Walk& walk : feature.Walks)
			{
				walk.Start += Offset;
				walk.StandHeight += Offset.y;
			}
		}
		return features;
	}

	// Where the camera goes for the flythrough: from high above down to the features, along them at
	// a person's height, up under the landmass and over it
	inline std::vector<glm::vec3> MakeFlythrough()
	{
		return { glm::vec3(-200.0f, 400.0f, -300.0f), glm::vec3(-100.0f, 60.0f, -60.0f), glm::vec3(-85.0f, 3.0f, -20.0f), glm::vec3(-50.0f, 3.0f, -10.0f),
			glm::vec3(-10.0f, 3.0f, -10.0f), glm::vec3(30.0f, 3.0f, -10.0f), glm::vec3(36.0f, 3.0f, -8.0f), glm::vec3(10.0f, 150.0f, 70.0f),
			glm::vec3(-20.0f, 280.0f, 60.0f), glm::vec3(300.0f, 120.0f, 300.0f) };
	}
}
