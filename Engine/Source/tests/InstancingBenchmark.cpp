// How long the renderer takes for a field of many small meshes: 50 000 small cubes in 200 cells of
// 10 by 10 metres, and 300 spheres among them, seen from above (all of it in view) and from inside
// the field (an eighth of it in view). Drawn as plain entities, one each, and as one entity drawn
// instanced (MeshInstancesComponent), cell by cell. Not a test (timing depends on the machine).
//
// Per frame: the whole frame on the CPU (DrawFrame()), the renderer's scene work on the CPU (the
// worlds and the views recorded), and the scene views on the GPU, by timestamps.
//
// Instanced, the field is then put on a ground plane under a sun, and measured again with the
// sun's shadows off and on (its defaults: four cascades of 2048 texels out to 80 m), then with the
// world's air (an EnvironmentComponent of the defaults: height fog, the sky following the sun), with
// its bloom, and with its ambient occlusion at each quality.
//
// The ground mode draws the atmosphere test scene (Assets/Scenes/AtmosphereTest.nyxscene: the
// ground test scene with about 24 000 small cubes scattered over its ground) from its camera,
// without and with its environment.
//
// NyxInstancingBenchmark [plain|instanced|ground] [frames] [material]: one mode, or all; the field
// drawn with another material than Materials/Untextured.nyxmat, e.g. one with a vertex function
// (Materials/VertexColorWind.nyxmat), to measure what it costs.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "Paths.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#if __has_include("MeshInstancesComponent.h")
	#include "MeshInstancesComponent.h"
	#define NYX_BENCHMARK_INSTANCING 1
#else
	#define NYX_BENCHMARK_INSTANCING 0
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 1280;
	constexpr uint32_t ViewHeight = 720;

	constexpr int CellsX = 20;
	constexpr int CellsZ = 10;
	constexpr float CellSize = 10.0f;
	constexpr int PerCell = 250;
	constexpr int Spheres = 300;

	// The same field every run
	struct Random
	{
		uint32_t State = 12345u;

		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) / 16777216.0f;
		}
	};

	struct Piece
	{
		glm::vec3 Position{ 0.0f };
		float Yaw = 0.0f;
		float Scale = 1.0f;
	};

	// Cell by cell, the field's cubes
	std::vector<Piece> MakeField()
	{
		Random random;
		std::vector<Piece> pieces;
		pieces.reserve(static_cast<size_t>(CellsX) * CellsZ * PerCell);
		for (int cellZ = 0; cellZ < CellsZ; ++cellZ)
		{
			for (int cellX = 0; cellX < CellsX; ++cellX)
			{
				const glm::vec2 corner(-CellsX * CellSize * 0.5f + cellX * CellSize, -CellsZ * CellSize * 0.5f + cellZ * CellSize);
				for (int index = 0; index < PerCell; ++index)
				{
					Piece piece;
					piece.Scale = 0.15f + random.Next() * 0.15f;
					piece.Position = glm::vec3(corner.x + random.Next() * CellSize, piece.Scale * 0.5f, corner.y + random.Next() * CellSize);
					piece.Yaw = random.Next() * 6.2831853f;
					pieces.push_back(piece);
				}
			}
		}
		return pieces;
	}

	TransformComponent TransformOf(const Piece& piece)
	{
		return TransformComponent{ .Position = piece.Position, .Rotation = glm::angleAxis(piece.Yaw, glm::vec3(0.0f, 1.0f, 0.0f)), .Scale = glm::vec3(piece.Scale) };
	}

	void AddMesh(Registry& world, const TransformComponent& transform, Mesh* mesh, Material* material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, transform);
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = mesh;
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
	}

	void AddSpheres(Registry& world, SceneRenderer& gpu, Material* material)
	{
		Random random{ 777u };
		for (int index = 0; index < Spheres; ++index)
		{
			const glm::vec3 position((random.Next() - 0.5f) * CellsX * CellSize, 0.5f, (random.Next() - 0.5f) * CellsZ * CellSize);
			AddMesh(world, TransformComponent{ .Position = position, .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) }, gpu.Renderer.GetSphereMesh(), material);
		}
	}

	void AddPlainField(Registry& world, SceneRenderer& gpu, Material* material, const std::vector<Piece>& field)
	{
		for (const Piece& piece : field)
		{
			AddMesh(world, TransformOf(piece), gpu.Renderer.GetCubeMesh(), material);
		}
	}

#if NYX_BENCHMARK_INSTANCING
	void AddInstancedField(Registry& world, SceneRenderer& gpu, Material* material, const std::vector<Piece>& field)
	{
		auto instances = std::make_shared<MeshInstances>();
		for (size_t first = 0; first < field.size(); first += PerCell)
		{
			MeshInstanceCell cell;
			cell.First = static_cast<uint32_t>(first);
			cell.Count = PerCell;
			cell.BoundsMin = field[first].Position;
			cell.BoundsMax = field[first].Position;
			for (size_t index = first; index < first + PerCell; ++index)
			{
				instances->Transforms.push_back(TransformOf(field[index]).ToMatrix());
				cell.BoundsMin = glm::min(cell.BoundsMin, field[index].Position);
				cell.BoundsMax = glm::max(cell.BoundsMax, field[index].Position);
			}
			instances->Cells.push_back(cell);
		}

		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) });
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = gpu.Renderer.GetCubeMesh();
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		MeshInstancesComponent meshInstances;
		SetMeshInstances(meshInstances, std::move(instances));
		world.Add<MeshInstancesComponent>(entity, meshInstances);
	}
#endif

	struct Shot
	{
		const char* Name;
		glm::vec3 Position;
		glm::quat Rotation;
	};

	struct Timing
	{
		double Frame = 0.0;
		double SceneCpu = 0.0;
		double SceneViewsGpu = 0.0;
		// Of that, the passes after the scene pass, every view's
		double BloomGpu = 0.0;
		double OcclusionGpu = 0.0;
	};

	double Median(std::vector<double> values)
	{
		std::sort(values.begin(), values.end());
		return values[values.size() / 2];
	}

	Timing Measure(SceneRenderer& gpu, int frames)
	{
		// Settled: uploads, pipelines, the first frames' stalls
		gpu.DrawFrames(30);
		std::vector<double> frame;
		std::vector<double> scene;
		std::vector<double> sceneViews;
		std::vector<double> bloom;
		std::vector<double> occlusion;
		for (int index = 0; index < frames; ++index)
		{
			const auto start = std::chrono::steady_clock::now();
			// The GPU's times of the frame before are there while the UI is built
			gpu.Renderer.DrawFrame([&]
				{
					const UI::PainterWindowStats::Times& times = gpu.Renderer.GetWindowPainterStats().FrameTimes;
					if (index > 0 && times.bGpu)
					{
						sceneViews.push_back(times.GpuSceneViews);
						SceneViewGpuTimes passes;
						for (const uint64_t view : static_cast<IEditorRenderer&>(gpu.Renderer).GetSceneViewIds())
						{
							passes.Bloom += gpu.Renderer.GetSceneViewGpuTimes(view)->Bloom;
							passes.AmbientOcclusion += gpu.Renderer.GetSceneViewGpuTimes(view)->AmbientOcclusion;
						}
						bloom.push_back(passes.Bloom);
						occlusion.push_back(passes.AmbientOcclusion);
					}
				});
			frame.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			scene.push_back(gpu.Renderer.GetWindowPainterStats().FrameTimes.Scene);
		}
		if (sceneViews.empty())
		{
			sceneViews.push_back(0.0);
			bloom.push_back(0.0);
			occlusion.push_back(0.0);
		}
		return Timing{ Median(frame), Median(scene), Median(sceneViews), Median(bloom), Median(occlusion) };
	}

	void PrintTiming(const char* name, const Timing& timing)
	{
		std::printf("  %-7s frame %8.2f ms   scene CPU %8.2f ms   scene views GPU %7.2f ms   bloom GPU %5.2f ms   occlusion GPU %5.2f ms\n", name, timing.Frame,
			timing.SceneCpu, timing.SceneViewsGpu, timing.BloomGpu, timing.OcclusionGpu);
	}

	// The world's air, each way it is measured: each line adds to the one before
	struct AirSetting
	{
		const char* Name;
		bool bEnvironment = false;
		bool bBloom = false;
		bool bAmbientOcclusion = false;
		EAmbientOcclusionQuality Quality = EAmbientOcclusionQuality::Medium;
	};
	const AirSetting AirSettings[] = {
		{ "without an environment", false, false, false },
		{ "with an environment: height fog, the sky following the sun", true, false, false },
		{ "with an environment and bloom", true, true, false },
		{ "with an environment, bloom and ambient occlusion (low)", true, true, true, EAmbientOcclusionQuality::Low },
		{ "with an environment, bloom and ambient occlusion (medium, the default)", true, true, true, EAmbientOcclusionQuality::Medium },
		{ "with an environment, bloom and ambient occlusion (high)", true, true, true, EAmbientOcclusionQuality::High },
	};

	void SetAir(Registry& world, Entity air, const AirSetting& setting)
	{
		world.Remove<EnvironmentComponent>(air);
		if (setting.bEnvironment)
		{
			EnvironmentComponent environment;
			environment.bBloom = setting.bBloom;
			environment.bAmbientOcclusion = setting.bAmbientOcclusion;
			environment.OcclusionQuality = setting.Quality;
			world.Add<EnvironmentComponent>(air, environment);
		}
	}

	// The atmosphere test scene from its own camera, each way the air is measured
	void RunGround(SceneRenderer& gpu, int frames)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		const std::filesystem::path path = Paths::GetAssetsDir() / "Scenes/AtmosphereTest.nyxscene";
		if (!SceneSerializer::LoadFromFile(path, world, context))
		{
			std::cout << "ground: " << path.string() << " can't be loaded\n";
			return;
		}
		UpdateScatters(world);
		size_t cubes = 0;
		world.Each<MeshInstancesComponent>([&](Entity, const MeshInstancesComponent& instances)
			{
				cubes += instances.Instances ? instances.Instances->Transforms.size() : 0;
			});
		Entity air;
		world.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent&)
			{
				air = entity;
			});

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		std::cout << "ground: the atmosphere test scene, " << cubes << " cubes scattered, from its camera\n";
		for (const AirSetting& setting : AirSettings)
		{
			SetAir(world, air, setting);
			std::cout << "  " << setting.Name << ":\n";
			PrintTiming("camera", Measure(gpu, frames));
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
	}

	void Run(SceneRenderer& gpu, const std::string& mode, int frames, const std::string& materialPath)
	{
		Registry world;
		const Entity camera = world.CreateEntity();
		world.Add<TransformComponent>(camera, TransformComponent{});
		world.Add<CameraComponent>(camera, CameraComponent{});

		Material* material = gpu.Renderer.GetMaterial(materialPath);
		const std::vector<Piece> field = MakeField();
		const auto buildStart = std::chrono::steady_clock::now();
		if (mode == "plain")
		{
			AddPlainField(world, gpu, material, field);
		}
#if NYX_BENCHMARK_INSTANCING
		else
		{
			AddInstancedField(world, gpu, material, field);
		}
#endif
		AddSpheres(world, gpu, material);
		const double buildMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - buildStart).count();

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();

		// From 120 m up looking down, all of the field in view; then 2 m up in its middle, looking
		// along -z
		const Shot shots[] = {
			{ "above", glm::vec3(0.0f, 120.0f, 60.0f), glm::angleAxis(glm::radians(-63.0f), glm::vec3(1.0f, 0.0f, 0.0f)) },
			{ "inside", glm::vec3(0.0f, 2.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) },
		};
		std::cout << mode << ": " << field.size() << " cubes and " << Spheres << " spheres, made in " << static_cast<int>(buildMilliseconds) << " ms\n";
		for (const Shot& shot : shots)
		{
			TransformComponent& transform = world.Get<TransformComponent>(camera);
			transform.Position = shot.Position;
			transform.Rotation = shot.Rotation;
			PrintTiming(shot.Name, Measure(gpu, frames));
		}

		// On the ground, under the sun, without and with its shadows
		if (mode != "plain")
		{
			AddMesh(world, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(CellsX * CellSize, 1.0f, CellsZ * CellSize) },
				gpu.Renderer.GetPlaneMesh(), material);
			const Entity sun = world.CreateEntity();
			world.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, -60.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
			world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
			for (const bool bShadows : { false, true })
			{
				world.Get<DirectionalLightComponent>(sun).bCastShadows = bShadows;
				std::cout << mode << " on a ground, under a sun, " << (bShadows ? "with its shadows (4 cascades)" : "without shadows") << ":\n";
				for (const Shot& shot : shots)
				{
					TransformComponent& transform = world.Get<TransformComponent>(camera);
					transform.Position = shot.Position;
					transform.Rotation = shot.Rotation;
					PrintTiming(shot.Name, Measure(gpu, frames));
				}
			}

			// With its shadows, and the world's air
			const Entity air = world.CreateEntity();
			for (const AirSetting& setting : AirSettings)
			{
				if (!setting.bEnvironment)
				{
					continue;
				}
				SetAir(world, air, setting);
				std::cout << mode << " on a ground, under a sun, with its shadows, " << setting.Name << ":\n";
				for (const Shot& shot : shots)
				{
					TransformComponent& transform = world.Get<TransformComponent>(camera);
					transform.Position = shot.Position;
					transform.Rotation = shot.Rotation;
					PrintTiming(shot.Name, Measure(gpu, frames));
				}
			}
		}

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	const std::string only = argc > 1 ? argv[1] : "";
#ifdef NDEBUG
	const int frames = argc > 2 ? std::atoi(argv[2]) : 300;
	std::cout << "Release, median of " << frames << " frames, " << ViewWidth << " x " << ViewHeight << "\n";
#else
	const int frames = argc > 2 ? std::atoi(argv[2]) : 60;
	std::cout << "Debug, median of " << frames << " frames, " << ViewWidth << " x " << ViewHeight << "\n";
#endif
	const std::string materialPath = argc > 3 ? argv[3] : "Materials/Untextured.nyxmat";
	std::cout << "The field in " << materialPath << "\n";
	try
	{
		SceneRenderer gpu("NyxInstancingBenchmark");
		gpu.Renderer.SetVSync(false);
		Nyx::RegisterComponentTypes();
		for (const char* mode : { "plain", "instanced", "ground" })
		{
			if (!only.empty() && only != mode)
			{
				continue;
			}
			if (std::string(mode) == "ground")
			{
				RunGround(gpu, frames);
				continue;
			}
#if !NYX_BENCHMARK_INSTANCING
			if (std::string(mode) == "instanced")
			{
				std::cout << "instanced: this build has no MeshInstancesComponent\n";
				continue;
			}
#endif
			Run(gpu, mode, frames, materialPath);
		}
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxInstancingBenchmark: " << error.what() << "\n";
		return 1;
	}
	return 0;
}
