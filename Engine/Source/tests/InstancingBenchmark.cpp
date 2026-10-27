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
// without and with its environment. The lights mode draws it with its environment and 0, 1, 16
// and 64 point lights in view, each reaching 6 m. The particles mode draws it with 10 emitters of
// 2 000 particles each, half additive, half alpha blended, lit and sorted.
//
// The wind mode draws the ground test scene (Assets/Scenes/GroundTest.nyxscene: its grass, crossed
// quads in Materials/GroundTest/Grass.nyxmat, swaying by Nyx's VertexColorWind) from above its
// grass, still (no wind), in the world's default wind, and in it with a game's local air over all of
// it that changes every frame (its texture filled and copied each frame); then the particles mode's
// 10 emitters, still and in the default wind.
//
// The msaa mode measures multisample anti-aliasing (IRenderer::SetMsaaSamples()) at 1, 4 and 8
// samples a pixel: the instanced field on its ground under a sun casting its shadows, with the
// world's air (the environment's defaults: fog, the sky, bloom, ambient occlusion), the atmosphere
// test scene as saved, then the lights and particles modes; and the memory of a 1920 x 1080 view's
// targets at each count. The other modes draw with one sample, as before anti-aliasing.
//
// NyxInstancingBenchmark [plain|instanced|ground|lights|particles|wind|msaa] [frames] [material]: one mode, or all; the field
// drawn with another material than Materials/Untextured.nyxmat, e.g. one with a vertex function
// (Materials/VertexColorWind.nyxmat), to measure what it costs; a swaying one is measured again in
// the world's default wind.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "ParticleEmitterComponent.h"
#include "Paths.h"
#include "PointLightComponent.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "WindComponent.h"
#include "WindField.h"
#include "Particles.h"
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
		double ParticlesGpu = 0.0;
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
		std::vector<double> particles;
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
							passes.Particles += gpu.Renderer.GetSceneViewGpuTimes(view)->Particles;
						}
						bloom.push_back(passes.Bloom);
						occlusion.push_back(passes.AmbientOcclusion);
						particles.push_back(passes.Particles);
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
			particles.push_back(0.0);
		}
		return Timing{ Median(frame), Median(scene), Median(sceneViews), Median(bloom), Median(occlusion), Median(particles) };
	}

	void PrintTiming(const char* name, const Timing& timing)
	{
		std::printf("  %-7s frame %8.2f ms   scene CPU %8.2f ms   scene views GPU %7.2f ms   bloom GPU %5.2f ms   occlusion GPU %5.2f ms   particles GPU %5.2f ms\n", name,
			timing.Frame, timing.SceneCpu, timing.SceneViewsGpu, timing.BloomGpu, timing.OcclusionGpu, timing.ParticlesGpu);
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

	// The atmosphere test scene from its own camera, with its environment, lit by more and more
	// point lights in front of the camera, among its cubes
	void RunLights(SceneRenderer& gpu, int frames)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		const std::filesystem::path path = Paths::GetAssetsDir() / "Scenes/AtmosphereTest.nyxscene";
		if (!SceneSerializer::LoadFromFile(path, world, context))
		{
			std::cout << "lights: " << path.string() << " can't be loaded\n";
			return;
		}
		UpdateScatters(world);

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		std::cout << "lights: the atmosphere test scene with its environment, from its camera, point lights reaching 6 m in front of it\n";
		Random random;
		std::vector<Entity> lights;
		for (const int count : { 0, 1, 16, 64 })
		{
			while (static_cast<int>(lights.size()) < count)
			{
				const Entity light = world.CreateEntity();
				const glm::vec3 position(-12.0f + random.Next() * 24.0f, 0.3f + random.Next() * 1.7f, -14.0f + random.Next() * 20.0f);
				world.Add<TransformComponent>(light, TransformComponent{ .Position = position });
				world.Add<PointLightComponent>(light, PointLightComponent{ .Color = glm::vec3(0.4f + random.Next() * 0.6f, 0.4f + random.Next() * 0.6f, 0.4f + random.Next() * 0.6f),
					.Intensity = 2.0f, .Range = 6.0f });
				lights.push_back(light);
			}
			const Timing timing = Measure(gpu, frames);
			const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
			std::cout << "  " << count << " point lights, " << (stats->PointLights - stats->PointLightsCulled) << " of them in view:\n";
			PrintTiming("camera", timing);
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
	}

	// The atmosphere test scene from its own camera, with its environment, and 10 emitters in front of
	// the camera, 2 000 particles each: five of sparks, additive, and five of smoke, alpha blended
	// and lit, so sorted back to front each frame
	void RunParticles(SceneRenderer& gpu, int frames)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		const std::filesystem::path path = Paths::GetAssetsDir() / "Scenes/AtmosphereTest.nyxscene";
		if (!SceneSerializer::LoadFromFile(path, world, context))
		{
			std::cout << "particles: " << path.string() << " can't be loaded\n";
			return;
		}
		UpdateScatters(world);

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		std::cout << "particles: the atmosphere test scene with its environment, from its camera, emitters of 2 000 particles each in front of it\n";
		for (const int count : { 0, 10 })
		{
			for (int index = 0; index < count; ++index)
			{
				const bool bSmoke = index % 2 == 1;
				ParticleEmitterComponent emitter;
				emitter.MaxParticles = 2000;
				emitter.Lifetime = glm::vec2(1.5f, 2.5f);
				// Enough a second that 2 000 stay alive
				emitter.Rate = 1500.0f;
				emitter.SpawnRadius = 0.3f;
				emitter.ConeAngle = 35.0f;
				emitter.Speed = bSmoke ? glm::vec2(0.3f, 0.6f) : glm::vec2(1.5f, 3.0f);
				emitter.Gravity = bSmoke ? 0.0f : 3.0f;
				emitter.Buoyancy = bSmoke ? 0.5f : 0.0f;
				emitter.Drag = 0.5f;
				emitter.Size = bSmoke ? glm::vec2(0.3f, 0.5f) : glm::vec2(0.05f, 0.08f);
				emitter.Blend = bSmoke ? EParticleBlend::Alpha : EParticleBlend::Additive;
				emitter.bLit = bSmoke;
				emitter.EmissiveIntensity = bSmoke ? 1.0f : 3.0f;
				emitter.ColorOverLife = { { .Time = 0.0f, .Color = bSmoke ? glm::vec3(0.5f) : glm::vec3(1.0f, 0.6f, 0.2f), .Alpha = bSmoke ? 0.4f : 1.0f },
					{ .Time = 1.0f, .Color = bSmoke ? glm::vec3(0.4f) : glm::vec3(1.0f, 0.2f, 0.05f), .Alpha = 0.0f } };
				emitter.Seed = index + 1;
				const Entity entity = world.CreateEntity();
				world.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(-9.0f + index * 2.0f, 0.5f, 2.0f - (index % 3) * 3.0f) });
				world.Add<ParticleEmitterComponent>(entity, emitter);
			}
			// Filled up
			gpu.DrawFrames(count > 0 ? 300 : 1);
			const Timing timing = Measure(gpu, frames);
			const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
			std::cout << "  " << count << " emitters, " << stats->Particles << " particles drawn:\n";
			PrintTiming("camera", timing);
		}
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
	}

	// A game's local air everywhere, changing every frame: a swirl round the origin as strong as the
	// time is, written into every cell asked for
	struct Swirl final : IWindField
	{
		void SampleWind(std::span<const glm::vec3> positions, float time, std::span<glm::vec3> outVelocities) const override
		{
			for (size_t index = 0; index < positions.size(); ++index)
			{
				const glm::vec3& p = positions[index];
				outVelocities[index] = glm::vec3(-p.z, 0.5f, p.x) * (0.1f + 0.05f * std::sin(time * 3.0f));
			}
		}

		void SampleWindGrid(const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities) const override
		{
			SampleWindGridAtCentres(*this, grid, time, outVelocities);
		}
	};

	// The ground test scene's grass still, in the world's wind and with a game's local air; the
	// particles mode's emitters still and in the wind
	void RunWind(SceneRenderer& gpu, int frames)
	{
		// The contract alone: a million points of the default wind, steady and while it fades
		{
			std::vector<glm::vec3> points(1000000);
			for (size_t index = 0; index < points.size(); ++index)
			{
				points[index] = glm::vec3(static_cast<float>(index % 1000) * 0.13f, static_cast<float>(index % 7) * 0.5f, static_cast<float>(index / 1000) * 0.11f);
			}
			std::vector<glm::vec3> velocities(points.size());
			WindDrive drive;
			SetWindTarget(drive, WindComponent{}, 0.0);
			WindComponent turned;
			turned.DirectionDegrees = 80.0f;
			const WindState steady = EvaluateWind(drive, 10.0);
			SetWindTarget(drive, turned, 10.0);
			const WindState fading = EvaluateWind(drive, 12.0);
			for (const auto& [name, wind] : { std::pair<const char*, WindState>{ "steady", steady }, std::pair<const char*, WindState>{ "fading", fading } })
			{
				std::fill(velocities.begin(), velocities.end(), glm::vec3(0.0f));
				const auto start = std::chrono::steady_clock::now();
				AddWind(wind, points, wind.Time, velocities);
				const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				std::printf("wind: AddWind() over a million points, %s: %.2f ms, %.1f ns a point (%.3f)\n", name, ms, ms * 1e6 / static_cast<double>(points.size()), velocities[12345].x);
			}
		}

		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		const std::filesystem::path path = Paths::GetAssetsDir() / "Scenes/GroundTest.nyxscene";
		if (!SceneSerializer::LoadFromFile(path, world, context))
		{
			std::cout << "wind: " << path.string() << " can't be loaded\n";
			return;
		}
		UpdateScatters(world);
		size_t blades = 0;
		world.Each<MeshInstancesComponent>([&](Entity, const MeshInstancesComponent& instances)
			{
				blades += instances.Instances ? instances.Instances->Transforms.size() : 0;
			});
		std::vector<Entity> winds;
		world.Each<WindComponent>([&](Entity entity, const WindComponent&) { winds.push_back(entity); });
		for (const Entity entity : winds)
		{
			world.Remove<WindComponent>(entity);
		}
		const Entity air = world.CreateEntity();
		world.Each<CameraComponent>([&](Entity entity, CameraComponent&)
			{
				world.Get<TransformComponent>(entity) = TransformComponent{ .Position = glm::vec3(0.0f, 6.0f, 14.0f),
					.Rotation = glm::quat(glm::radians(glm::vec3(-28.0f, 0.0f, 0.0f))),
					.Scale = glm::vec3(1.0f) };
			});

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		std::cout << "wind: the ground test scene, " << blades << " blades of grass, from above them\n";
		std::cout << "  still:\n";
		PrintTiming("camera", Measure(gpu, frames));
		world.Add<WindComponent>(air, WindComponent{});
		std::cout << "  in the default wind:\n";
		PrintTiming("camera", Measure(gpu, frames));
		Swirl swirl;
		renderer.SetWorldWindField(&world, &swirl);
		std::cout << "  in the default wind, with a game's local air changing every frame:\n";
		PrintTiming("camera", Measure(gpu, frames));
		renderer.SetWorldWindField(&world, nullptr);

		// The particles mode's emitters, still and in the wind
		world.Remove<WindComponent>(air);
		for (int index = 0; index < 10; ++index)
		{
			const bool bSmoke = index % 2 == 1;
			ParticleEmitterComponent emitter;
			emitter.MaxParticles = 2000;
			emitter.Lifetime = glm::vec2(1.5f, 2.5f);
			emitter.Rate = 1500.0f;
			emitter.SpawnRadius = 0.3f;
			emitter.ConeAngle = 35.0f;
			emitter.Speed = bSmoke ? glm::vec2(0.3f, 0.6f) : glm::vec2(1.5f, 3.0f);
			emitter.Gravity = bSmoke ? 0.0f : 3.0f;
			emitter.Buoyancy = bSmoke ? 0.5f : 0.0f;
			emitter.Drag = 0.5f;
			emitter.Size = bSmoke ? glm::vec2(0.3f, 0.5f) : glm::vec2(0.05f, 0.08f);
			emitter.Blend = bSmoke ? EParticleBlend::Alpha : EParticleBlend::Additive;
			emitter.Seed = index + 1;
			const Entity entity = world.CreateEntity();
			world.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(-9.0f + index * 2.0f, 0.5f, 2.0f - (index % 3) * 3.0f) });
			world.Add<ParticleEmitterComponent>(entity, emitter);
		}
		// Their CPU step alone (UpdateParticles()), as the renderer runs it, still and in the wind
		for (const bool bWind : { false, true })
		{
			RenderParticles particles;
			ParticleAir particleAir;
			particleAir.Wind = bWind ? MakeSteadyWind(WindComponent{}, 0.0) : WindState{};
			for (int frame = 0; frame < 300; ++frame)
			{
				UpdateParticles(world, static_cast<float>(frame) / 60.0f, std::nullopt, particleAir, 50000, particles);
			}
			std::vector<double> steps;
			for (int frame = 300; frame < 300 + frames; ++frame)
			{
				const auto start = std::chrono::steady_clock::now();
				UpdateParticles(world, static_cast<float>(frame) / 60.0f, std::nullopt, particleAir, 50000, particles);
				steps.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
			}
			std::printf("  UpdateParticles() of 10 emitters, %u alive, %s: %.3f ms\n", particles.Alive, bWind ? "in the default wind" : "still", Median(steps));
		}

		gpu.DrawFrames(300);
		std::cout << "  10 emitters of 2 000 particles, still:\n";
		PrintTiming("camera", Measure(gpu, frames));
		WindComponent steady;
		steady.Gustiness = 0.0f;
		steady.Turbulence = 0.0f;
		steady.GroundFactor = 1.0f;
		steady.Aloft = 0.0f;
		world.Add<WindComponent>(air, steady);
		gpu.DrawFrames(300);
		std::cout << "  10 emitters of 2 000 particles, in a steady wind the same everywhere (as the environment's was):\n";
		PrintTiming("camera", Measure(gpu, frames));
		world.Get<WindComponent>(air) = WindComponent{};
		gpu.DrawFrames(300);
		std::cout << "  10 emitters of 2 000 particles, in the default wind:\n";
		PrintTiming("camera", Measure(gpu, frames));

		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(2);
	}

	// Anti-aliasing at 1, 4 and 8 samples a pixel: the field on its ground with its shadows and air,
	// the atmosphere test scene, with lights and with particles, a 1920 x 1080 view's targets
	void RunMsaa(SceneRenderer& gpu, int frames, const std::string& materialPath)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		Material* material = gpu.Renderer.GetMaterial(materialPath);
		Registry field;
		const Entity camera = field.CreateEntity();
		field.Add<TransformComponent>(camera, TransformComponent{});
		field.Add<CameraComponent>(camera, CameraComponent{});
		AddInstancedField(field, gpu, material, MakeField());
		AddSpheres(field, gpu, material);
		AddMesh(field, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(CellsX * CellSize, 1.0f, CellsZ * CellSize) },
			gpu.Renderer.GetPlaneMesh(), material);
		const Entity sun = field.CreateEntity();
		field.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, -60.0f, 0.0f))), .Scale = glm::vec3(1.0f) });
		field.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		field.Add<EnvironmentComponent>(field.CreateEntity(), EnvironmentComponent{});

		Registry ground;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		const std::filesystem::path path = Paths::GetAssetsDir() / "Scenes/AtmosphereTest.nyxscene";
		const bool bGround = SceneSerializer::LoadFromFile(path, ground, context);
		if (bGround)
		{
			UpdateScatters(ground);
		}
		else
		{
			std::cout << "msaa: " << path.string() << " can't be loaded\n";
		}

		const Shot shots[] = {
			{ "above", glm::vec3(0.0f, 120.0f, 60.0f), glm::angleAxis(glm::radians(-63.0f), glm::vec3(1.0f, 0.0f, 0.0f)) },
			{ "inside", glm::vec3(0.0f, 2.0f, 0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f) },
		};
		for (const uint32_t samples : { 1u, 4u, 8u })
		{
			renderer.SetMsaaSamples(samples);
			gpu.DrawFrames(2);
			if (renderer.GetMsaaSamples() != samples)
			{
				std::cout << "msaa: " << samples << " samples a pixel: the GPU can't, skipped\n";
				continue;
			}

			renderer.SetWorld(&field);
			uint64_t view = renderer.CreateSceneView();
			renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
			gpu.DrawUntilShadersCompiled();
			std::cout << "msaa: " << samples << " samples a pixel, the instanced field on a ground, with its shadows and the environment's defaults:\n";
			for (const Shot& shot : shots)
			{
				TransformComponent& transform = field.Get<TransformComponent>(camera);
				transform.Position = shot.Position;
				transform.Rotation = shot.Rotation;
				PrintTiming(shot.Name, Measure(gpu, frames));
			}
			renderer.DestroySceneView(view);

			if (bGround)
			{
				renderer.SetWorld(&ground);
				view = renderer.CreateSceneView();
				renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
				renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
				gpu.DrawUntilShadersCompiled();
				std::cout << "msaa: " << samples << " samples a pixel, the atmosphere test scene from its camera:\n";
				PrintTiming("camera", Measure(gpu, frames));
				renderer.DestroySceneView(view);
			}

			// The memory of a 1920 x 1080 view's targets
			view = renderer.CreateSceneView();
			renderer.SetSceneViewSize(view, 1920, 1080);
			gpu.DrawFrames(2);
			std::printf("  a 1920 x 1080 view's targets: %.1f MiB\n", static_cast<double>(gpu.Renderer.GetSceneViewTargetMemory(view)) / (1024.0 * 1024.0));
			renderer.DestroySceneView(view);
			renderer.SetWorld(nullptr);
			gpu.DrawFrames(2);

			// Point lights, and particles drawn after the resolve
			std::cout << "msaa: " << samples << " samples a pixel, ";
			RunLights(gpu, frames);
			std::cout << "msaa: " << samples << " samples a pixel, ";
			RunParticles(gpu, frames);
		}
		renderer.SetMsaaSamples(1);
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

		// A swaying material again in the world's default wind: what reading it costs at every vertex
		if (materialPath.find("Wind") != std::string::npos)
		{
			const Entity air = world.CreateEntity();
			world.Add<WindComponent>(air, WindComponent{});
			std::cout << "  in the world's default wind:\n";
			for (const Shot& shot : shots)
			{
				TransformComponent& transform = world.Get<TransformComponent>(camera);
				transform.Position = shot.Position;
				transform.Rotation = shot.Rotation;
				PrintTiming(shot.Name, Measure(gpu, frames));
			}
			world.DestroyEntity(air);
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
		for (const char* mode : { "plain", "instanced", "ground", "lights", "particles", "wind", "msaa" })
		{
			if (!only.empty() && only != mode)
			{
				continue;
			}
			if (std::string(mode) == "msaa")
			{
				RunMsaa(gpu, frames, materialPath);
				continue;
			}
			if (std::string(mode) == "ground")
			{
				RunGround(gpu, frames);
				continue;
			}
			if (std::string(mode) == "lights")
			{
				RunLights(gpu, frames);
				continue;
			}
			if (std::string(mode) == "particles")
			{
				RunParticles(gpu, frames);
				continue;
			}
			if (std::string(mode) == "wind")
			{
				RunWind(gpu, frames);
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
