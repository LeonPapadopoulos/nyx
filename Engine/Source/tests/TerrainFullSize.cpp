// A full-size world measured: the Gate 3 region's values on a world 20 km wide, more rifts, rivers
// and lakes for its size, a few Place Rules and a Route Check. How long its plan, water, places and
// traversal graph take and how much they hold; the chunks round it from high above, beneath the
// underside, down in a rift and round two characters 10 km apart (how many, how many triangles and
// how much mesh, how long until drawn); how far apart floats are out there, and what that does to a
// vertex on the screen. Pictures of the views with --shots, a scene to walk with --write-scene.
//
//   NyxTerrainFullSize --report <report.md> | --shots <folder> | --write-scene
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
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
#include "TerrainChunks.h"
#include "TerrainGate3Region.h"
#include "TerrainOcclusion.h"
#include "TerrainPlacement.h"
#include "TerrainTraversal.h"
#include "TerrainWater.h"
#include "TerrainWorldPlan.h"
#include "TerrainWorldPlanMap.h"
#include "TestSupport.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <format>
#include <functional>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;
	using Nyx::Tests::Require;

	constexpr float WorldSize = 20480.0f;
	constexpr uint32_t ViewWidth = 1280;
	constexpr uint32_t ViewHeight = 720;

	double SecondsSince(std::chrono::steady_clock::time_point start)
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	}

	// The region's values on a world 20 km wide, its counts grown with it; a few rules and a check
	SdfTerrainComponent MakeWorld()
	{
		SdfTerrainComponent terrain = TerrainGate3::MakeRegion();
		SdfTerrainWorldPlan& plan = terrain.Plan;
		plan.Size = WorldSize;
		plan.EdgeRoughness = 900.0f;
		plan.Rifts = 12;
		plan.RiftLength = 4000.0f;
		plan.Rivers = 24;
		plan.Lakes = 24;
		terrain.ViewDistance = 4096.0f;
		SdfTerrainPlaceRule wide;
		wide.Name = "Wide";
		wide.Count = 24;
		wide.Radius = 20.0f;
		wide.MaxSlope = 8.0f;
		wide.Spacing = 1500.0f;
		SdfTerrainPlaceRule near;
		near.Name = "Near";
		near.Count = 48;
		near.Radius = 5.0f;
		near.MaxSlope = 20.0f;
		near.Spacing = 60.0f;
		near.Near = "Wide";
		near.NearDistance = glm::vec2(30.0f, 200.0f);
		terrain.PlaceRules = { wide, near };
		SdfTerrainRouteCheck check;
		check.From = "Wide";
		check.To = "Wide";
		check.bClimb = true;
		check.bSwim = true;
		terrain.RouteChecks = { check };
		PrepareTerrainWorldPlan(terrain);
		return terrain;
	}

	float GroundBelow(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan, const glm::vec2& at)
	{
		const TerrainWorldPlan::Bounds bounds = plan.GetRange(at - 16.0f, at + 16.0f);
		return FindTerrainGround(terrain, at, bounds.HighHeight + bounds.HighRough + bounds.HighPlains * 2.0f + 4.0f, bounds.LowHeight - 600.0f).value_or(-1000.0f);
	}

	// A rift's floor well inside the landmass: its node nearest the middle with ground under it
	std::optional<glm::vec3> FindRiftFloor(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan)
	{
		std::optional<glm::vec3> best;
		float nearest = std::numeric_limits<float>::max();
		for (int j = 0; j < plan.Nodes; j += 4)
		{
			for (int i = 0; i < plan.Nodes; i += 4)
			{
				const size_t index = plan.IndexOf(i, j);
				const glm::vec2 point = plan.PointOf(i, j);
				if (plan.Landform[index] != uint8_t(ESdfTerrainLandform::Rift) || plan.Rift[index] > -8.0f || glm::length(point) > nearest)
				{
					continue;
				}
				const float floor = GroundBelow(terrain, plan, point);
				if (floor > -900.0f)
				{
					nearest = glm::length(point);
					best = glm::vec3(point.x, floor, point.y);
				}
			}
		}
		return best;
	}

	// Land under a point near `toward`, searched outward on the plan
	glm::vec3 LandNear(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan, const glm::vec2& toward)
	{
		for (float reach = 0.0f; reach < 4000.0f; reach += 64.0f)
		{
			for (int n = 0; n < 16; ++n)
			{
				const float angle = float(n) * 0.3926991f;
				const glm::vec2 point = toward + glm::vec2(std::cos(angle), std::sin(angle)) * reach;
				const glm::ivec2 node = plan.NodeOf(point);
				const uint8_t landform = plan.Landform[plan.IndexOf(node.x, node.y)];
				if (landform == uint8_t(ESdfTerrainLandform::Plains) || landform == uint8_t(ESdfTerrainLandform::Roughland))
				{
					return glm::vec3(point.x, GroundBelow(terrain, plan, point), point.y);
				}
			}
		}
		return glm::vec3(toward.x, 0.0f, toward.y);
	}

	struct View
	{
		std::string Name;
		std::vector<glm::vec3> Viewpoints;
		// Where the camera looks from the first, for its picture
		glm::vec3 LookAt{ 0.0f };
		float ViewDistance = 4096.0f;
	};

	std::vector<View> MakeViews(const SdfTerrainComponent& terrain, const TerrainWorldPlan& plan)
	{
		std::vector<View> views;
		const glm::vec3 middle = LandNear(terrain, plan, glm::vec2(0.0f));
		views.push_back({ "From high above", { middle + glm::vec3(0.0f, 3000.0f, 0.0f) }, middle + glm::vec3(1500.0f, 0.0f, 1500.0f), 6144.0f });
		const float underside = plan.Made.Height - plan.Made.Thickness - plan.Made.UndersideDepth - 150.0f;
		views.push_back({ "Beneath the underside", { glm::vec3(middle.x, underside, middle.z) }, glm::vec3(middle.x + 800.0f, underside + 200.0f, middle.z + 400.0f), 4096.0f });
		if (const std::optional<glm::vec3> rift = FindRiftFloor(terrain, plan))
		{
			views.push_back({ "Down in a rift", { *rift + glm::vec3(0.0f, 6.0f, 0.0f) }, *rift + glm::vec3(300.0f, 60.0f, 0.0f), 4096.0f });
		}
		const glm::vec3 west = LandNear(terrain, plan, glm::vec2(-5000.0f, 0.0f)) + glm::vec3(0.0f, 1.8f, 0.0f);
		const glm::vec3 east = LandNear(terrain, plan, glm::vec2(5000.0f, 0.0f)) + glm::vec3(0.0f, 1.8f, 0.0f);
		views.push_back({ std::format("Two characters {:.0f} m apart", glm::length(east - west)), { west, east }, west + glm::vec3(60.0f, 0.0f, 20.0f), 4096.0f });
		return views;
	}

	struct ViewNumbers
	{
		double Seconds = 0.0;
		uint32_t Chunks = 0;
		uint64_t Triangles = 0;
		double MeshMegabytes = 0.0;
		std::array<uint32_t, TerrainCoarsestLevel - TerrainFinestLevel + 1> ByLevel{};
	};

	ViewNumbers Measure(Registry& world, Entity terrain, const View& view, const std::function<void()>& frame = {})
	{
		world.Get<SdfTerrainComponent>(terrain).ViewDistance = view.ViewDistance;
		const auto start = std::chrono::steady_clock::now();
		for (;;)
		{
			UpdateTerrainChunks(world, view.Viewpoints, JobSystem::Get());
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			if (stats.bSettled && stats.Pending == 0)
			{
				break;
			}
			Require(SecondsSince(start) < 600.0, "The chunks should be made within ten minutes");
			if (frame)
			{
				frame();
			}
			else
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(2));
			}
		}
		ViewNumbers numbers;
		numbers.Seconds = SecondsSince(start);
		const TerrainChunksComponent& chunks = world.Get<TerrainChunksComponent>(terrain);
		numbers.Triangles = chunks.Stats.Triangles;
		numbers.ByLevel = chunks.Stats.ByLevel;
		size_t bytes = 0;
		for (const TerrainChunk& chunk : chunks.Chunks)
		{
			if (chunk.Mesh.Mesh)
			{
				++numbers.Chunks;
				bytes += chunk.Mesh.Mesh->Vertices.size() * sizeof(Vertex) + chunk.Mesh.Mesh->Indices.size() * sizeof(uint32_t);
			}
		}
		numbers.MeshMegabytes = double(bytes) / (1024.0 * 1024.0);
		return numbers;
	}

	// The chunks a view chooses, made straight on the workers, nothing else running: how long they
	// take alone
	ViewNumbers MakeChunksAlone(const SdfTerrainComponent& terrain, const View& view)
	{
		TerrainLevelSettings settings;
		settings.ViewDistance = view.ViewDistance;
		settings.LevelDistance = terrain.LevelDistance;
		settings.FinestLevel = terrain.FinestLevel;
		const auto start = std::chrono::steady_clock::now();
		const TerrainChunkTree tree = SelectTerrainChunks(view.Viewpoints, settings);
		const std::vector<TerrainNeighbourhood> neighbourhoods = GetTerrainNeighbourhoods(tree);
		std::vector<MeshData> meshes(tree.Leaves.size());
		JobGroup group;
		for (size_t index = 0; index < tree.Leaves.size(); ++index)
		{
			JobSystem::Get().Schedule([&, index] { meshes[index] = MakeTerrainChunkMesh(terrain, tree.Leaves[index], neighbourhoods[index]); }, EJobPriority::Normal, &group);
		}
		JobSystem::Get().Wait(group);
		ViewNumbers numbers;
		numbers.Seconds = SecondsSince(start);
		size_t bytes = 0;
		for (const MeshData& mesh : meshes)
		{
			numbers.Chunks += mesh.Indices.empty() ? 0 : 1;
			numbers.Triangles += mesh.Indices.size() / 3;
			bytes += mesh.Vertices.size() * sizeof(Vertex) + mesh.Indices.size() * sizeof(uint32_t);
		}
		numbers.MeshMegabytes = double(bytes) / (1024.0 * 1024.0);
		return numbers;
	}

	// A vertex of a chunk far out drawn as the shaders do, in floats (the chunk's model matrix moving
	// it from round its corner into the world, then the camera's), against doubles: how far off it
	// lands on a 1280 wide picture, at a few distances from a camera that far out
	std::string MeasurePrecision(float distance)
	{
		std::string lines;
		const float spacing = std::nextafter(distance, 2.0f * distance) - distance;
		lines += std::format("Floats {:.0f} m out are {:.2f} mm apart.\n\n| From the camera | Off on the picture |\n|---|---|\n", distance, spacing * 1000.0f);
		const glm::vec3 camera(distance + 0.37f, 21.3f, distance * 0.5f + 0.61f);
		const glm::dvec3 cameraD(camera);
		for (const float away : { 0.5f, 2.0f, 10.0f, 100.0f })
		{
			float worst = 0.0f;
			for (int n = 0; n < 64; ++n)
			{
				const float angle = float(n) * 0.0981748f;
				const glm::vec3 local(std::fmod(float(n) * 0.731f, 32.0f), 1.0f + std::fmod(float(n) * 0.37f, 2.0f), std::fmod(float(n) * 1.37f, 32.0f));
				const glm::vec3 corner = glm::floor((camera + glm::vec3(std::cos(angle) * away, 0.0f, std::sin(angle) * away)) / 32.0f) * 32.0f;
				const glm::vec3 point = corner + local;
				const glm::mat4 model = glm::translate(glm::mat4(1.0f), corner);
				const glm::vec3 lookAt = point;
				const glm::mat4 viewProjection = glm::perspective(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 8000.0f) * glm::lookAt(camera, lookAt + glm::vec3(0.3f, 0.0f, 0.2f), glm::vec3(0.0f, 1.0f, 0.0f));
				const glm::dmat4 viewProjectionD = glm::perspective(glm::radians(60.0), 16.0 / 9.0, 0.1, 8000.0) * glm::lookAt(cameraD, glm::dvec3(lookAt) + glm::dvec3(0.3, 0.0, 0.2), glm::dvec3(0.0, 1.0, 0.0));
				const glm::vec4 clip = viewProjection * (model * glm::vec4(local, 1.0f));
				const glm::dvec4 clipD = viewProjectionD * glm::dvec4(glm::dvec3(corner) + glm::dvec3(local), 1.0);
				const glm::dvec2 screen = glm::dvec2(clip.x / clip.w, clip.y / clip.w);
				const glm::dvec2 screenD = glm::dvec2(clipD.x / clipD.w, clipD.y / clipD.w);
				worst = std::max(worst, float(glm::length(screen - screenD) * 0.5 * double(ViewWidth)));
			}
			lines += std::format("| {:.1f} m | {:.2f} px |\n", away, worst);
		}
		return lines;
	}

	void WriteReport(const std::filesystem::path& path)
	{
		std::string report = "# A full-size world\n\n";
		auto line = [&](const std::string& text)
		{
			std::cout << text << "\n";
			report += text + "\n";
		};

		// The plan, water, places and traversal, each on the workers
		SdfTerrainComponent terrain = MakeWorld();
		auto start = std::chrono::steady_clock::now();
		const std::shared_ptr<const TerrainWorldPlan> plan = MakeTerrainWorldPlan(terrain.Seed, terrain.Plan);
		const double planSeconds = SecondsSince(start);
		terrain.Planned = plan;
		const size_t planBytes = (plan->Height.size() + plan->PlainsAmplitude.size() + plan->RoughAmplitude.size() + plan->Rift.size() + plan->Channel.size()
									 + plan->ChannelBed.size() + plan->Tunnels.size()) * sizeof(float) + plan->Landform.size();
		start = std::chrono::steady_clock::now();
		const TerrainWater water = MakeTerrainWater(terrain, nullptr, nullptr, nullptr, &JobSystem::Get());
		const double waterSeconds = SecondsSince(start);
		const size_t waterBytes = water.Ground.size() * sizeof(float) + water.Body.size() + water.Level.size() * sizeof(float);
		start = std::chrono::steady_clock::now();
		const TerrainPlacement placement = PlaceOnTerrain(terrain, &JobSystem::Get());
		const double placesSeconds = SecondsSince(start);
		start = std::chrono::steady_clock::now();
		const TerrainTraversal traversal = BuildTerrainTraversal(terrain, &water, &JobSystem::Get());
		const double traversalSeconds = SecondsSince(start);
		start = std::chrono::steady_clock::now();
		const std::vector<TerrainRouteResult> routes = CheckTerrainRoutes(terrain, placement, traversal);
		const double checksSeconds = SecondsSince(start);
		const size_t traversalBytes = traversal.ColumnFirst.size() * 4 + traversal.FloorHeights.size() * 4 + traversal.Wet.size() + traversal.EdgeFirst.size() * 4
			+ traversal.EdgeTargets.size() * 4 + traversal.EdgeTags.size();
		const TerrainPlanShares shares = GetTerrainWorldPlanShares(*plan);

		line(std::format("{:.0f} m wide, seed {}, {} workers.\n", WorldSize, terrain.Seed, JobSystem::Get().GetWorkerCount()));
		line("## Making it\n\n| Part | Time | Holds |\n|---|---|---|");
		line(std::format("| Plan, {} x {} nodes: sea {:.0f}%, rough {:.0f}%, rifts {:.1f}%, tunnels {:.0f}%, {} rivers, {} lakes | {:.2f} s | {:.0f} MB |", plan->Nodes,
			plan->Nodes, shares.Sea * 100.0f, shares.Roughland * 100.0f, shares.Rift * 100.0f, shares.Tunnels * 100.0f, shares.Rivers, shares.Lakes, planSeconds,
			double(planBytes) / 1048576.0));
		line(std::format("| Water, {} x {} cells of {:.0f} m, {} river stretches, {} falls | {:.2f} s | {:.0f} MB |", water.Cells, water.Cells, TerrainWater::CellSize,
			water.Rivers.size(), water.Falls.size(), waterSeconds, double(waterBytes) / 1048576.0));
		uint32_t found = 0;
		uint32_t asked = 0;
		for (size_t index = 0; index < placement.Rules.size(); ++index)
		{
			found += placement.Rules[index].Found;
			asked += terrain.PlaceRules[index].Count;
		}
		line(std::format("| Places, {} of {} found | {:.2f} s | |", found, asked, placesSeconds));
		line(std::format("| Traversal, {} x {} columns {:.1f} m apart, {} floors, {} edges | {:.2f} s | {:.0f} MB |", traversal.Columns, traversal.Columns,
			traversal.Values.Spacing, traversal.Floors(), traversal.EdgeTargets.size(), traversalSeconds, double(traversalBytes) / 1048576.0));
		line(std::format("| Route Check: {} ({}) | {:.2f} s | |", routes.empty() ? "none" : (routes[0].bPassed ? "passed" : "failed"), routes.empty() ? "" : routes[0].Why,
			checksSeconds));
		line(std::format("| All of it | {:.2f} s | |\n", planSeconds + waterSeconds + placesSeconds + traversalSeconds + checksSeconds));

		// The views, each from nothing made
		line("## Views\n\nThe chunks round each view: those chosen made straight on the workers with nothing else running (*Made alone*), and in a world");
		line("from none until all are drawn, its water worked out on the same workers alongside (*In a world*).\n");
		line("| View | Chunks | By level, finest first | Triangles | Mesh | Made alone | In a world |\n|---|---|---|---|---|---|---|");
		for (const View& view : MakeViews(terrain, *plan))
		{
			Registry world;
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, TransformComponent{});
			SdfTerrainComponent values = terrain;
			values.PlaceRules.clear();
			values.RouteChecks.clear();
			world.Add<SdfTerrainComponent>(entity, values);
			const ViewNumbers alone = MakeChunksAlone(values, view);
			const ViewNumbers numbers = Measure(world, entity, view);
			std::string levels;
			for (size_t level = 0; level < numbers.ByLevel.size(); ++level)
			{
				if (numbers.ByLevel[level] != 0 || (level > 0 && level + 1 < numbers.ByLevel.size() && numbers.ByLevel[level + 1] != 0))
				{
					levels += (levels.empty() ? "" : ", ") + std::to_string(numbers.ByLevel[level]);
				}
			}
			line(std::format("| {} | {} | {} | {:.2f} M | {:.0f} MB | {:.2f} s | {:.2f} s |", view.Name, numbers.Chunks, levels, double(numbers.Triangles) / 1e6,
				numbers.MeshMegabytes, alone.Seconds, numbers.Seconds));
			RemoveTerrainChunks(world, entity);
			JobSystem::Get().Wait(JobGroup{});
		}

		line("\n## Precision far out\n");
		line(MeasurePrecision(WorldSize * 0.5f));
		std::ofstream file(path, std::ios::binary);
		file << report;
		std::cout << "Wrote " << path.string() << "\n";
	}

	void WriteShots(const std::filesystem::path& folder)
	{
		std::filesystem::create_directories(folder);
		const SdfTerrainComponent terrain = MakeWorld();
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(terrain);
		SceneRenderer gpu("NyxTerrainFullSize", 1);
		Registry world;
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, TransformComponent{});
		world.Add<CameraComponent>(camera, CameraComponent{ .FarPlane = 12000.0f });
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-35.0f, 40.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity air = world.CreateEntity();
		EnvironmentComponent environment;
		environment.FogDensity = 0.0002f;
		environment.FogHeightFalloff = 0.004f;
		world.Add<EnvironmentComponent>(air, environment);
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		SdfTerrainComponent values = terrain;
		values.PlaceRules.clear();
		values.RouteChecks.clear();
		world.Add<SdfTerrainComponent>(entity, values);
		MeshRendererComponent meshRenderer;
		meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		int n = 0;
		for (const View& shot : MakeViews(terrain, *plan))
		{
			TransformComponent& transform = world.Get<TransformComponent>(camera);
			transform.Position = shot.Viewpoints.front();
			transform.Rotation = glm::quatLookAt(glm::normalize(shot.LookAt - transform.Position), glm::vec3(0.0f, 1.0f, 0.0f));
			const ViewNumbers numbers = Measure(world, entity, shot, [&] { gpu.DrawFrames(1); });
			gpu.DrawFrames(6);
			RgbaImage image;
			Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The view can't be read back");
			std::string name = shot.Name;
			std::replace(name.begin(), name.end(), ' ', '-');
			const std::filesystem::path path = folder / (std::to_string(++n) + "-" + name + ".png");
			std::string error;
			Require(SavePng(path, image, error), "The picture can't be saved: " + error);
			std::cout << "Wrote " << path.string() << " (" << numbers.Chunks << " chunks in " << numbers.Seconds << " s)\n";
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	// The world with a character on land 9 km out from the middle, an orbit camera after it, a sun
	// and thin fog
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
		SdfTerrainComponent values = MakeWorld();
		const std::shared_ptr<const TerrainWorldPlan> plan = GetTerrainWorldPlan(values);
		values.Planned.reset();
		const Entity terrain = addNamed("Terrain", TransformComponent{});
		world.Add<SdfTerrainComponent>(terrain, values);
		world.Add<MeshRendererComponent>(terrain, MeshRendererComponent{ .Material = AssetReference{ .Type = "Material", .Path = "Materials/VertexColor.nyxmat" }, .bVisible = true });
		const glm::vec3 feet = LandNear(values, *plan, glm::vec2(9000.0f, 0.0f)) + glm::vec3(0.0f, 1.0f, 0.0f);
		const Entity character = addNamed("Character", TransformComponent{ .Position = feet });
		world.Add<MeshRendererComponent>(character, MeshRendererComponent{ .Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Capsule.nyxmesh" },
			.Material = AssetReference{ .Type = "Material", .Path = "Materials/Untextured.nyxmat" }, .bVisible = true });
		world.Add<CharacterComponent>(character, CharacterComponent{});
		const glm::vec3 behind = feet + glm::vec3(-8.0f, 3.0f, 0.0f);
		const Entity camera = addNamed("Camera", TransformComponent{ .Position = behind, .Rotation = glm::quatLookAt(glm::normalize(feet - behind), glm::vec3(0.0f, 1.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{ .FarPlane = 12000.0f });
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });
		const Entity sun = addNamed("Sun", TransformComponent{ .Position = feet + glm::vec3(0.0f, 50.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-35.0f, 40.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		EnvironmentComponent environment;
		environment.FogDensity = 0.0002f;
		environment.FogHeightFalloff = 0.004f;
		const Entity air = addNamed("Air", TransformComponent{});
		world.Add<EnvironmentComponent>(air, environment);
		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / "Scenes/SdfTerrainFullWorld.nyxscene";
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
		if (argc > 2 && std::strcmp(argv[1], "--report") == 0)
		{
			WriteReport(argv[2]);
			return 0;
		}
		if (argc > 2 && std::strcmp(argv[1], "--shots") == 0)
		{
			WriteShots(argv[2]);
			return 0;
		}
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		std::cout << "NyxTerrainFullSize --report <report.md> | --shots <folder> | --write-scene\n";
		return 1;
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
}
