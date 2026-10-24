// The materials' own numbers on a real GPU: a mesh's custom channels (Vertex::Custom), each
// instance's (MeshInstances::Custom) and an entity's drawn once (MeshRendererComponent::Custom)
// reach the material, shown as its colour: each where it belongs, an instance without any as zero,
// an entity's changed from one frame to the next without its mesh going up again, two entities
// drawn in one draw each with its own. The view's time stands still while the world's clock is
// paused, and moves on by a step when it steps.
//
// A material's vertex function (ISurfaceVertex) moves its mesh in every pass alike: the mesh moved
// in the world is drawn where an unmoved one put there is drawn, with its shadow there too, picked
// and outlined there, and not where it was. The bounds culled by are widened by the material's
// Max Vertex Offset: a mesh whose bounds are out of view but which its function moves into view is
// drawn, and left out without the widening. The function gets the mesh's, the instance's and the
// entity's numbers (an entity drawn once in picking too) and the time, still while paused.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there.
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "DirectionalLightComponent.h"
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

	// A material with a vertex function: the mesh shrunk towards its origin by its custom y, in its
	// own space; moved in the world by Offset, up by its instance's custom y, and to and fro along
	// x by Sway metres with the time
	constexpr const char* OffsetProbeShader = R"(import nyx.surface;

struct NyxVertexOffsetProbe : ISurface, ISurfaceVertex
{
    [Color] float3 BaseColor = float3(0.8, 0.3, 0.2);
    float3 Offset = float3(0.0, 0.0, 0.0);
    float Sway = 0.0;

    SurfaceOutput shade(SurfaceInput input)
    {
        SurfaceOutput o = SurfaceOutput::defaults(input);
        o.BaseColor = BaseColor;
        return o;
    }

    void displace(inout SurfaceVertex vertex)
    {
        vertex.Position *= 1.0 - vertex.Custom.y;
        vertex.OffsetWS = Offset + float3(Sway * sin(vertex.Time * 3.0), vertex.InstanceCustom.y, 0.0);
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

	// The vertex function's probe: its shader and its materials, by name
	struct OffsetProbe
	{
		LiveMaterialFile Shader{ "Shaders/NyxVertexOffsetProbe.slang" };
		std::vector<std::unique_ptr<LiveMaterialFile>> Materials;

		OffsetProbe()
		{
			Shader.Write(OffsetProbeShader);
			Add("Still", "");
			Add("Moved", "Offset = 2 0 0\nMaxVertexOffset = 2.5\n");
			Add("Far", "Offset = 10 0 0\nMaxVertexOffset = 10.5\n");
			Add("FarUnbounded", "Offset = 10 0 0\n");
			Add("Numbers", "MaxVertexOffset = 1.5\n");
			Add("Sway", "Sway = 1\nMaxVertexOffset = 1\n");
		}

		void Add(const std::string& name, const std::string& values)
		{
			auto material = std::make_unique<LiveMaterialFile>("Materials/NyxVertexOffsetProbe" + name + ".nyxmat");
			material->Write("Shader = Shaders/NyxVertexOffsetProbe.slang\n" + values);
			Materials.push_back(std::move(material));
		}

		Material* Get(SceneRenderer& gpu, const std::string& name) const
		{
			return gpu.Renderer.GetMaterial("Materials/NyxVertexOffsetProbe" + name + ".nyxmat");
		}
	};

	enum class ESceneLook : uint8_t
	{
		// The materials' base colour as it is
		BaseColor,
		// Lit
		Lit,
	};

	// A world seen by a camera 6 m in front of the origin, looking at it, in a view of its own
	struct TestScene
	{
		SceneRenderer& Gpu;
		Registry World;
		Entity Camera;
		uint64_t View = 0;

		explicit TestScene(SceneRenderer& gpu, ESceneLook look = ESceneLook::BaseColor)
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
			if (look == ESceneLook::BaseColor)
			{
				renderer.SetSceneViewDebug(View, ESceneViewDebug::BaseColor);
			}
		}

		~TestScene()
		{
			IEditorRenderer& renderer = Gpu.Renderer;
			renderer.SetSelectedEntity(std::nullopt);
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

		Entity AddBuiltIn(const TransformComponent& transform, Mesh* mesh, Material* material)
		{
			const Entity entity = World.CreateEntity();
			World.Add<TransformComponent>(entity, transform);
			MeshRendererComponent meshRenderer;
			meshRenderer.MeshAsset = mesh;
			meshRenderer.MaterialAsset = material;
			World.Add<MeshRendererComponent>(entity, meshRenderer);
			return entity;
		}

		// The entity a click on a point of the world picks; none for nothing. After a frame of the
		// view with its overlays, which takes clicks from then on.
		std::optional<Entity> PickAt(const glm::vec3& point)
		{
			Gpu.DrawFrames(1);
			const glm::vec2 pixel = ScreenPointOf(point);
			IEditorRenderer& renderer = Gpu.Renderer;
			renderer.RequestPick(View, static_cast<uint32_t>(pixel.x), static_cast<uint32_t>(pixel.y));
			Gpu.DrawFrames(3);
			const IEditorRenderer::PickResult pick = renderer.ConsumeLastPickResult(View);
			Require(pick.bHasNewResult, "The click should have a result");
			return pick.HitEntity;
		}

		const SceneViewCullingStats& Culling() const
		{
			const SceneViewCullingStats* stats = Gpu.Renderer.GetSceneViewCullingStats(View);
			Require(stats != nullptr, "The view has no culling statistics");
			return *stats;
		}

		RgbaImage Draw(int frames = 2)
		{
			Gpu.DrawFrames(frames);
			RgbaImage image;
			Require(Gpu.Renderer.ReadSceneViewPixels(View, image), "The scene view can't be read back");
			return image;
		}

		// Where a point of the world is in the picture
		glm::vec2 ScreenPointOf(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel =
				WorldToScreenPoint(World.Get<TransformComponent>(Camera), World.Get<CameraComponent>(Camera), glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel.has_value(), "The point should be in front of the camera");
			return *pixel;
		}

		std::array<uint8_t, 3> PixelOf(const RgbaImage& image, const glm::vec3& point) const
		{
			const glm::vec2 pixel = ScreenPointOf(point);
			return PixelAt(image, static_cast<uint32_t>(pixel.x), static_cast<uint32_t>(pixel.y));
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

	// How many pixels differ by more than `tolerance` in a channel
	size_t CountDifferent(const RgbaImage& a, const RgbaImage& b, int tolerance)
	{
		size_t different = 0;
		for (size_t pixel = 0; pixel < a.Pixels.size(); pixel += 4)
		{
			for (size_t channel = 0; channel < 3; ++channel)
			{
				if (std::abs(int(a.Pixels[pixel + channel]) - int(b.Pixels[pixel + channel])) > tolerance)
				{
					++different;
					break;
				}
			}
		}
		return different;
	}

	// A ground under a sun that casts shadows from the left, looked down on from the front, and a
	// cube a metre above it
	struct ShadowedCube
	{
		TestScene Scene;
		Entity Ground;
		Entity Cube;

		ShadowedCube(SceneRenderer& gpu, const glm::vec3& cubeAt, Material* cubeMaterial)
			: Scene(gpu, ESceneLook::Lit)
		{
			Scene.World.Get<TransformComponent>(Scene.Camera) = TransformComponent{ .Position = glm::vec3(0.0f, 4.0f, 8.0f),
				.Rotation = glm::quat(glm::radians(glm::vec3(-25.0f, 0.0f, 0.0f))),
				.Scale = glm::vec3(1.0f) };
			Ground = Scene.AddBuiltIn(TransformComponent{ .Position = glm::vec3(0.0f), .Rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f), .Scale = glm::vec3(20.0f, 1.0f, 20.0f) },
				gpu.Renderer.GetPlaneMesh(), gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat"));
			Cube = Scene.AddBuiltIn(At(cubeAt), gpu.Renderer.GetCubeMesh(), cubeMaterial);
			const Entity sun = Scene.World.CreateEntity();
			Scene.World.Add<TransformComponent>(sun, TransformComponent{ .Position = glm::vec3(0.0f, 10.0f, 0.0f),
				.Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, -70.0f, 0.0f))),
				.Scale = glm::vec3(1.0f) });
			Scene.World.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});
		}
	};

	// The box around the pixels that differ between two pictures by more than `tolerance`
	struct ChangedArea
	{
		glm::vec2 Min{ 1e9f };
		glm::vec2 Max{ -1e9f };
		size_t Pixels = 0;

		ChangedArea(const RgbaImage& a, const RgbaImage& b, int tolerance)
		{
			for (uint32_t y = 0; y < a.Height; ++y)
			{
				for (uint32_t x = 0; x < a.Width; ++x)
				{
					if (!Near(PixelAt(a, x, y), PixelAt(b, x, y), tolerance))
					{
						Min = glm::min(Min, glm::vec2(x, y));
						Max = glm::max(Max, glm::vec2(x, y));
						++Pixels;
					}
				}
			}
		}

		bool Holds(const glm::vec2& pixel) const
		{
			return pixel.x >= Min.x && pixel.y >= Min.y && pixel.x <= Max.x && pixel.y <= Max.y;
		}
	};

	// A cube its material moves 2 m along x, against an unmoved cube put there: the same picture,
	// its shadow included; picked and outlined there, not where it stands
	void TestAVertexFunctionMovesItAll(SceneRenderer& gpu, const OffsetProbe& probe)
	{
		const glm::vec3 there(1.0f, 1.5f, 0.0f);
		const glm::vec3 standing(-1.0f, 1.5f, 0.0f);
		RgbaImage unmoved;
		{
			ShadowedCube scene(gpu, there, probe.Get(gpu, "Still"));
			gpu.DrawUntilShadersCompiled();
			unmoved = scene.Scene.Draw();
		}
		RgbaImage empty;
		{
			ShadowedCube scene(gpu, there, probe.Get(gpu, "Still"));
			scene.Scene.World.Get<MeshRendererComponent>(scene.Cube).bVisible = false;
			empty = scene.Scene.Draw();
		}

		ShadowedCube scene(gpu, standing, probe.Get(gpu, "Moved"));
		gpu.DrawUntilShadersCompiled();
		const RgbaImage moved = scene.Scene.Draw();
		SaveIfAsked(unmoved, "Unmoved");
		SaveIfAsked(moved, "Moved");
		const size_t cubeAndShadow = CountDifferent(unmoved, empty, 8);
		Require(cubeAndShadow > 800, "The cube and its shadow should change the picture, not only " + std::to_string(cubeAndShadow) + " pixels");
		const size_t different = CountDifferent(moved, unmoved, 6);
		Require(different < 40, "The moved cube, or its shadow, differs from an unmoved one there in " + std::to_string(different) + " pixels");
		const SceneViewShadows* shadows = gpu.Renderer.GetSceneViewShadows(scene.Scene.View);
		Require(shadows && shadows->CascadeCount > 0, "The view should draw shadows");

		// In the editor
		gpu.Renderer.SetSceneViewShowEditorOverlays(scene.Scene.View, true);
		Require(scene.Scene.PickAt(there) == scene.Cube, "A click where the cube is drawn should pick it");
		Require(scene.Scene.PickAt(standing) != scene.Cube, "A click where the cube stands, but isn't drawn, shouldn't pick it");
		const RgbaImage unselected = scene.Scene.Draw();
		gpu.Renderer.SetSelectedEntity(scene.Cube);
		const RgbaImage selected = scene.Scene.Draw();
		SaveIfAsked(selected, "MovedSelected");
		const ChangedArea outline(selected, unselected, 8);
		Require(outline.Pixels > 50, "The selected cube should be outlined");
		Require(outline.Holds(scene.Scene.ScreenPointOf(there)) && !outline.Holds(scene.Scene.ScreenPointOf(standing)),
			"The cube should be outlined where it is drawn, not where it stands");
	}

	// A cube 9 m to the left, out of view, moved 10 m into view: drawn, as its material widens its
	// bounds by 10.5 m, also cell by cell when instanced; without the widening, culled
	void TestBoundsAreWidened(SceneRenderer& gpu, const OffsetProbe& probe)
	{
		const glm::vec3 standing(-9.0f, 0.0f, 0.0f);
		const glm::vec3 drawn(1.0f, 0.0f, 0.0f);
		RgbaImage empty;
		{
			TestScene scene(gpu);
			empty = scene.Draw();
		}
		{
			TestScene scene(gpu);
			scene.AddBuiltIn(At(standing), gpu.Renderer.GetCubeMesh(), probe.Get(gpu, "Far"));
			gpu.DrawUntilShadersCompiled();
			const RgbaImage image = scene.Draw();
			Require(scene.Culling().ObjectsCulled == 0, "A cube its material moves into view shouldn't be culled");
			RequireShows(scene.PixelOf(image, drawn), glm::vec3(0.8f, 0.3f, 0.2f), "The cube moved into view");
		}
		{
			TestScene scene(gpu);
			scene.AddBuiltIn(At(standing), gpu.Renderer.GetCubeMesh(), probe.Get(gpu, "FarUnbounded"));
			gpu.DrawUntilShadersCompiled();
			const RgbaImage image = scene.Draw();
			Require(scene.Culling().ObjectsCulled == 1, "Without its Max Vertex Offset, the cube should be culled by where it stands");
			Require(Near(scene.PixelOf(image, drawn), scene.PixelOf(empty, drawn), 2), "A culled cube shouldn't be drawn");
		}
		{
			TestScene scene(gpu);
			auto instances = std::make_shared<MeshInstances>();
			instances->Transforms = { glm::translate(glm::mat4(1.0f), standing) };
			instances->Cells = { MeshInstanceCell{ .BoundsMin = standing, .BoundsMax = standing, .First = 0, .Count = 1 } };
			const Entity field = scene.AddBuiltIn(At(glm::vec3(0.0f)), gpu.Renderer.GetCubeMesh(), probe.Get(gpu, "Far"));
			MeshInstancesComponent component;
			SetMeshInstances(component, instances);
			scene.World.Add<MeshInstancesComponent>(field, component);
			const RgbaImage image = scene.Draw();
			Require(scene.Culling().CellsCulled == 0 && scene.Culling().Instances == 1, "A cell its material moves into view shouldn't be culled");
			RequireShows(scene.PixelOf(image, drawn), glm::vec3(0.8f, 0.3f, 0.2f), "The instance moved into view");
		}
	}

	// The function moves each instance up by its custom y, an entity drawn once by its own (also in
	// picking, which draws it from the identity buffer), and shrinks a mesh by its vertices' custom y
	void TestTheFunctionGetsTheNumbers(SceneRenderer& gpu, const OffsetProbe& probe)
	{
		const glm::vec3 colour(0.8f, 0.3f, 0.2f);
		RgbaImage empty;
		{
			TestScene scene(gpu);
			empty = scene.Draw();
		}
		TestScene scene(gpu);
		Material* material = probe.Get(gpu, "Numbers");
		auto instances = std::make_shared<MeshInstances>();
		instances->Transforms = { glm::translate(glm::mat4(1.0f), glm::vec3(-2.0f, 0.0f, 0.0f)), glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f, 0.0f, 0.0f)) };
		instances->Custom = { glm::vec4(0.0f), glm::vec4(0.0f, 1.2f, 0.0f, 0.0f) };
		const Entity field = scene.AddMesh(At(glm::vec3(0.0f)), MakeSquare(0.6f, glm::vec4(0.0f)), material);
		MeshInstancesComponent component;
		SetMeshInstances(component, instances);
		scene.World.Add<MeshInstancesComponent>(field, component);
		const Entity raised = scene.AddMesh(At(glm::vec3(1.0f, 0.0f, 0.0f)), MakeSquare(0.6f, glm::vec4(0.0f)), material);
		scene.World.Get<MeshRendererComponent>(raised).Custom = glm::vec4(0.0f, 1.2f, 0.0f, 0.0f);
		scene.AddMesh(At(glm::vec3(2.5f, 0.0f, 0.0f)), MakeSquare(0.6f, glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)), material);
		gpu.DrawUntilShadersCompiled();
		const RgbaImage image = scene.Draw();
		SaveIfAsked(image, "FunctionNumbers");

		RequireShows(scene.PixelOf(image, glm::vec3(-2.0f, 0.0f, 0.0f)), colour, "The instance without numbers, where it stands,");
		RequireShows(scene.PixelOf(image, glm::vec3(-0.5f, 1.2f, 0.0f)), colour, "The instance raised by its numbers");
		Require(Near(scene.PixelOf(image, glm::vec3(-0.5f, 0.0f, 0.0f)), scene.PixelOf(empty, glm::vec3(-0.5f, 0.0f, 0.0f)), 2), "Where the raised instance stands, nothing should be drawn");
		RequireShows(scene.PixelOf(image, glm::vec3(1.0f, 1.2f, 0.0f)), colour, "The entity raised by its numbers");
		Require(Near(scene.PixelOf(image, glm::vec3(1.0f, 0.0f, 0.0f)), scene.PixelOf(empty, glm::vec3(1.0f, 0.0f, 0.0f)), 2), "Where the raised entity stands, nothing should be drawn");
		Require(Near(scene.PixelOf(image, glm::vec3(2.5f, 0.0f, 0.0f)), scene.PixelOf(empty, glm::vec3(2.5f, 0.0f, 0.0f)), 2), "A mesh shrunk to nothing by its vertices' numbers shouldn't be drawn");

		// Picked where it is drawn: the editor draws an entity drawn once from the identity buffer,
		// its numbers with the draw
		gpu.Renderer.SetSceneViewShowEditorOverlays(scene.View, true);
		Require(scene.PickAt(glm::vec3(1.0f, 1.2f, 0.0f)) == raised, "A click on the raised entity should pick it");
		Require(scene.PickAt(glm::vec3(1.0f, 0.0f, 0.0f)) != raised, "A click where the raised entity stands shouldn't pick it");
		Require(scene.PickAt(glm::vec3(-0.5f, 1.2f, 0.0f)) == field, "A click on the raised instance should pick its entity");
	}

	// Moved by the time, the mesh stands still while the world's clock is paused, and moves on a
	// step with it
	void TestTheFunctionStandsStillWhilePaused(SceneRenderer& gpu, const OffsetProbe& probe)
	{
		TestScene scene(gpu);
		scene.AddBuiltIn(At(glm::vec3(0.0f)), gpu.Renderer.GetCubeMesh(), probe.Get(gpu, "Sway"));
		gpu.DrawUntilShadersCompiled();
		FixedStepClock clock;
		clock.SetStepSeconds(0.1);
		clock.Advance(0.1);
		gpu.Renderer.SetWorldInterpolation(&scene.World, &clock);
		clock.SetPaused(true);
		const RgbaImage paused = scene.Draw();
		for (int frame = 0; frame < 3; ++frame)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			clock.Advance(0.5);
			Require(scene.Draw(1).Pixels == paused.Pixels, "Paused, the mesh its function moves by the time should stand still");
		}
		clock.StepOnce();
		clock.Advance(0.0);
		Require(CountDifferent(scene.Draw(1), paused, 8) > 50, "A step on, the mesh should have moved");
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
		const OffsetProbe offsetProbe;
		TestAVertexFunctionMovesItAll(gpu, offsetProbe);
		TestBoundsAreWidened(gpu, offsetProbe);
		TestTheFunctionGetsTheNumbers(gpu, offsetProbe);
		TestTheFunctionStandsStillWhilePaused(gpu, offsetProbe);
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All vertex data GPU tests passed\n";
	return 0;
}
