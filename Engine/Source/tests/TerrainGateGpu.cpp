// Gate 1 of the SDF terrain on the GPU: the test volume (TerrainGateVolume.h) drawn at 1920 x 1080
// with shadows, from along the features, from high above and from under the landmass, with each
// mesher at 1 m and half-metre cells. The terrain's GPU time is its scene and shadow passes' time
// against the same view with the terrain hidden, averaged over 60 frames; printed as Markdown with
// the triangles and draws. Needs a Vulkan device; its window stays hidden. Not a test of ctest's.
#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "JobSystem.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "SdfTerrainComponent.h"
#include "TerrainChunks.h"
#include "TerrainGateVolume.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 1920;
	constexpr uint32_t ViewHeight = 1080;

	std::string Fixed(double value, int digits = 2)
	{
		std::ostringstream text;
		text << std::fixed << std::setprecision(digits) << value;
		return text.str();
	}

	struct Viewpoint
	{
		const char* Name;
		glm::vec3 Position;
		glm::vec3 LookAt;
	};

	struct Measured
	{
		double Scene = 0.0;
		double Shadows = 0.0;
		uint64_t Triangles = 0;
		uint64_t ShadowTriangles = 0;
		uint32_t Draws = 0;
		uint32_t ShadowDraws = 0;
	};

	// The view's averages over `frames` frames, once their GPU work is done
	Measured MeasureFrames(SceneRenderer& gpu, uint64_t view, int frames)
	{
		gpu.DrawFrames(4);
		Measured measured;
		for (int frame = 0; frame < frames; ++frame)
		{
			gpu.DrawFrames(1);
			const SceneViewFrameStats* stats = gpu.Renderer.GetSceneViewFrameStats(view);
			Require(stats != nullptr && stats->bDrawn, "The view should be drawn");
			measured.Scene += stats->Gpu.Scene;
			measured.Shadows += stats->Gpu.Shadows;
			measured.Triangles = stats->Scene.Triangles;
			measured.ShadowTriangles = stats->Shadows.Triangles;
			measured.Draws = stats->Scene.Draws;
			measured.ShadowDraws = stats->Shadows.Draws;
		}
		measured.Scene /= frames;
		measured.Shadows /= frames;
		return measured;
	}

	void Run(SceneRenderer& gpu, ESdfTerrainMesher mesher, int finestLevel, std::ostringstream& table, double& worst)
	{
		Registry world;
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, TransformComponent{});
		world.Add<CameraComponent>(camera, CameraComponent{});
		const Entity sun = world.CreateEntity();
		world.Add<TransformComponent>(sun, TransformComponent{ .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		const Entity terrain = world.CreateEntity();
		world.Add<TransformComponent>(terrain, TransformComponent{});
		SdfTerrainComponent values = TerrainGate::MakeVolume();
		values.Mesher = mesher;
		values.FinestLevel = finestLevel;
		world.Add<SdfTerrainComponent>(terrain, values);
		MeshRendererComponent meshRenderer;
		meshRenderer.MaterialAsset = gpu.Renderer.GetMaterial("Materials/VertexColor.nyxmat");
		world.Add<MeshRendererComponent>(terrain, meshRenderer);

		Nyx::IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, Nyx::EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		const Viewpoint viewpoints[] = { { "Along the features", glm::vec3(-100.0f, 4.0f, -30.0f), glm::vec3(0.0f, 2.0f, 0.0f) },
			{ "High above", glm::vec3(-150.0f, 250.0f, -200.0f), glm::vec3(0.0f, 0.0f, 0.0f) },
			{ "Under the landmass", glm::vec3(-30.0f, 120.0f, 40.0f), glm::vec3(0.0f, 190.0f, 80.0f) } };
		const std::string name = std::string(mesher == ESdfTerrainMesher::Dual ? "Dual" : "Marching cubes") + (finestLevel < 0 ? ", 0.5 m" : ", 1 m");
		for (const Viewpoint& viewpoint : viewpoints)
		{
			TransformComponent& transform = world.Get<TransformComponent>(camera);
			transform.Position = viewpoint.Position;
			transform.Rotation = glm::quatLookAt(glm::normalize(viewpoint.LookAt - viewpoint.Position), glm::vec3(0.0f, 1.0f, 0.0f));
			const auto start = std::chrono::steady_clock::now();
			for (;;)
			{
				UpdateTerrainChunks(world, { &viewpoint.Position, 1 }, JobSystem::Get());
				const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
				if (stats.bSettled && stats.Pending == 0)
				{
					break;
				}
				Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(120), "The chunks should be made within two minutes");
				gpu.DrawFrames(1);
			}
			world.Get<MeshRendererComponent>(terrain).bVisible = true;
			const Measured shown = MeasureFrames(gpu, view, 60);
			world.Get<MeshRendererComponent>(terrain).bVisible = false;
			const Measured hidden = MeasureFrames(gpu, view, 60);
			world.Get<MeshRendererComponent>(terrain).bVisible = true;
			const double terrainTime = (shown.Scene - hidden.Scene) + (shown.Shadows - hidden.Shadows);
			worst = std::max(worst, terrainTime);
			const TerrainChunksStats& stats = world.Get<TerrainChunksComponent>(terrain).Stats;
			table << "| " << name << " | " << viewpoint.Name << " | " << stats.Resident << " | " << shown.Triangles << " | " << shown.ShadowTriangles << " | "
				  << shown.Draws << " + " << shown.ShadowDraws << " | " << Fixed(shown.Scene - hidden.Scene) << " | " << Fixed(shown.Shadows - hidden.Shadows) << " | "
				  << Fixed(terrainTime) << " |\n";
			std::cout << "  " << name << ", " << viewpoint.Name << ": " << Fixed(terrainTime) << " ms" << std::endl;
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}
}

int main()
{
	try
	{
		std::cout << std::unitbuf;
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxTerrainGateGpu", 1);
		std::ostringstream table;
		table << "| Configuration | View | Chunks | Triangles drawn | In the shadows | Draws (scene + shadows) | Scene, ms | Shadows, ms | Terrain, ms |\n"
			  << "|---|---|---|---|---|---|---|---|---|\n";
		double worst = 0.0;
		Run(gpu, ESdfTerrainMesher::Dual, 0, table, worst);
		Run(gpu, ESdfTerrainMesher::MarchingCubes, 0, table, worst);
		Run(gpu, ESdfTerrainMesher::Dual, -1, table, worst);
		Run(gpu, ESdfTerrainMesher::MarchingCubes, -1, table, worst);
		std::cout << "\n" << table.str() << "\n";
		std::cout << (worst <= 3.0 ? "pass" : "FAIL") << "  Terrain on the GPU (scene and shadows) at most 3 ms: " << Fixed(worst) << " ms at worst\n";
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
	return 0;
}
