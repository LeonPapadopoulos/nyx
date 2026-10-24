// Particles on a real GPU (ParticleEmitterComponent), their time a fixed step clock's, stepped by
// the test, so every run draws the same particles. One big particle facing the camera against the
// CPU's numbers, tonemapped: additive over black and over a lit wall, alpha covering it, its
// emissive intensity, lit by a point light or dark without one; hidden behind a box; fading where
// it meets the floor with a soft distance, not without. Paused, the picture stands still, and Step
// moves it on; a game's velocity field carries the particles; an emitter out of view is culled, the
// picture the same; a click on an emitter's icon selects it. Sparks, smoke and small spinning
// meshes look as their reference picture does, and the particles test scene loads with its
// emitters in view.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, e.g. to make a new reference
// after a change that is meant to change the picture. NyxParticleGpuTests --write-scene writes
// Assets/Scenes/ParticlesTest.nyxscene, the lights test scene with a fire's flames, embers and
// smoke in a wind.
#include "AssetResolver.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "FixedStepClock.h"
#include "GuidComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "ParticleEmitterComponent.h"
#include "ParticleVelocityField.h"
#include "Particles.h"
#include "Paths.h"
#include "PointLightComponent.h"
#include "SceneSerializer.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;
	constexpr double Step = 1.0 / 60.0;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("Particles-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, const glm::ivec2& pixel)
	{
		const size_t offset = (static_cast<size_t>(pixel.y) * image.Width + pixel.x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	int Brightness(const std::array<uint8_t, 3>& rgb)
	{
		return int(rgb[0]) + int(rgb[1]) + int(rgb[2]);
	}

	size_t DifferentPixels(const RgbaImage& a, const RgbaImage& b)
	{
		size_t different = 0;
		for (size_t pixel = 0; pixel < a.Pixels.size(); pixel += 4)
		{
			different += std::memcmp(&a.Pixels[pixel], &b.Pixels[pixel], 3) != 0 ? 1 : 0;
		}
		return different;
	}

	TransformComponent Placed(const glm::vec3& position, const glm::vec3& scale = glm::vec3(1.0f), const glm::quat& rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f))
	{
		return TransformComponent{ .Position = position, .Rotation = rotation, .Scale = scale };
	}

	Entity AddMesh(Registry& world, const TransformComponent& transform, Mesh* mesh, Material* material)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, transform);
		MeshRendererComponent meshRenderer;
		meshRenderer.MeshAsset = mesh;
		meshRenderer.MaterialAsset = material;
		world.Add<MeshRendererComponent>(entity, meshRenderer);
		return entity;
	}

	Entity AddEmitter(Registry& world, const glm::vec3& position, const ParticleEmitterComponent& emitter)
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, Placed(position));
		world.Add<ParticleEmitterComponent>(entity, emitter);
		return entity;
	}

	// One particle, born at once, still and living long: a round quad `size` across of `color` and
	// `alpha`, its edge fading only at its rim
	ParticleEmitterComponent OneParticle(EParticleBlend blend, const glm::vec3& color, float alpha, float size)
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = 0.0f;
		emitter.Bursts = { ParticleBurst{ .Time = 0.0f, .Count = 1 } };
		emitter.Lifetime = glm::vec2(1000.0f, 1000.0f);
		emitter.SpawnRadius = 0.0f;
		emitter.Speed = glm::vec2(0.0f, 0.0f);
		emitter.Drag = 0.0f;
		emitter.Size = glm::vec2(size, size);
		emitter.ColorOverLife = { ParticleColorKey{ .Time = 0.0f, .Color = color, .Alpha = alpha } };
		emitter.Blend = blend;
		emitter.SoftDistance = 0.0f;
		return emitter;
	}

	// A camera 6 m from a wall facing it, a floor below, under a black sky and a sun that gives no
	// light, the world's time a clock's the test steps
	struct ParticleScene
	{
		Registry World;
		LiveMaterialFile BlackFile{ "Materials/ParticleTestBlack.nyxmat" };
		LiveMaterialFile GreyFile{ "Materials/ParticleTestGrey.nyxmat" };
		Material* Black = nullptr;
		Material* Grey = nullptr;
		Entity Wall;
		Entity Floor;
		Entity Camera;
		CameraComponent Lens;
		FixedStepClock Clock;

		explicit ParticleScene(VulkanRenderer& renderer)
		{
			BlackFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\n");
			GreyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.5 0.5 0.5\nMetallic = 0\nRoughness = 0.9\n");
			Black = renderer.GetMaterial(BlackFile.Path);
			Grey = renderer.GetMaterial(GreyFile.Path);
			Wall = AddMesh(World, Placed(glm::vec3(0.0f, 0.0f, -6.0f), glm::vec3(100.0f, 1.0f, 100.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
				renderer.GetPlaneMesh(), Black);
			Floor = AddMesh(World, Placed(glm::vec3(0.0f, -1.5f, 0.0f), glm::vec3(100.0f, 1.0f, 100.0f)), renderer.GetPlaneMesh(), Black);

			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f)));
			World.Add<CameraComponent>(Camera, Lens);

			const Entity sun = World.CreateEntity();
			World.Add<TransformComponent>(sun, Placed(glm::vec3(0.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f)))));
			DirectionalLightComponent light;
			light.Intensity = 0.0f;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(sun, light);
			Clock.SetStepSeconds(Step);
		}

		glm::ivec2 Pixel(const glm::vec3& point) const
		{
			const std::optional<glm::vec2> pixel = WorldToScreenPoint(World.Get<TransformComponent>(Camera), Lens, glm::vec2(ViewWidth, ViewHeight), point);
			Require(pixel && pixel->x >= 0.0f && pixel->y >= 0.0f && pixel->x < ViewWidth && pixel->y < ViewHeight, "A point the test looks at is out of the picture");
			return glm::ivec2(glm::floor(*pixel));
		}
	};

	// `steps` steps of the world's clock, a frame each, then the picture
	RgbaImage Draw(SceneRenderer& gpu, ParticleScene& scene, uint64_t view, int steps = 2)
	{
		for (int step = 0; step < steps; ++step)
		{
			scene.Clock.Advance(Step);
			gpu.DrawFrames(1);
		}
		gpu.DrawFrames(1);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	uint64_t MakeView(SceneRenderer& gpu, ParticleScene& scene, ESceneViewSky sky)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&scene.World);
		renderer.SetWorldInterpolation(&scene.World, &scene.Clock);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		renderer.SetSceneViewSky(view, sky, 0.0f);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, ParticleScene& scene, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorldInterpolation(&scene.World, nullptr);
		renderer.SetWorldParticleVelocityField(&scene.World, nullptr);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	void RequireNear(const std::array<uint8_t, 3>& actual, const std::array<uint8_t, 3>& expected, int tolerance, const std::string& what)
	{
		Require(Near(actual, expected, tolerance), what + ": " + Text(actual) + ", expected " + Text(expected));
	}

	// One big particle 3 m ahead, its middle in the middle of the picture: additive and alpha, over
	// black and over light, brighter by its emissive intensity, lit by a point light or not
	void TestOneParticle(SceneRenderer& gpu)
	{
		ParticleScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene, ESceneViewSky::Black);
		const glm::vec3 position(0.0f, 0.0f, -3.0f);
		const glm::ivec2 middle = scene.Pixel(position);

		// Additive over the black wall: its colour, the rim's fade nothing at the middle
		const Entity emitter = AddEmitter(scene.World, position, OneParticle(EParticleBlend::Additive, glm::vec3(0.5f, 0.3f, 0.1f), 1.0f, 2.0f));
		const RgbaImage additive = Draw(gpu, scene, view);
		SaveIfAsked(additive, "Additive");
		RequireNear(PixelAt(additive, middle), Tonemapped(glm::vec3(0.5f, 0.3f, 0.1f)), 3, "An additive particle over black");
		// Softer towards its rim, nothing past it
		Require(Brightness(PixelAt(additive, scene.Pixel(position + glm::vec3(0.7f, 0.0f, 0.0f)))) < Brightness(PixelAt(additive, middle)), "No softer towards the rim");
		RequireNear(PixelAt(additive, scene.Pixel(position + glm::vec3(1.1f, 0.0f, 0.0f))), { 0, 0, 0 }, 0, "Past the rim");

		// Half its alpha: half its light; twice the emissive intensity: twice
		ParticleEmitterComponent& settings = scene.World.Get<ParticleEmitterComponent>(emitter);
		settings.ColorOverLife[0].Alpha = 0.5f;
		RequireNear(PixelAt(Draw(gpu, scene, view), middle), Tonemapped(glm::vec3(0.25f, 0.15f, 0.05f)), 3, "Additive at half alpha");
		settings.ColorOverLife[0].Alpha = 1.0f;
		settings.EmissiveIntensity = 2.0f;
		RequireNear(PixelAt(Draw(gpu, scene, view), middle), Tonemapped(glm::vec3(1.0f, 0.6f, 0.2f)), 3, "Additive at emissive intensity 2");
		settings.EmissiveIntensity = 1.0f;

		// Over a lit wall: added to it
		const Entity lamp = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(lamp, Placed(glm::vec3(0.0f, 0.0f, -5.0f)));
		scene.World.Add<PointLightComponent>(lamp, PointLightComponent{ .Intensity = 1.0f, .Range = 20.0f });
		scene.World.Get<MeshRendererComponent>(scene.Wall).MaterialAsset = scene.Grey;
		const RgbaImage withParticle = Draw(gpu, scene, view);
		settings.ColorOverLife[0].Alpha = 0.0f;
		const RgbaImage wallAlone = Draw(gpu, scene, view);
		settings.ColorOverLife[0].Alpha = 1.0f;
		Require(Brightness(PixelAt(withParticle, middle)) > Brightness(PixelAt(wallAlone, middle)) + 60, "An additive particle doesn't add to the lit wall");

		// Alpha, wholly opaque: covers the lit wall with its colour
		settings.Blend = EParticleBlend::Alpha;
		const RgbaImage covering = Draw(gpu, scene, view);
		SaveIfAsked(covering, "Alpha");
		RequireNear(PixelAt(covering, middle), Tonemapped(glm::vec3(0.5f, 0.3f, 0.1f)), 3, "An opaque alpha particle over the lit wall");
		// Half covering: halfway between, in linear light; the wall's light as it is beside it
		settings.ColorOverLife[0].Alpha = 0.5f;
		const RgbaImage half = Draw(gpu, scene, view);
		Require(Brightness(PixelAt(half, middle)) > Brightness(PixelAt(covering, middle)) - 200 && PixelAt(half, middle) != PixelAt(covering, middle),
			"A half covering alpha particle covers as an opaque one");
		settings.ColorOverLife[0].Alpha = 1.0f;

		// Lit: dark with nothing to light it, over the black wall
		scene.World.Get<PointLightComponent>(lamp).Intensity = 0.0f;
		scene.World.Get<MeshRendererComponent>(scene.Wall).MaterialAsset = scene.Black;
		settings.bLit = true;
		settings.Blend = EParticleBlend::Additive;
		RequireNear(PixelAt(Draw(gpu, scene, view), middle), { 0, 0, 0 }, 0, "A lit particle with no light on it");
		// A red light 1 m beside it: lit red, as a white surface facing the light at that distance
		const Entity red = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(red, Placed(position + glm::vec3(1.0f, 0.0f, 0.0f)));
		scene.World.Add<PointLightComponent>(red, PointLightComponent{ .Color = glm::vec3(1.0f, 0.0f, 0.0f), .Intensity = 1.0f, .Range = 10.0f });
		const float falloff = GetPointLightFalloff(1.0f, 10.0f);
		RequireNear(PixelAt(Draw(gpu, scene, view), middle), Tonemapped(glm::vec3(0.5f * falloff, 0.0f, 0.0f)), 3, "A lit particle by a red light");
		EndView(gpu, scene, view);
	}

	// Behind a box, hidden; meeting the floor, faded over the soft distance, not without one
	void TestDepth(SceneRenderer& gpu)
	{
		ParticleScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene, ESceneViewSky::Black);
		const glm::vec3 position(0.0f, -1.5f, -5.0f);
		ParticleEmitterComponent settings = OneParticle(EParticleBlend::Additive, glm::vec3(0.8f), 1.0f, 2.0f);
		const Entity emitter = AddEmitter(scene.World, position, settings);

		// Its middle on the floor: just above the floor's line, hard without a soft distance, faded
		// with one
		const glm::ivec2 above = scene.Pixel(position + glm::vec3(0.0f, 0.03f, 0.0f));
		const RgbaImage hard = Draw(gpu, scene, view);
		SaveIfAsked(hard, "Hard");
		scene.World.Get<ParticleEmitterComponent>(emitter).SoftDistance = 3.0f;
		const RgbaImage soft = Draw(gpu, scene, view);
		SaveIfAsked(soft, "Soft");
		Require(Brightness(PixelAt(hard, above)) > 300, "Above the floor, the hard particle isn't bright: " + Text(PixelAt(hard, above)));
		Require(Brightness(PixelAt(soft, above)) * 3 < Brightness(PixelAt(hard, above)), "Where it meets the floor, the soft particle doesn't fade: " + Text(PixelAt(soft, above)));
		// Below the floor's line, behind the floor: nothing either way
		const glm::ivec2 below = scene.Pixel(position + glm::vec3(0.0f, -0.3f, 0.3f));
		RequireNear(PixelAt(hard, below), { 0, 0, 0 }, 0, "Below the floor, the particle shows");

		// Behind a box: hidden where the box is. Born again at its new place: a particle in the world
		// stays where it was born
		scene.World.Get<TransformComponent>(emitter).Position = glm::vec3(0.0f, 0.0f, -4.0f);
		scene.World.Get<ParticleEmitterComponent>(emitter).SoftDistance = 0.0f;
		scene.World.Get<ParticleEmitterComponent>(emitter).Seed = 4;
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(0.5f)), gpu.Renderer.GetCubeMesh(), scene.Black);
		const RgbaImage hidden = Draw(gpu, scene, view);
		SaveIfAsked(hidden, "Hidden");
		const glm::ivec2 middle = scene.Pixel(glm::vec3(0.0f, 0.0f, -4.0f));
		RequireNear(PixelAt(hidden, middle), { 0, 0, 0 }, 0, "A particle behind a box shows through it");
		Require(Brightness(PixelAt(hidden, scene.Pixel(glm::vec3(0.6f, 0.0f, -4.0f)))) > 200, "Beside the box, the particle doesn't show");
		EndView(gpu, scene, view);
	}

	// Paused, still; stepped, moved on; carried by a game's velocity field; culled out of view
	struct Updraft : IParticleVelocityField
	{
		void SampleVelocities(std::span<const glm::vec3>, float, std::span<glm::vec3> outVelocities) const override
		{
			for (glm::vec3& velocity : outVelocities)
			{
				velocity = glm::vec3(4.0f, 0.0f, 0.0f);
			}
		}
	};

	void TestClockFieldAndCulling(SceneRenderer& gpu)
	{
		ParticleScene scene(gpu.Renderer);
		const uint64_t view = MakeView(gpu, scene, ESceneViewSky::Black);
		ParticleEmitterComponent sparks;
		sparks.Rate = 80.0f;
		sparks.Lifetime = glm::vec2(1.0f, 1.5f);
		sparks.Speed = glm::vec2(0.5f, 1.0f);
		sparks.Size = glm::vec2(0.15f, 0.2f);
		sparks.EmissiveIntensity = 3.0f;
		sparks.Drag = 2.0f;
		const Entity emitter = AddEmitter(scene.World, glm::vec3(0.0f, -1.0f, -4.0f), sparks);
		Draw(gpu, scene, view, 60);

		// Paused: frame after frame the same; Step: on by one step
		scene.Clock.SetPaused(true);
		const RgbaImage paused = Draw(gpu, scene, view, 1);
		const RgbaImage stillPaused = Draw(gpu, scene, view, 3);
		Require(paused.Pixels == stillPaused.Pixels, "The particles move while the game is paused");
		scene.Clock.StepOnce();
		const RgbaImage stepped = Draw(gpu, scene, view, 1);
		Require(DifferentPixels(stepped, paused) > 20, "Step doesn't move the particles on");
		scene.Clock.SetPaused(false);

		// Carried along +x by a game's air
		Updraft updraft;
		gpu.Renderer.SetWorldParticleVelocityField(&scene.World, &updraft);
		const RgbaImage carried = Draw(gpu, scene, view, 60);
		SaveIfAsked(carried, "Carried");
		const auto brightnessOnSide = [&](const RgbaImage& image, bool bRight)
		{
			const int middle = scene.Pixel(glm::vec3(0.0f, -1.0f, -4.0f)).x;
			int sum = 0;
			for (uint32_t y = 0; y < image.Height; ++y)
			{
				for (uint32_t x = 0; x < image.Width; ++x)
				{
					if ((static_cast<int>(x) > middle + 20) == bRight && (static_cast<int>(x) < middle - 20) == !bRight)
					{
						sum += Brightness(PixelAt(image, glm::ivec2(x, y)));
					}
				}
			}
			return sum;
		};
		Require(brightnessOnSide(carried, true) > brightnessOnSide(carried, false) * 4, "The velocity field doesn't carry the particles along +x");
		gpu.Renderer.SetWorldParticleVelocityField(&scene.World, nullptr);

		// Out of view: culled, the picture without it
		scene.World.Get<TransformComponent>(emitter).Position = glm::vec3(0.0f, 0.0f, 30.0f);
		scene.World.Get<ParticleEmitterComponent>(emitter).Seed = 5;
		Draw(gpu, scene, view, 30);
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->ParticleEmitters == 1 && stats->ParticleEmittersCulled == 1 && stats->Particles == 0, "The emitter out of view isn't culled");
		const RgbaImage culled = Draw(gpu, scene, view, 1);
		for (size_t pixel = 0; pixel < culled.Pixels.size(); pixel += 4)
		{
			Require(culled.Pixels[pixel] == 0 && culled.Pixels[pixel + 1] == 0 && culled.Pixels[pixel + 2] == 0, "An emitter out of view draws something");
		}
		EndView(gpu, scene, view);
	}

	// In the editor's scene views, a click on an emitter's icon selects it
	void TestEditorIcon(SceneRenderer& gpu)
	{
		ParticleScene scene(gpu.Renderer);
		ParticleEmitterComponent quiet = OneParticle(EParticleBlend::Additive, glm::vec3(0.2f), 1.0f, 0.1f);
		quiet.Bursts.clear();
		const glm::vec3 position(1.0f, 0.0f, -3.0f);
		const Entity emitter = AddEmitter(scene.World, position, quiet);
		const uint64_t view = MakeView(gpu, scene, ESceneViewSky::Black);
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetSceneViewShowEditorOverlays(view, true);
		Draw(gpu, scene, view);
		const glm::ivec2 icon = scene.Pixel(position);
		renderer.RequestPick(view, icon.x, icon.y);
		Draw(gpu, scene, view, 3);
		const IEditorRenderer::PickResult pick = renderer.ConsumeLastPickResult(view);
		Require(pick.bHasNewResult && pick.HitEntity == emitter, "A click on an emitter's icon, without particles, doesn't pick it");
		EndView(gpu, scene, view);
	}

	// Sparks, smoke drifting in a wind, and small spinning glowing cubes over a dark floor, by a
	// warm light, after 1.5 s, against References/Particles.png
	void TestParticlesScene(SceneRenderer& gpu)
	{
		ParticleScene scene(gpu.Renderer);
		scene.World.Get<MeshRendererComponent>(scene.Floor).MaterialAsset = scene.Grey;
		scene.World.Get<TransformComponent>(scene.Camera) = Placed(glm::vec3(0.0f, 0.5f, 2.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-8.0f, 0.0f, 0.0f))));
		const Entity air = scene.World.CreateEntity();
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bSkyFollowsSun = false;
		environment.bAmbientOcclusion = false;
		environment.Wind = glm::vec3(0.8f, 0.0f, 0.0f);
		scene.World.Add<EnvironmentComponent>(air, environment);
		const Entity lamp = scene.World.CreateEntity();
		scene.World.Add<TransformComponent>(lamp, Placed(glm::vec3(0.0f, -1.0f, -3.0f)));
		scene.World.Add<PointLightComponent>(lamp, PointLightComponent{ .Color = glm::vec3(1.0f, 0.6f, 0.3f), .Intensity = 2.0f, .Range = 5.0f });

		ParticleEmitterComponent sparks;
		sparks.Rate = 120.0f;
		sparks.Lifetime = glm::vec2(0.6f, 1.0f);
		sparks.ConeAngle = 25.0f;
		sparks.Speed = glm::vec2(2.0f, 3.0f);
		sparks.Gravity = 4.0f;
		sparks.Drag = 0.3f;
		sparks.Size = glm::vec2(0.05f, 0.08f);
		sparks.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.8f, 0.4f), .Alpha = 1.0f }, { .Time = 1.0f, .Color = glm::vec3(1.0f, 0.2f, 0.05f), .Alpha = 0.0f } };
		sparks.EmissiveIntensity = 4.0f;
		sparks.Seed = 11;
		AddEmitter(scene.World, glm::vec3(-1.5f, -1.5f, -3.5f), sparks);

		ParticleEmitterComponent smoke;
		smoke.Rate = 25.0f;
		smoke.Lifetime = glm::vec2(2.0f, 3.0f);
		smoke.SpawnRadius = 0.15f;
		smoke.ConeAngle = 15.0f;
		smoke.Speed = glm::vec2(0.2f, 0.4f);
		smoke.Buoyancy = 0.6f;
		smoke.Drag = 0.8f;
		smoke.Spin = 30.0f;
		smoke.Size = glm::vec2(0.3f, 0.4f);
		smoke.SizeOverLife = { { .Time = 0.0f, .Size = 0.6f }, { .Time = 1.0f, .Size = 2.5f } };
		smoke.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(0.6f), .Alpha = 0.0f }, { .Time = 0.2f, .Color = glm::vec3(0.6f), .Alpha = 0.7f },
			{ .Time = 1.0f, .Color = glm::vec3(0.4f), .Alpha = 0.0f } };
		smoke.Blend = EParticleBlend::Alpha;
		smoke.bLit = true;
		smoke.Seed = 12;
		AddEmitter(scene.World, glm::vec3(0.0f, -1.4f, -3.5f), smoke);

		ParticleEmitterComponent embers;
		embers.Rate = 30.0f;
		embers.Lifetime = glm::vec2(1.0f, 1.5f);
		embers.ConeAngle = 40.0f;
		embers.Speed = glm::vec2(0.8f, 1.5f);
		embers.Buoyancy = 0.5f;
		embers.Drag = 1.0f;
		embers.Spin = 360.0f;
		embers.Shape = EParticleShape::Mesh;
		embers.Size = glm::vec2(0.04f, 0.07f);
		embers.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.5f, 0.1f), .Alpha = 1.0f }, { .Time = 1.0f, .Color = glm::vec3(0.6f, 0.1f, 0.0f), .Alpha = 1.0f } };
		embers.EmissiveIntensity = 3.0f;
		embers.Seed = 13;
		AddEmitter(scene.World, glm::vec3(1.5f, -1.4f, -3.5f), embers);

		const uint64_t view = MakeView(gpu, scene, ESceneViewSky::Black);
		const RgbaImage image = Draw(gpu, scene, view, 90);
		SaveIfAsked(image, "Scene");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "Particles.png");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->ParticleEmitters == 3 && stats->ParticleEmittersCulled == 0 && stats->Particles > 100, "The scene's particles aren't drawn");
		EndView(gpu, scene, view);
	}

	constexpr const char* ScenePath = "Scenes/ParticlesTest.nyxscene";

	Entity AddEntity(Registry& world, const std::string& name, uint64_t guid, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ guid } });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	// The lights test scene with a fire in front of the camera: flames rising and shrinking, embers
	// (small glowing cubes) thrown up, smoke rising and drifting in the environment's wind, lit by the
	// fire's light
	void WriteScene()
	{
		const std::filesystem::path assets = Paths::GetAssetsDir();
		Registry world;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(assets / "Scenes/LightsTest.nyxscene", world, context), "The lights test scene can't be loaded");
		world.Each<EnvironmentComponent>([&](Entity, EnvironmentComponent& environment)
			{
				environment.Wind = glm::vec3(0.8f, 0.0f, -0.4f);
			});

		const glm::vec3 fire(2.5f, 0.35f, 1.5f);
		uint64_t guid = 0xA7305F1AEull;

		ParticleEmitterComponent flames;
		flames.Rate = 90.0f;
		flames.Lifetime = glm::vec2(0.5f, 0.9f);
		flames.SpawnRadius = 0.2f;
		flames.ConeAngle = 12.0f;
		flames.Speed = glm::vec2(0.6f, 1.0f);
		flames.Buoyancy = 1.5f;
		flames.Drag = 0.5f;
		flames.Spin = 90.0f;
		flames.Size = glm::vec2(0.35f, 0.5f);
		flames.SizeOverLife = { { .Time = 0.0f, .Size = 0.8f }, { .Time = 0.3f, .Size = 1.0f }, { .Time = 1.0f, .Size = 0.2f } };
		flames.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.75f, 0.35f), .Alpha = 0.0f }, { .Time = 0.15f, .Color = glm::vec3(1.0f, 0.55f, 0.15f), .Alpha = 0.8f },
			{ .Time = 1.0f, .Color = glm::vec3(0.8f, 0.15f, 0.02f), .Alpha = 0.0f } };
		flames.EmissiveIntensity = 3.0f;
		flames.SoftDistance = 0.25f;
		flames.MaxParticles = 200;
		const Entity flamesEntity = AddEntity(world, "Fire Flames", guid++, Placed(fire));
		world.Add<ParticleEmitterComponent>(flamesEntity, flames);

		ParticleEmitterComponent embers;
		embers.Rate = 25.0f;
		embers.Lifetime = glm::vec2(1.2f, 2.2f);
		embers.SpawnRadius = 0.25f;
		embers.ConeAngle = 30.0f;
		embers.Speed = glm::vec2(1.0f, 2.2f);
		embers.Gravity = 1.0f;
		embers.Buoyancy = 0.8f;
		embers.Drag = 0.6f;
		embers.Spin = 400.0f;
		embers.Shape = EParticleShape::Mesh;
		embers.Size = glm::vec2(0.025f, 0.045f);
		embers.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.6f, 0.2f), .Alpha = 1.0f }, { .Time = 1.0f, .Color = glm::vec3(0.7f, 0.12f, 0.02f), .Alpha = 1.0f } };
		embers.EmissiveIntensity = 6.0f;
		embers.Seed = 2;
		const Entity embersEntity = AddEntity(world, "Fire Embers", guid++, Placed(fire));
		world.Add<ParticleEmitterComponent>(embersEntity, embers);

		ParticleEmitterComponent smoke;
		smoke.Rate = 12.0f;
		smoke.Lifetime = glm::vec2(4.0f, 6.0f);
		smoke.SpawnRadius = 0.2f;
		smoke.ConeAngle = 10.0f;
		smoke.Speed = glm::vec2(0.3f, 0.5f);
		smoke.Buoyancy = 0.35f;
		smoke.Drag = 0.4f;
		smoke.Spin = 20.0f;
		smoke.Size = glm::vec2(0.5f, 0.7f);
		smoke.SizeOverLife = { { .Time = 0.0f, .Size = 0.7f }, { .Time = 1.0f, .Size = 4.0f } };
		smoke.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(0.12f), .Alpha = 0.0f }, { .Time = 0.15f, .Color = glm::vec3(0.12f), .Alpha = 0.5f },
			{ .Time = 1.0f, .Color = glm::vec3(0.1f), .Alpha = 0.0f } };
		smoke.Blend = EParticleBlend::Alpha;
		smoke.bLit = true;
		smoke.SoftDistance = 0.5f;
		smoke.Seed = 3;
		const Entity smokeEntity = AddEntity(world, "Fire Smoke", guid++, Placed(fire + glm::vec3(0.0f, 0.6f, 0.0f)));
		world.Add<ParticleEmitterComponent>(smokeEntity, smoke);

		const Entity light = AddEntity(world, "Fire Light", guid++, Placed(fire + glm::vec3(0.0f, 0.5f, 0.0f)));
		world.Add<PointLightComponent>(light, PointLightComponent{ .Color = glm::vec3(1.0f, 0.55f, 0.2f), .Intensity = 3.0f, .Range = 9.0f });

		Require(SceneSerializer::SaveToFile(world, assets / ScenePath), "The scene can't be saved");
		std::cout << "Wrote " << (assets / ScenePath).string() << "\n";
	}

	// The particles test scene as the game loads it: its emitters drawn
	void TestParticlesTestScene(SceneRenderer& gpu)
	{
		Registry world;
		AssetResolver resolver(gpu.Renderer);
		ScenePostLoadContext context{};
		context.AssetResolver = &resolver;
		Require(SceneSerializer::LoadFromFile(Paths::GetAssetsDir() / ScenePath, world, context),
			"The particles test scene can't be loaded; NyxParticleGpuTests --write-scene writes it");
		size_t emitters = 0;
		world.Each<ParticleEmitterComponent>([&](Entity, const ParticleEmitterComponent&)
			{
				++emitters;
			});
		Require(emitters == 3, "The particles test scene has " + std::to_string(emitters) + " emitters, not 3");

		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, ViewWidth, ViewHeight);
		gpu.DrawUntilShadersCompiled();
		gpu.DrawFrames(30);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		SaveIfAsked(image, "TestScene");
		const SceneViewCullingStats* stats = gpu.Renderer.GetSceneViewCullingStats(view);
		Require(stats && stats->ParticleEmitters == 3 && stats->Particles > 0, "The particles test scene's particles aren't drawn");
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteScene();
			return 0;
		}
		SceneRenderer gpu("NyxParticleGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestOneParticle(gpu);
		TestDepth(gpu);
		TestClockFieldAndCulling(gpu);
		TestEditorIcon(gpu);
		TestParticlesScene(gpu);
		TestParticlesTestScene(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxParticleGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxParticleGpuTests: all passed\n";
	return 0;
}
