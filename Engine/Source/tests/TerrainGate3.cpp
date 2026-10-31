// Gate 3 of the SDF terrain: whether plains, rough land, rifts, the sea, the underside and tunnels fit
// together in one world, and whether the world plan steers them. The test region
// (TerrainGate3Region.h) is a world plan of 2 km made into ground, with its water and flat colours.
// Needs a Vulkan device for its pictures; its window stays hidden. Not a test of ctest's.
//
// NyxTerrainGate3 --report <folder>   maps of a few seeds and plans, their numbers, and pictures of
//                                     the region and of each from above, with report.md
// NyxTerrainGate3 --shots <folder> [seed]  the region's pictures alone
// NyxTerrainGate3 --write-scene       writes Assets/Scenes/SdfTerrainWorld.nyxscene, the region with
//                                     a character to walk
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "OrbitCameraComponent.h"
#include "Paths.h"
#include "SceneSerializer.h"
#include "SdfTerrain.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TerrainGate3Region.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"
#include "TerrainWorldPlanMap.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 1280;
	constexpr uint32_t ViewHeight = 720;

	struct Viewpoint
	{
		std::string Name;
		glm::vec3 Position;
		glm::vec3 LookAt;
	};

	// Thin fog, so the far side and the low ground show
	EnvironmentComponent MakeAir()
	{
		EnvironmentComponent environment;
		environment.FogDensity = 0.0005f;
		environment.FogHeightFalloff = 0.004f;
		return environment;
	}

	struct Scene
	{
		Registry World;
		Entity Camera;
		Entity Terrain;
	};

	void MakeScene(Scene& scene, SceneRenderer& gpu, const SdfTerrainComponent& values)
	{
		scene.Camera = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(scene.Camera, TransformComponent{});
		scene.World.Add<CameraComponent>(scene.Camera, CameraComponent{ .FarPlane = 8000.0f });
		const Entity sun = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-35.0f, 40.0f, 0.0f))) });
		scene.World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity air = scene.World.CreateEntity();
		scene.World.Add<EnvironmentComponent>(air, MakeAir());
		scene.Terrain = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(scene.Terrain, TransformComponent{});
		scene.World.Add<SdfTerrainComponent>(scene.Terrain, values);
		MeshRendererComponent meshRenderer;
		meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
		scene.World.Add<MeshRendererComponent>(scene.Terrain, meshRenderer);
	}

	// Drawn from the viewpoint once its chunks and water are made, saved; how long they took
	double Shoot(Scene& scene, SceneRenderer& gpu, uint64_t view, const Viewpoint& viewpoint, const std::filesystem::path& path)
	{
		TransformComponent& transform = scene.World.Get<TransformComponent>(scene.Camera);
		transform.Position = viewpoint.Position;
		transform.Rotation = glm::quatLookAt(glm::normalize(viewpoint.LookAt - viewpoint.Position), glm::vec3(0.0f, 1.0f, 0.0f));
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			UpdateTerrainChunks(scene.World, { &viewpoint.Position, 1 }, JobSystem::Get());
			const TerrainChunksStats& stats = scene.World.Get<TerrainChunksComponent>(scene.Terrain).Stats;
			const bool bWater = scene.World.Has<TerrainWaterComponent>(scene.Terrain) && scene.World.Get<TerrainWaterComponent>(scene.Terrain).Made > 0;
			if (stats.bSettled && stats.Pending == 0 && bWater)
			{
				break;
			}
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(180), "The chunks should be made within three minutes");
			gpu.DrawFrames(1);
		}
		const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		gpu.DrawFrames(6);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The view can't be read back");
		std::string error;
		Require(SavePng(path, image, error), "The picture can't be saved: " + error);
		std::cout << "Wrote " << path.string() << " (" << viewpoint.Name << ", made in " << seconds << " s)\n";
		return seconds;
	}

	// Where the ground is under a point: the first surface down from high above
	float GroundBelow(const SdfTerrainComponent& terrain, const glm::vec2& at)
	{
		const std::optional<TerrainRayHit> hit = RayCastTerrain(terrain, glm::vec3(at.x, 400.0f, at.y), glm::vec3(0.0f, -1.0f, 0.0f), 1000.0f);
		return hit ? hit->Point.y : -1000.0f;
	}

	// A rift's inland end and its mouth, if the plan has one
	std::optional<std::pair<glm::vec2, glm::vec2>> FindRift(const TerrainWorldPlan& plan)
	{
		std::optional<glm::vec2> inland;
		std::optional<glm::vec2> mouth;
		float inlandMost = 0.0f;
		float mouthLeast = 0.0f;
		for (int j = 0; j < plan.Nodes; ++j)
		{
			for (int i = 0; i < plan.Nodes; ++i)
			{
				const size_t index = plan.IndexOf(i, j);
				if (plan.Rift[index] < -5.0f && plan.Landform[index] != TerrainWorldPlan::NotLand)
				{
					const glm::vec2 point = plan.PointOf(i, j);
					const float depth = -plan.EdgeValue(point.x, point.y);
					if (!inland || depth > inlandMost)
					{
						inlandMost = depth;
						inland = point;
					}
					if (!mouth || depth < mouthLeast)
					{
						mouthLeast = depth;
						mouth = point;
					}
				}
			}
		}
		if (!inland || !mouth)
		{
			return std::nullopt;
		}
		return std::make_pair(*inland, *mouth);
	}

	std::vector<Viewpoint> PlanViewpoints(const SdfTerrainComponent& terrain)
	{
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		const SdfTerrainWorldPlan& made = terrain.Plan;
		std::vector<Viewpoint> viewpoints{ { "high above", glm::vec3(0.0f, 1100.0f, 1500.0f), glm::vec3(0.0f, 0.0f, 0.0f) } };

		// Along a rift, from above its inland end looking out to the edge
		if (const auto rift = FindRift(*plan))
		{
			const glm::vec2 back = rift->first - glm::normalize(rift->second - rift->first) * 80.0f;
			viewpoints.push_back({ "along a rift", glm::vec3(back.x, std::max(GroundBelow(terrain, back), made.Height) + 50.0f, back.y),
				glm::vec3(rift->second.x, made.Height - 80.0f, rift->second.y) });
		}

		// At a shore: a sea node away from the edge with plains inland of it, looking inland from just
		// above the sea
		bool bShore = false;
		for (int j = 1; j + 1 < plan->Nodes && !bShore; ++j)
		{
			for (int i = 1; i + 1 < plan->Nodes && !bShore; ++i)
			{
				const glm::vec2 point = plan->PointOf(i, j);
				if (plan->Landform[plan->IndexOf(i, j)] != uint8_t(ESdfTerrainLandform::Sea) || plan->EdgeValue(point.x, point.y) > -150.0f)
				{
					continue;
				}
				const glm::vec2 land = point + glm::normalize(-point) * 150.0f;
				const glm::ivec2 node = plan->NodeOf(land);
				if (plan->Landform[plan->IndexOf(node.x, node.y)] == uint8_t(ESdfTerrainLandform::Plains) && plan->Rift[plan->IndexOf(node.x, node.y)] > 50.0f)
				{
					viewpoints.push_back({ "at a shore", glm::vec3(point.x, made.SeaLevel + 6.0f, point.y), glm::vec3(land.x, made.Height, land.y) });
					bShore = true;
				}
			}
		}

		// In a tunnel: a place in its band with room round it and a way on along it
		bool bTunnel = false;
		const glm::vec2 ways[8] = { { 1.0f, 0.0f }, { -1.0f, 0.0f }, { 0.0f, 1.0f }, { 0.0f, -1.0f }, { 0.7071f, 0.7071f }, { -0.7071f, 0.7071f }, { 0.7071f, -0.7071f },
			{ -0.7071f, -0.7071f } };
		for (int j = 2; j + 2 < plan->Nodes && !bTunnel; j += 2)
		{
			for (int i = 2; i + 2 < plan->Nodes && !bTunnel; i += 2)
			{
				const size_t index = plan->IndexOf(i, j);
				if (plan->Tunnels[index] < 0.95f || plan->Landform[index] != uint8_t(ESdfTerrainLandform::Plains))
				{
					continue;
				}
				const glm::vec2 at = plan->PointOf(i, j);
				for (float down = 12.0f; down < 60.0f && !bTunnel; down += 4.0f)
				{
					const glm::vec3 point(at.x, plan->Height[index] - down, at.y);
					if (SampleTerrainValue(terrain, point) < 2.0f)
					{
						continue;
					}
					for (const glm::vec2& way : ways)
					{
						bool bOpen = true;
						for (float along = 4.0f; along <= 24.0f && bOpen; along += 4.0f)
						{
							bOpen = SampleTerrainValue(terrain, point + glm::vec3(way.x, 0.0f, way.y) * along) > 0.5f;
						}
						if (bOpen)
						{
							viewpoints.push_back({ "in a tunnel", point, point + glm::vec3(way.x, -0.1f, way.y) * 30.0f });
							bTunnel = true;
							break;
						}
					}
				}
			}
		}

		// Under the edge, looking up at the underside and in under it
		const float underside = made.Height - made.Thickness - made.UndersideDepth;
		viewpoints.push_back({ "under the edge", glm::vec3(plan->Radius + made.EdgeRoughness + 150.0f, underside - 150.0f, 0.0f), glm::vec3(plan->Radius * 0.5f, underside + 20.0f, 0.0f) });
		return viewpoints;
	}

	// The plans the report compares: seeds of the defaults, then more of one kind, then pinned
	struct Variant
	{
		std::string Name;
		std::string Says;
		uint32_t Seed = 2;
		SdfTerrainWorldPlan Made;
	};

	std::vector<Variant> MakeVariants()
	{
		std::vector<Variant> variants;
		for (const uint32_t seed : { 2u, 3u, 5u })
		{
			variants.push_back({ "seed-" + std::to_string(seed), "The defaults, seed " + std::to_string(seed), seed, {} });
		}
		Variant sea{ "more-sea", "Sea 0.45: bays become an inner sea", 2, {} };
		sea.Made.Sea = 0.45f;
		variants.push_back(sea);
		Variant rough{ "rough-and-rifted", "Rough land 0.7, 4 rifts, tunnels 0.8", 2, {} };
		rough.Made.Roughland = 0.7f;
		rough.Made.Rifts = 4;
		rough.Made.Tunnels = 0.8f;
		variants.push_back(rough);
		Variant flat{ "plains", "Rough land 0.1, sea 0.1, no rifts", 2, {} };
		flat.Made.Roughland = 0.1f;
		flat.Made.Sea = 0.1f;
		flat.Made.Rifts = 0;
		variants.push_back(flat);
		Variant pinned{ "pinned", "Pins: sea in the middle, a rift through the north-west, rough land in the south-east", 2, {} };
		pinned.Made.Pins = { SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Sea, .Position = glm::vec2(0.0f, 0.0f), .Radius = 300.0f },
			SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Rift, .Position = glm::vec2(-350.0f, -300.0f) },
			SdfTerrainPlanPin{ .Landform = ESdfTerrainLandform::Roughland, .Position = glm::vec2(400.0f, 350.0f), .Radius = 350.0f } };
		variants.push_back(pinned);
		return variants;
	}

	std::string Percent(size_t of, size_t in)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(0) << (in > 0 ? 100.0 * double(of) / double(in) : 0.0) << "%";
		return text.str();
	}

	void WriteReport(const std::filesystem::path& folder)
	{
		std::filesystem::create_directories(folder);
		std::ostringstream report;
		report << "# SDF terrain Gate 3\n\n"
			   << "Plains, rough land, rifts, the sea, the underside and tunnels in one world, from a world plan, and how its values and pins steer it. "
			   << "The region is 2 km wide; maps a pixel every 16 m.\n\n"
			   << "| Plan | Map | Plan made | Sea | Rough | Rift | Tunnels | Rivers | Lakes | Water made | Falls |\n|---|---|---|---|---|---|---|---|---|---|---|\n";
		const std::vector<Variant> variants = MakeVariants();
		for (const Variant& variant : variants)
		{
			const auto planStart = std::chrono::steady_clock::now();
			const std::shared_ptr<const TerrainWorldPlan> plan = MakeTerrainWorldPlan(variant.Seed, variant.Made);
			const double planMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - planStart).count();
			std::string error;
			Require(SavePng(folder / ("map-" + variant.Name + ".png"), Tests::DrawTerrainWorldPlanMap(*plan), error), "The map can't be saved: " + error);
			size_t land = 0;
			std::array<size_t, 5> kinds{};
			size_t tunnels = 0;
			for (size_t index = 0; index < plan->Landform.size(); ++index)
			{
				if (plan->Landform[index] == TerrainWorldPlan::NotLand)
				{
					continue;
				}
				++land;
				++kinds[plan->Landform[index]];
				tunnels += plan->Tunnels[index] > 0.5f ? 1 : 0;
			}
			SdfTerrainComponent terrain = TerrainGate3::MakeRegion();
			terrain.Seed = variant.Seed;
			terrain.Plan = variant.Made;
			PrepareTerrainWorldPlan(terrain);
			const auto waterStart = std::chrono::steady_clock::now();
			const TerrainWater water = MakeTerrainWater(terrain, nullptr, nullptr, nullptr, &JobSystem::Get());
			const double waterMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - waterStart).count();
			report << std::fixed << std::setprecision(0) << "| " << variant.Says << " | ![](map-" << variant.Name << ".png) | " << planMilliseconds << " ms | "
				   << Percent(kinds[size_t(ESdfTerrainLandform::Sea)], land) << " | " << Percent(kinds[size_t(ESdfTerrainLandform::Roughland)], land) << " | "
				   << Percent(kinds[size_t(ESdfTerrainLandform::Rift)], land) << " | " << Percent(tunnels, land) << " | " << plan->Rivers.size() << " | " << plan->Lakes.size()
				   << " | " << waterMilliseconds << " ms | " << water.Falls.size() << " |\n";
			std::cout << "Planned " << variant.Name << "\n";
		}
		report << "\nShares of the landmass's nodes; tunnels where they are more than half open. Water made on "
			   << JobSystem::Get().GetWorkerCount() << " workers.\n\n## The region\n\n";

		SceneRenderer gpu("NyxTerrainGate3", 1);
		Scene scene;
		MakeScene(scene, gpu, TerrainGate3::MakeRegion());
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		int n = 0;
		for (const Viewpoint& viewpoint : PlanViewpoints(scene.World.Get<SdfTerrainComponent>(scene.Terrain)))
		{
			std::string name = viewpoint.Name;
			std::replace(name.begin(), name.end(), ' ', '-');
			const std::string file = "region-" + std::to_string(++n) + "-" + name + ".png";
			const double seconds = Shoot(scene, gpu, view, viewpoint, folder / file);
			report << "**" << viewpoint.Name << "** (chunks and water made in " << std::fixed << std::setprecision(1) << seconds << " s from nothing)\n\n![](" << file << ")\n\n";
		}

		report << "## Each plan from above\n\n";
		for (const Variant& variant : variants)
		{
			SdfTerrainComponent& terrain = scene.World.Get<SdfTerrainComponent>(scene.Terrain);
			terrain.Seed = variant.Seed;
			terrain.Plan = variant.Made;
			const std::string file = "above-" + variant.Name + ".png";
			Shoot(scene, gpu, view, { variant.Name, glm::vec3(0.0f, 1100.0f, 1500.0f), glm::vec3(0.0f) }, folder / file);
			report << "**" << variant.Says << "**\n\n![](" << file << ")\n\n";
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);

		std::ofstream(folder / "report.md") << report.str();
		std::cout << "\n" << report.str();
	}

	void WriteShots(const std::filesystem::path& folder, std::optional<uint32_t> seed)
	{
		SdfTerrainComponent values = TerrainGate3::MakeRegion();
		if (seed)
		{
			values.Seed = *seed;
		}
		SceneRenderer gpu("NyxTerrainGate3", 1);
		Scene scene;
		MakeScene(scene, gpu, values);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&scene.World);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		int n = 0;
		for (const Viewpoint& viewpoint : PlanViewpoints(values))
		{
			std::string name = viewpoint.Name;
			std::replace(name.begin(), name.end(), ' ', '-');
			Shoot(scene, gpu, view, viewpoint, folder / (std::to_string(++n) + "-" + name + ".png"));
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// The region with a character on the plains beside a rift's inland end, an orbit camera after it,
	// a sun and thin fog
	void WriteScene()
	{
		Registry world;
		auto addNamed = [&](const std::string& name, const TransformComponent& transform)
		{
			const Entity entity = world.CreateEntity();
			world.Add<NameComponent>(entity, NameComponent{ name });
			world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
			world.Add<TransformComponent>(entity, transform);
			return entity;
		};
		const SdfTerrainComponent values = TerrainGate3::MakeRegion();
		const Entity terrain = addNamed("Terrain", TransformComponent{});
		world.Add<SdfTerrainComponent>(terrain, values);
		world.Add<MeshRendererComponent>(terrain, MeshRendererComponent{ .Material = AssetReference{ .Type = "Material", .Path = "Materials/VertexColor.nyxmat" }, .bVisible = true });

		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(values);
		const auto rift = FindRift(*plan);
		Require(rift.has_value(), "The region should have a rift");
		const glm::vec2 inward = glm::normalize(rift->first - rift->second);
		const glm::vec2 standing = rift->first + inward * 70.0f;
		const glm::vec3 feet(standing.x, GroundBelow(values, standing) + 1.0f, standing.y);
		const Entity character = addNamed("Character", TransformComponent{ .Position = feet });
		world.Add<MeshRendererComponent>(character, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Capsule.nyxmesh" },
			.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" }, .bVisible = true });
		world.Add<CharacterComponent>(character, CharacterComponent{});

		const glm::vec3 behind = feet + glm::vec3(inward.x, 0.0f, inward.y) * 8.0f + glm::vec3(0.0f, 3.0f, 0.0f);
		const Entity camera = addNamed("Camera", TransformComponent{ .Position = behind, .Rotation = glm::quatLookAt(glm::normalize(feet - behind), glm::vec3(0.0f, 1.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{ .FarPlane = 8000.0f });
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });

		const Entity sun = addNamed("Sun", TransformComponent{ .Position = feet + glm::vec3(0.0f, 50.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-35.0f, 40.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity air = addNamed("Air", TransformComponent{});
		world.Add<EnvironmentComponent>(air, MakeAir());

		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / "Scenes/SdfTerrainWorld.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		std::cout << "Wrote " << path.string() << "\n";
	}
}

int main(int argc, char** argv)
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		if (argc > 2 && std::strcmp(argv[1], "--report") == 0)
		{
			WriteReport(argv[2]);
			return 0;
		}
		if (argc > 2 && std::strcmp(argv[1], "--shots") == 0)
		{
			WriteShots(argv[2], argc > 3 ? std::optional<uint32_t>(uint32_t(std::stoul(argv[3]))) : std::nullopt);
			return 0;
		}
		std::cout << "NyxTerrainGate3 --report <folder> | --shots <folder> [seed] | --write-scene\n";
		return 1;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
}
