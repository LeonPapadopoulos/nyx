// The materials' own numbers on a real GPU: a mesh's custom channels (Vertex::Custom), each
// instance's (MeshInstances::Custom) and an entity's drawn once (MeshRendererComponent::Custom)
// reach the material, shown as its colour: each where it belongs, an instance without any as zero,
// an entity's changed from one frame to the next without its mesh going up again, two entities
// drawn in one draw each with its own. The view's time stands still while the world's clock is
// paused, and moves on by a step when it steps.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "Entity.h"
#include "FixedStepClock.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "MeshRendererComponent.h"
#include "RuntimeMeshComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;

	// What the probe shows as its colour (unlit, seen in the base colour debug view, so as it is):
	// what Show chooses
	constexpr const char* ProbeShader = R"(import nyx.surface;

struct NyxVertexDataProbe : ISurface
{
    [Enum("Custom, Custom W, Instance Custom, Instance Custom W, Time")] uint Show = 0;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.Unlit = true;
        switch (Show)
        {
        case 0: o.BaseColor = input.Custom.xyz; break;
        case 1: o.BaseColor = input.Custom.www; break;
        case 2: o.BaseColor = input.InstanceCustom.xyz; break;
        case 3: o.BaseColor = input.InstanceCustom.www; break;
        default: o.BaseColor = float3(frac(View.Time * 0.25), frac(View.Time * 0.5), 0.0); break;
        }
        return o;
    }
}
)";

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("VertexData-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	// A colour as the base colour debug view shows it: sRGB-encoded, neither exposed nor tonemapped
	std::array<uint8_t, 3> Encoded(const glm::vec3& linear)
	{
		std::array<uint8_t, 3> encoded{};
		for (int channel = 0; channel < 3; ++channel)
		{
			const float c = std::clamp(linear[channel], 0.0f, 1.0f);
			const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
			encoded[channel] = static_cast<uint8_t>(std::lround(srgb * 255.0f));
		}
		return encoded;
	}

	TransformComponent At(const glm::vec3& position)
	{
		return TransformComponent{ .Position = position, .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(1.0f) };
	}

	// A square of `size` facing the camera (+Z), every vertex with the same custom channels
	std::shared_ptr<MeshData> MakeSquare(float size, const glm::vec4& custom)
	{
		auto square = std::make_shared<MeshData>();
		const float h = size * 0.5f;
		const glm::vec3 normal(0.0f, 0.0f, 1.0f);
		for (const glm::vec2 corner : { glm::vec2(-h, -h), glm::vec2(h, -h), glm::vec2(h, h), glm::vec2(-h, h) })
		{
			square->Vertices.push_back(Vertex{ .Position = glm::vec3(corner, 0.0f), .Normal = normal, .Custom = custom });
		}
		square->Indices = { 0, 1, 2, 2, 3, 0 };
		return square;
	}

	// The probe's shader, and a material of it for each thing it shows, in the assets folder while
	// the test runs
	struct Probe
	{
		LiveMaterialFile Shader{ "Shaders/NyxVertexDataProbe.slang" };
		std::vector<std::unique_ptr<LiveMaterialFile>> Materials;

		Probe()
		{
			Shader.Write(ProbeShader);
			for (int show = 0; show < 5; ++show)
			{
				auto material = std::make_unique<LiveMaterialFile>("Materials/NyxVertexDataProbe" + std::to_string(show) + ".nyxmat");
				material->Write("Shader = Shaders/NyxVertexDataProbe.slang\nShow = " + std::to_string(show) + "\n");
				Materials.push_back(std::move(material));
			}
		}

		Material* Get(SceneRenderer& gpu, int show) const
		{
			return gpu.Renderer.GetMaterial(Materials[static_cast<size_t>(show)]->Path);
		}
	};

	// A world seen by a camera 6 m in front of the origin, looking at it, in a view of its own that
	// shows the materials' base colour as it is
	struct TestScene
	{
		SceneRenderer& Gpu;
		Registry World;
		Entity Camera;
		uint64_t View = 0;

		explicit TestScene(SceneRenderer& gpu)
			: Gpu(gpu)
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, At(glm::vec3(0.0f, 0.0f, 6.0f)));
			World.Add<CameraComponent>(Camera, CameraComponent{});

			IEditorRenderer& renderer = gpu.Renderer;
			renderer.SetWorld(&World);
			View = renderer.CreateSceneView();
			renderer.SetSceneViewCameraMode(View, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewSize(View, ViewWidth, ViewHeight);
			renderer.SetSceneViewDebug(View, ESceneViewDebug::BaseColor);
		}

		~TestScene()
		{
			IEditorRenderer& renderer = Gpu.Renderer;
			renderer.SetWorldInterpolation(&World, nullptr);
			renderer.DestroySceneView(View);
			renderer.SetWorld(nullptr);
			Gpu.DrawFrames(1);
		}

		Entity AddMesh(const TransformComponent& transform, std::shared_ptr<const MeshData> mesh, Material* material)
		{
			const Entity entity = World.CreateEntity();
			World.Add<TransformComponent>(entity, transform);
			MeshRendererComponent meshRenderer;
			meshRenderer.MaterialAsset = material;
			World.Add<MeshRendererComponent>(entity, meshRenderer);
			RuntimeMeshComponent runtimeMesh;
			SetRuntimeMesh(runtimeMesh, std::move(mesh));
			World.Add<RuntimeMeshComponent>(entity, runtimeMesh);
			return entity;
		}

		RgbaImage Draw(int frames = 2)
		{
			Gpu.DrawFrames(frames);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}

		// Where a point of the world is in the picture
		std::array<uint8_t, 3> PixelOf(const RgbaImage& image, const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel =
				WorldToScreenPoint(World.Get<TransformComponent>(Camera), World.Get<CameraComponent>(Camera), glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel.has_value(), "The point should be in front of the camera");
			return PixelAt(image, static_cast<uint32_t>(pixel->x), static_cast<uint32_t>(pixel->y));
		}
	};

	void RequireShows(const std::array<uint8_t, 3>& pixel, const glm::vec3& value, const std::string& what)
	{
		Require(Near(pixel, Encoded(value), 2), what + " shows as " + Text(pixel) + ", not " + Text(Encoded(value)));
	}

	// A mesh's custom channels, all four, as they were made: half precision keeps these exactly
	void TestTheMeshsChannels(SceneRenderer& gpu, const Probe& probe)
	{
		TestScene scene(gpu);
		const glm::vec4 custom(0.25f, 0.5f, 0.75f, 0.375f);
		const auto square = MakeSquare(1.0f, custom);
		const Entity xyz = scene.AddMesh(At(glm::vec3(-1.0f, 0.0f, 0.0f)), square, probe.Get(gpu, 0));
		scene.AddMesh(At(glm::vec3(1.0f, 0.0f, 0.0f)), square, probe.Get(gpu, 1));
		// A mesh without them: zero
		scene.AddMesh(At(glm::vec3(0.0f, 1.2f, 0.0f)), MakeSquare(0.8f, glm::vec4(0.0f)), probe.Get(gpu, 0));
		gpu.DrawUntilShadersCompiled();
		const RgbaImage image = scene.Draw();
		SaveIfAsked(image, "MeshChannels");
		RequireShows(scene.PixelOf(image, glm::vec3(-1.0f, 0.0f, 0.0f)), glm::vec3(custom), "The mesh's custom x, y and z");
		RequireShows(scene.PixelOf(image, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(custom.w), "The mesh's custom w");
		RequireShows(scene.PixelOf(image, glm::vec3(0.0f, 1.2f, 0.0f)), glm::vec3(0.0f), "A mesh without custom channels");

		// Between vertices the channels are interpolated as any attribute is
		auto ramp = std::make_shared<MeshData>(*square);
		ramp->Vertices[0].Custom = ramp->Vertices[3].Custom = glm::vec4(0.0f);
		ramp->Vertices[1].Custom = ramp->Vertices[2].Custom = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
		SetRuntimeMesh(scene.World.Get<RuntimeMeshComponent>(xyz), ramp);
		const RgbaImage ramped = scene.Draw();
		const std::array<uint8_t, 3> middle = scene.PixelOf(ramped, glm::vec3(-1.0f, 0.0f, 0.0f));
		Require(std::abs(int(middle[0]) - int(Encoded(glm::vec3(0.5f))[0])) <= 6 && middle[1] == 0, "Halfway along a ramp of custom x, the material should get a half, not " + Text(middle));
	}

	// Each instance's custom numbers, an instance without any as zero, the transforms as ever
	void TestEachInstancesNumbers(SceneRenderer& gpu, const Probe& probe)
	{
		TestScene scene(gpu);
		auto instances = std::make_shared<MeshInstances>();
		const std::vector<glm::vec4> custom = { glm::vec4(1.0f, 0.0f, 0.0f, 0.25f), glm::vec4(0.0f, 1.0f, 0.0f, 0.5f), glm::vec4(0.0f, 0.0f, 1.0f, 0.75f) };
		for (const float x : { -1.5f, 0.0f, 1.5f, 3.0f })
		{
			instances->Transforms.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, 0.0f)));
		}
		// The fourth has none
		instances->Custom = custom;
		const Entity entity = scene.AddMesh(At(glm::vec3(0.0f)), MakeSquare(0.8f, glm::vec4(0.0f)), probe.Get(gpu, 2));
		MeshInstancesComponent component;
		SetMeshInstances(component, instances);
		scene.World.Add<MeshInstancesComponent>(entity, component);
		// Its own number is for drawing once: not what its instances get
		scene.World.Get<MeshRendererComponent>(entity).Custom = glm::vec4(1.0f);
		gpu.DrawUntilShadersCompiled();

		const RgbaImage image = scene.Draw();
		SaveIfAsked(image, "InstanceNumbers");
		for (size_t index = 0; index < custom.size(); ++index)
		{
			RequireShows(scene.PixelOf(image, glm::vec3(instances->Transforms[index][3])), glm::vec3(custom[index]), "Instance " + std::to_string(index) + "'s custom numbers");
		}
		RequireShows(scene.PixelOf(image, glm::vec3(3.0f, 0.0f, 0.0f)), glm::vec3(0.0f), "An instance without custom numbers");

		scene.World.Get<MeshRendererComponent>(entity).MaterialAsset = probe.Get(gpu, 3);
		const RgbaImage w = scene.Draw();
		for (size_t index = 0; index < custom.size(); ++index)
		{
			RequireShows(scene.PixelOf(w, glm::vec3(instances->Transforms[index][3])), glm::vec3(custom[index].w), "Instance " + std::to_string(index) + "'s custom w");
		}
	}

	// Two entities of one mesh drawn once, in one draw: each with its own numbers, changed from one
	// frame to the next without the mesh going up again
	void TestAnEntitysNumbers(SceneRenderer& gpu, const Probe& probe)
	{
		TestScene scene(gpu);
		const auto square = MakeSquare(0.8f, glm::vec4(0.0f));
		const Entity left = scene.AddMesh(At(glm::vec3(-1.0f, 0.0f, 0.0f)), square, probe.Get(gpu, 2));
		const Entity right = scene.AddMesh(At(glm::vec3(1.0f, 0.0f, 0.0f)), square, probe.Get(gpu, 2));
		scene.World.Get<MeshRendererComponent>(left).Custom = glm::vec4(0.8f, 0.2f, 0.1f, 0.0f);
		scene.World.Get<MeshRendererComponent>(right).Custom = glm::vec4(0.1f, 0.3f, 0.9f, 0.0f);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage image = scene.Draw();
		SaveIfAsked(image, "EntityNumbers");
		const SceneViewCullingStats* culling = gpu.Renderer.GetSceneViewCullingStats(scene.View);
		Require(culling && culling->Draws == 1 && culling->Instances == 2, "Two entities of one mesh and material should be one draw");
		RequireShows(scene.PixelOf(image, glm::vec3(-1.0f, 0.0f, 0.0f)), glm::vec3(0.8f, 0.2f, 0.1f), "The left entity's custom numbers");
		RequireShows(scene.PixelOf(image, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(0.1f, 0.3f, 0.9f), "The right entity's custom numbers");

		// Every frame something new, as a system sets it: drawn at once, the mesh not uploaded again
		const uint64_t uploads = gpu.Renderer.GetRuntimeMeshes().GetUploadCount();
		for (int frame = 0; frame < 4; ++frame)
		{
			const float value = 0.2f * static_cast<float>(frame + 1);
			scene.World.Get<MeshRendererComponent>(left).Custom = glm::vec4(value, 1.0f - value, 0.5f, 0.0f);
			const RgbaImage changed = scene.Draw(1);
			RequireShows(scene.PixelOf(changed, glm::vec3(-1.0f, 0.0f, 0.0f)), glm::vec3(value, 1.0f - value, 0.5f), "The left entity's numbers set in frame " + std::to_string(frame));
			RequireShows(scene.PixelOf(changed, glm::vec3(1.0f, 0.0f, 0.0f)), glm::vec3(0.1f, 0.3f, 0.9f), "The right entity's numbers, unchanged");
		}
		Require(gpu.Renderer.GetRuntimeMeshes().GetUploadCount() == uploads, "Changing an entity's numbers uploads its mesh again");
	}

	// The view's time is its world's: while the world's clock is paused it stands still, however
	// many frames pass; Step moves it on by a step. Without a clock (the editor's worlds) it runs on.
	void TestTimeStandsStillWhilePaused(SceneRenderer& gpu, const Probe& probe)
	{
		TestScene scene(gpu);
		scene.AddMesh(At(glm::vec3(0.0f)), MakeSquare(2.0f, glm::vec4(0.0f)), probe.Get(gpu, 4));
		gpu.DrawUntilShadersCompiled();
		const glm::vec3 middle(0.0f);

		// Without a clock: the renderer's seconds, which run on
		const std::array<uint8_t, 3> before = scene.PixelOf(scene.Draw(1), middle);
		std::this_thread::sleep_for(std::chrono::milliseconds(300));
		Require(!Near(scene.PixelOf(scene.Draw(1), middle), before, 0), "Without a clock, the view's time should run on");

		// The game's clock, 1.5 s in, half a step drawn: 1.45 s
		FixedStepClock clock;
		clock.SetStepSeconds(0.1);
		// A frame at a time: the clock drops what one frame takes beyond its most
		for (int frame = 0; frame < 15; ++frame)
		{
			clock.Advance(0.1);
		}
		clock.Advance(0.05);
		gpu.Renderer.SetWorldInterpolation(&scene.World, &clock);
		const RgbaImage running = scene.Draw(1);
		RequireShows(scene.PixelOf(running, middle), glm::vec3(std::fmod(1.45f * 0.25f, 1.0f), std::fmod(1.45f * 0.5f, 1.0f), 0.0f), "The time 1.45 s");

		clock.SetPaused(true);
		clock.Advance(0.2);
		const std::array<uint8_t, 3> paused = scene.PixelOf(scene.Draw(1), middle);
		RequireShows(paused, glm::vec3(std::fmod(1.5f * 0.25f, 1.0f), std::fmod(1.5f * 0.5f, 1.0f), 0.0f), "Paused, the time at the last step's end, 1.5 s,");
		for (int frame = 0; frame < 3; ++frame)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			clock.Advance(0.5);
			Require(scene.PixelOf(scene.Draw(1), middle) == paused, "Paused, the view's time should stand still");
		}
		clock.StepOnce();
		clock.Advance(0.0);
		RequireShows(scene.PixelOf(scene.Draw(1), middle), glm::vec3(std::fmod(1.6f * 0.25f, 1.0f), std::fmod(1.6f * 0.5f, 1.0f), 0.0f), "A step on, 1.6 s,");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		SceneRenderer gpu("NyxVertexDataGpuTests");
		const Probe probe;
		TestTheMeshsChannels(gpu, probe);
		TestEachInstancesNumbers(gpu, probe);
		TestAnEntitysNumbers(gpu, probe);
		TestTimeStandsStillWhilePaused(gpu, probe);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All vertex data GPU tests passed\n";
	return 0;
}
