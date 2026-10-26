// The sky made by the air's scattering on a real GPU (VulkanSkyAtmosphere, EnvironmentComponent's
// Atmosphere mode). Its tables, read back: the transmittance as the CPU integrates it, all light
// through at the top of the air and less along longer rays, redder for a low sun; the sky bluer at
// noon than at sunset. The sky from 2 m and from 2 km up: up there the horizon is below eye level
// and the sky above is darker. Its light lights the world: a wall the sun doesn't reach shows the
// sky's irradiance and specular light as the tables have them, warmer towards a low sun. The fog's
// own colour is the sky's the way the ray looks, brighter towards the sun. The tables are made again only
// when the air, the sun or the camera's height changes. Without the component, and with the plain
// cube map, the picture is the same to the bit, and nothing of the sky is made. Its passes' GPU times
// at 1280 x 720 are printed. A time of day fading the set sun's light leaves the air's dusk and night
// as they are. The glow round a low sun is a halo, not a band up the sky, with fog or without; the
// night's light is none at 0, brighter above it, and no part of the noon sky.
//
// NYX_SCENE_TEST_OUTPUT=<folder> saves what was rendered there, for looking at by hand.
#include "CameraComponent.h"
#include "ComponentRegistration.h"
#include "DirectionalLightComponent.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "ImageFile.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "SkyAtmosphere.h"
#include "TimeOfDayComponent.h"
#include "TransformComponent.h"
#include "VulkanRenderer.h"
#include "RenderTestSupport.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::RenderTests;

	constexpr uint32_t ViewWidth = 480;
	constexpr uint32_t ViewHeight = 270;
	constexpr float Pi = 3.14159265358979f;

	void SaveIfAsked(const RgbaImage& image, const std::string& name)
	{
		const char* folder = std::getenv("NYX_SCENE_TEST_OUTPUT");
		if (folder == nullptr || *folder == '\0')
		{
			return;
		}
		const std::filesystem::path path = std::filesystem::path(folder) / ("SkyAtmosphere-" + name + ".png");
		std::string error;
		Require(SavePng(path, image, error), "Couldn't save " + path.string() + ": " + error);
		std::cout << "Saved " << path.string() << "\n";
	}

	std::array<uint8_t, 3> PixelAt(const RgbaImage& image, uint32_t x, uint32_t y)
	{
		x = std::min(x, image.Width - 1);
		y = std::min(y, image.Height - 1);
		const size_t offset = (static_cast<size_t>(y) * image.Width + x) * 4;
		return { image.Pixels[offset], image.Pixels[offset + 1], image.Pixels[offset + 2] };
	}

	float Luminance(const std::array<uint8_t, 3>& rgb)
	{
		return 0.2126f * rgb[0] + 0.7152f * rgb[1] + 0.0722f * rgb[2];
	}

	float Luminance(const glm::vec3& color)
	{
		return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	float BlueOverRed(const glm::vec3& color)
	{
		return color.b / std::max(color.r, 1e-9f);
	}

	float RedOverBlue(const glm::vec3& color)
	{
		return color.r / std::max(color.b, 1e-9f);
	}

	std::string Text(const glm::vec3& v)
	{
		return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
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

	// The sky and nothing else of the environment: no fog, no bloom, no ambient occlusion
	EnvironmentComponent SkyOnly()
	{
		EnvironmentComponent environment;
		environment.bFog = false;
		environment.bBloom = false;
		environment.bAmbientOcclusion = false;
		return environment;
	}

	// A camera, a sun and an environment in the Atmosphere mode
	struct SkyWorld
	{
		Registry World;
		Entity Camera;
		Entity Sun;
		Entity Environment;

		SkyWorld()
		{
			Camera = World.CreateEntity();
			World.Add<TransformComponent>(Camera, Placed(glm::vec3(0.0f, 2.0f, 0.0f)));
			CameraComponent lens;
			lens.FarPlane = 10000.0f;
			World.Add<CameraComponent>(Camera, lens);
			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f)));
			DirectionalLightComponent light;
			light.bCastShadows = false;
			World.Add<DirectionalLightComponent>(Sun, light);
			Environment = World.CreateEntity();
			World.Add<EnvironmentComponent>(Environment, SkyOnly());
			SetSun(30.0f, false);
		}

		// The sun `degrees` above the level, ahead of the camera's -Z or behind it
		void SetSun(float degrees, bool bAhead)
		{
			const glm::quat turn = bAhead ? glm::angleAxis(Pi, glm::vec3(0.0f, 1.0f, 0.0f)) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
			World.Get<TransformComponent>(Sun).Rotation = turn * glm::angleAxis(glm::radians(-degrees), glm::vec3(1.0f, 0.0f, 0.0f));
		}

		// The camera `height` metres up, looking along -Z `pitch` degrees up, `fov` degrees high
		void SetCamera(float height, float pitch, float fov = 60.0f)
		{
			TransformComponent& transform = World.Get<TransformComponent>(Camera);
			transform.Position = glm::vec3(0.0f, height, 0.0f);
			transform.Rotation = glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
			World.Get<CameraComponent>(Camera).FovYDegrees = fov;
		}

		EnvironmentComponent& Air() { return World.Get<EnvironmentComponent>(Environment); }
	};

	uint64_t MakeView(SceneRenderer& gpu, Registry& world, uint32_t width = ViewWidth, uint32_t height = ViewHeight)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.SetWorld(&world);
		const uint64_t view = renderer.CreateSceneView();
		renderer.SetSceneViewCameraMode(view, EViewportCameraMode::ScenePrimaryCamera);
		renderer.SetSceneViewSize(view, width, height);
		gpu.DrawUntilShadersCompiled();
		return view;
	}

	void EndView(SceneRenderer& gpu, uint64_t view)
	{
		IEditorRenderer& renderer = gpu.Renderer;
		renderer.DestroySceneView(view);
		renderer.SetWorld(nullptr);
		gpu.DrawFrames(1);
	}

	RgbaImage Draw(SceneRenderer& gpu, uint64_t view, int frames = 2)
	{
		gpu.DrawFrames(frames);
		RgbaImage image;
		Require(gpu.Renderer.ReadSceneViewPixels(view, image), "The scene view can't be read back");
		return image;
	}

	SceneViewSkyTables& TablesOf(SceneRenderer& gpu, uint64_t view)
	{
		SceneViewSkyTables* tables = gpu.Renderer.GetSceneViewSkyTables(view);
		Require(tables != nullptr && tables->IsMade() && tables->bBuilt, "The view has no sky tables");
		return *tables;
	}

	std::vector<glm::vec4> Read(SceneRenderer& gpu, uint64_t view, ESkyTable table, uint32_t level = 0)
	{
		const std::vector<glm::vec4> texels = gpu.Renderer.GetSkyAtmosphere().ReadTable(TablesOf(gpu, view), table, level);
		Require(!texels.empty(), "A sky table can't be read back");
		for (const glm::vec4& texel : texels)
		{
			Require(std::isfinite(texel.r) && std::isfinite(texel.g) && std::isfinite(texel.b) && glm::all(glm::greaterThanEqual(glm::vec3(texel), glm::vec3(0.0f))),
				"A sky table has a texel below 0 or not a number");
		}
		return texels;
	}

	// The transmittance table's texel as its height and angle (nyx::transmittanceParams())
	void TransmittanceParams(const SceneViewSkyAtmosphere& sky, uint32_t x, uint32_t y, float& height, float& cosZenith)
	{
		const float radius = sky.PlanetRadius;
		const float u = (static_cast<float>(x) + 0.5f) / VulkanSkyAtmosphere::TransmittanceWidth;
		const float v = (static_cast<float>(y) + 0.5f) / VulkanSkyAtmosphere::TransmittanceHeight;
		const float h = std::sqrt(sky.AtmosphereHeight * (2.0f * radius + sky.AtmosphereHeight));
		const float rho = h * v;
		const float r = std::sqrt(rho * rho + radius * radius);
		height = rho * rho / (r + radius);
		const float dMin = sky.AtmosphereHeight - height;
		const float dMax = rho + h;
		const float d = dMin + u * (dMax - dMin);
		cosZenith = std::clamp((h * h - rho * rho - d * d) / (2.0f * r * d), -1.0f, 1.0f);
	}

	// The tables, read back: the transmittance as the CPU has it; the sky bluer at noon than at
	// sunset; the sun's light redder low
	void TestTables(SceneRenderer& gpu)
	{
		SkyWorld scene;
		const uint64_t view = MakeView(gpu, scene.World);
		scene.SetSun(30.0f, false);
		Draw(gpu, view);
		const SceneViewSkyTables& tables = TablesOf(gpu, view);
		const SceneViewSkyAtmosphere& sky = tables.Built;

		const std::vector<glm::vec4> transmittance = Read(gpu, view, ESkyTable::Transmittance);
		constexpr uint32_t Width = VulkanSkyAtmosphere::TransmittanceWidth;
		constexpr uint32_t Height = VulkanSkyAtmosphere::TransmittanceHeight;
		const auto at = [&](uint32_t x, uint32_t y)
		{
			return glm::vec3(transmittance[static_cast<size_t>(y) * Width + x]);
		};
		// As the CPU integrates it, texel by texel, within the half floats' rounding
		float worst = 0.0f;
		for (uint32_t y = 0; y < Height; y += 7)
		{
			for (uint32_t x = 0; x < Width; x += 13)
			{
				float height;
				float cosZenith;
				TransmittanceParams(sky, x, y, height, cosZenith);
				const float r = sky.PlanetRadius + height;
				const float b = r * cosZenith;
				const float c = (height - sky.AtmosphereHeight) * (2.0f * sky.PlanetRadius + height + sky.AtmosphereHeight);
				const glm::vec3 expected = GetAtmosphereTransmittance(sky, height, cosZenith, std::max(-b + std::sqrt(std::max(b * b - c, 0.0f)), 0.0f));
				const glm::vec3 difference = glm::abs(at(x, y) - expected);
				worst = std::max(worst, std::max(difference.r, std::max(difference.g, difference.b)));
				Require(glm::all(glm::lessThan(difference, glm::vec3(2e-3f) + expected * 4e-3f)),
					"The transmittance table at " + std::to_string(x) + ", " + std::to_string(y) + " is " + Text(at(x, y)) + ", the CPU's " + Text(expected));
			}
		}
		std::cout << "Transmittance table against the CPU: at most " << worst << " apart\n";
		// At the top of the air, straight up, all of it gets through; along a row, from straight up
		// to the horizon, ever less, and redder
		Require(glm::all(glm::greaterThan(at(0, Height - 1), glm::vec3(0.999f))), "At the top of the air the light doesn't all get through: " + Text(at(0, Height - 1)));
		for (uint32_t x = 1; x < Width; ++x)
		{
			Require(glm::all(glm::lessThanEqual(at(x, 0), at(x - 1, 0) + glm::vec3(1e-3f))), "On the ground the transmittance rises along a longer ray at " + std::to_string(x));
		}
		Require(RedOverBlue(at(Width - 1, 0)) > RedOverBlue(at(0, 0)) * 10.0f, "Along the horizon the sun's light isn't far redder than overhead: " + Text(at(Width - 1, 0)));

		// The multiple scattering: some, for a sun above the horizon
		const std::vector<glm::vec4> multiple = Read(gpu, view, ESkyTable::MultipleScattering);
		const glm::vec3 noonMultiple(multiple[static_cast<size_t>(0) * VulkanSkyAtmosphere::MultipleScatteringSize + VulkanSkyAtmosphere::MultipleScatteringSize - 1]);
		Require(Luminance(noonMultiple) > 1e-4f && noonMultiple.b > noonMultiple.r, "No bluish multiple scattering on the ground under a sun overhead: " + Text(noonMultiple));

		// The sky above the horizon, on average: bluer at noon than at sunset, and brighter
		const auto skyAverage = [&](float degrees)
		{
			scene.SetSun(degrees, false);
			const RgbaImage picture = Draw(gpu, view);
			SaveIfAsked(picture, "Sun" + std::to_string(static_cast<int>(degrees)));
			const std::vector<glm::vec4> skyView = Read(gpu, view, ESkyTable::SkyView);
			glm::vec3 sum(0.0f);
			for (uint32_t y = 0; y < VulkanSkyAtmosphere::SkyViewHeight / 2; ++y)
			{
				for (uint32_t x = 0; x < VulkanSkyAtmosphere::SkyViewWidth; ++x)
				{
					sum += glm::vec3(skyView[static_cast<size_t>(y) * VulkanSkyAtmosphere::SkyViewWidth + x]);
				}
			}
			return sum / static_cast<float>(VulkanSkyAtmosphere::SkyViewWidth * VulkanSkyAtmosphere::SkyViewHeight / 2);
		};
		const glm::vec3 noon = skyAverage(70.0f);
		const glm::vec3 sunset = skyAverage(2.0f);
		std::cout << "The sky on average: noon " << Text(noon) << ", sunset " << Text(sunset) << "\n";
		Require(BlueOverRed(noon) > BlueOverRed(sunset) * 1.3f, "The sky isn't bluer at noon than at sunset: " + Text(noon) + " against " + Text(sunset));
		Require(Luminance(noon) > Luminance(sunset) * 2.0f, "The sky isn't brighter at noon than at sunset");
		EndView(gpu, view);
	}

	// From 2 m and 2 km up, looking level away from the sun through a narrow lens: up there the
	// horizon is 1.44 degrees below eye level, and the sky straight up is darker
	void TestCameraHighUp(SceneRenderer& gpu)
	{
		SkyWorld scene;
		scene.SetSun(20.0f, false);
		scene.SetCamera(2.0f, 0.0f, 20.0f);
		const uint64_t view = MakeView(gpu, scene.World);
		const uint32_t middle = ViewHeight / 2;
		const uint32_t column = ViewWidth / 2;
		// The horizon's dip, in pixels below the middle
		const float dip = std::acos(GetHorizonCosine(6360.0f, 2.0f)) - Pi * 0.5f;
		const uint32_t dipRows = static_cast<uint32_t>(std::round(std::tan(dip) / std::tan(glm::radians(10.0f)) * (ViewHeight * 0.5f)));

		// The horizon: where the column darkens most from one row to the next, from the haze above
		// to the planet's ground below
		const auto horizonRow = [&](const RgbaImage& image)
		{
			uint32_t horizon = 0;
			float steepest = 0.0f;
			for (uint32_t y = 0; y + 1 < image.Height; ++y)
			{
				const float step = Luminance(PixelAt(image, column, y)) - Luminance(PixelAt(image, column, y + 1));
				if (step > steepest)
				{
					steepest = step;
					horizon = y;
				}
			}
			return horizon;
		};

		const RgbaImage ground = Draw(gpu, view);
		SaveIfAsked(ground, "Level2m");
		const uint32_t airBuilds = TablesOf(gpu, view).AirBuilds;
		const uint32_t skyViewBuilds = TablesOf(gpu, view).SkyViewBuilds;
		scene.SetCamera(2000.0f, 0.0f, 20.0f);
		const RgbaImage high = Draw(gpu, view);
		SaveIfAsked(high, "Level2km");
		Require(TablesOf(gpu, view).SkyViewBuilds > skyViewBuilds && TablesOf(gpu, view).AirBuilds == airBuilds, "Rising 2 km makes the wrong tables again");

		const uint32_t groundHorizon = horizonRow(ground);
		const uint32_t highHorizon = horizonRow(high);
		std::cout << "The horizon's row: " << groundHorizon << " from 2 m, " << highHorizon << " from 2 km; eye level " << middle << ", the dip " << dipRows << " rows\n";
		Require(groundHorizon + 2 >= middle && groundHorizon <= middle + 2, "From 2 m the horizon isn't at eye level: row " + std::to_string(groundHorizon));
		Require(highHorizon + 3 >= middle + dipRows && highHorizon <= middle + dipRows + 3, "From 2 km the horizon isn't 1.44 degrees below eye level: row " + std::to_string(highHorizon));
		// 1.44 degrees below eye level: the planet's ground from 2 m, still the sky's haze from 2 km
		// but for the last rows above the horizon
		Require(Luminance(PixelAt(ground, column, middle)) > Luminance(PixelAt(ground, column, middle + dipRows - 4)) + 10.0f, "From 2 m, below the horizon isn't darker than eye level");
		Require(Luminance(PixelAt(high, column, middle + dipRows - 4)) > Luminance(PixelAt(high, column, middle + dipRows + 4)) + 10.0f,
			"From 2 km, the haze above the low horizon isn't brighter than the ground below it");

		// Straight up: darker from 2 km, the air above thinner; in the table and on the screen
		const auto zenith = [&](float height, const std::string& name)
		{
			scene.SetCamera(height, 90.0f);
			const RgbaImage image = Draw(gpu, view);
			SaveIfAsked(image, name);
			const std::vector<glm::vec4> skyView = Read(gpu, view, ESkyTable::SkyView);
			glm::vec3 row(0.0f);
			for (uint32_t x = 0; x < VulkanSkyAtmosphere::SkyViewWidth; ++x)
			{
				row += glm::vec3(skyView[x]);
			}
			return std::make_pair(Luminance(PixelAt(image, column, middle)), Luminance(row));
		};
		const auto [groundPixel, groundTable] = zenith(2.0f, "Up2m");
		const auto [highPixel, highTable] = zenith(2000.0f, "Up2km");
		std::cout << "The zenith: " << groundPixel << " of 255 from 2 m, " << highPixel << " from 2 km\n";
		Require(highTable < groundTable * 0.95f && highPixel + 2.0f < groundPixel, "The sky straight up isn't darker from 2 km: " + std::to_string(highPixel) + " against " + std::to_string(groundPixel));
		EndView(gpu, view);
	}

	// The world lit by the sky: a white wall facing away from the sun shows the tables' irradiance and
	// specular light as nyx.lighting adds them; the irradiance towards a low sun warmer than at noon
	void TestSkyLight(SceneRenderer& gpu)
	{
		SkyWorld scene;
		LiveMaterialFile whiteFile("Materials/SkyAtmosphereTestWhite.nyxmat");
		whiteFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 1 1 1\nMetallic = 0\nRoughness = 1\n");
		Material* white = gpu.Renderer.GetMaterial(whiteFile.Path);
		// Facing +Z, the camera 10 m in front of it; the sun behind it, ahead of the camera
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 2.0f, -10.0f), glm::vec3(40.0f, 1.0f, 40.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
			gpu.Renderer.GetPlaneMesh(), white);
		scene.SetSun(30.0f, true);
		const uint64_t view = MakeView(gpu, scene.World);
		const RgbaImage wall = Draw(gpu, view, 3);
		SaveIfAsked(wall, "LitBySky");

		// What lightSurfaceApart() makes of it, n = v = +Z, roughness 1: the +Z face's middle of the
		// irradiance cube and the specular cube's last mip, by the split-sum table
		const auto middleOfZ = [](const std::vector<glm::vec4>& texels, uint32_t size)
		{
			glm::vec3 sum(0.0f);
			for (const uint32_t y : { size / 2 - 1, size / 2 })
			{
				for (const uint32_t x : { size / 2 - 1, size / 2 })
				{
					sum += glm::vec3(texels[(static_cast<size_t>(4) * size + y) * size + x]) * 0.25f;
				}
			}
			return sum;
		};
		const glm::vec3 irradiance = middleOfZ(Read(gpu, view, ESkyTable::Irradiance), VulkanSkyAtmosphere::IrradianceSize);
		constexpr uint32_t LastLevel = VulkanSkyAtmosphere::SpecularLevels - 1;
		const glm::vec3 specular = middleOfZ(Read(gpu, view, ESkyTable::Specular, LastLevel), VulkanSkyAtmosphere::CubeSize >> LastLevel);
		const std::vector<glm::vec2> lookup = gpu.Renderer.GetEnvironment().ReadLookup();
		constexpr uint32_t Size = VulkanEnvironment::LookupSize;
		const float row = static_cast<float>(Size) - 0.5f;
		const glm::vec2 brdf = glm::mix(lookup[(Size - 2) * Size + Size - 1], lookup[(Size - 1) * Size + Size - 1], std::clamp(row - static_cast<float>(Size - 2), 0.0f, 1.0f));
		const glm::vec3 share = glm::vec3(0.04f) * brdf.x + brdf.y;
		const glm::vec3 expected = (irradiance * (1.0f - share) + specular * share) * scene.Air().SkyLightIntensity;
		const std::array<uint8_t, 3> pixel = PixelAt(wall, ViewWidth / 2, ViewHeight / 2);
		std::cout << "The wall lit by the sky: " << RenderTests::Text(pixel) << ", the tables' " << RenderTests::Text(Tonemapped(expected)) << " (irradiance " << Text(irradiance) << ")\n";
		Require(Near(pixel, Tonemapped(expected), 3), "The wall isn't lit by the sky's tables: " + RenderTests::Text(pixel) + ", expected " + RenderTests::Text(Tonemapped(expected)));
		Require(irradiance.b > irradiance.r, "The sky's light away from the sun isn't bluish: " + Text(irradiance));

		// The sky's light as the sun goes down: towards it (the cube's -Z face) and away from it (+Z),
		// dimmer at golden hour, and warmer towards the sun than away from it
		const auto irradianceAt = [&](float degrees, uint32_t face)
		{
			scene.SetSun(degrees, true);
			Draw(gpu, view);
			const std::vector<glm::vec4> cube = Read(gpu, view, ESkyTable::Irradiance);
			constexpr uint32_t Side = VulkanSkyAtmosphere::IrradianceSize;
			glm::vec3 sum(0.0f);
			for (const uint32_t y : { Side / 2 - 1, Side / 2 })
			{
				for (const uint32_t x : { Side / 2 - 1, Side / 2 })
				{
					sum += glm::vec3(cube[(static_cast<size_t>(face) * Side + y) * Side + x]) * 0.25f;
				}
			}
			return sum;
		};
		const glm::vec3 noon = irradianceAt(60.0f, 5);
		const glm::vec3 goldenTowards = irradianceAt(4.0f, 5);
		const glm::vec3 goldenAway = irradianceAt(4.0f, 4);
		const glm::vec3 goldenUp = irradianceAt(4.0f, 2);
		std::cout << "The sky's light: towards the sun at noon " << Text(noon) << "; at golden hour towards it " << Text(goldenTowards) << ", away " << Text(goldenAway) << ", from above "
				  << Text(goldenUp) << "\n";
		Require(Luminance(goldenTowards) < Luminance(noon) * 0.5f, "The sky's light doesn't dim at golden hour");
		Require(RedOverBlue(goldenTowards) > RedOverBlue(goldenAway) * 1.1f, "At golden hour the sky's light towards the sun isn't warmer than away from it");
		EndView(gpu, view);
	}

	// The fog takes the sky's colour at the horizon: a black wall 3 km off, hidden by thick fog that
	// scatters no sunlight of its own, shows the sky view's row above the horizon; towards the low
	// sun brighter than away from it
	void TestFogFromTheSky(SceneRenderer& gpu)
	{
		SkyWorld scene;
		LiveMaterialFile blackFile("Materials/SkyAtmosphereTestBlack.nyxmat");
		blackFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0 0 0\nMetallic = 0\nRoughness = 1\n");
		Material* black = gpu.Renderer.GetMaterial(blackFile.Path);
		const glm::quat facingZ = glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 2.0f, -3000.0f), glm::vec3(4000.0f, 1.0f, 400.0f), facingZ), gpu.Renderer.GetPlaneMesh(), black);
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 2.0f, 3000.0f), glm::vec3(4000.0f, 1.0f, 400.0f), glm::angleAxis(Pi, glm::vec3(0.0f, 1.0f, 0.0f)) * facingZ),
			gpu.Renderer.GetPlaneMesh(), black);
		EnvironmentComponent& air = scene.Air();
		air.bFog = true;
		air.FogDensity = 0.05f;
		air.FogHeightFalloff = 0.0f;
		air.FogSunScattering = 0.0f;
		scene.SetSun(4.0f, true);
		const uint64_t view = MakeView(gpu, scene.World);

		// The sky view's last row above the horizon (nyx::skyAtmosphereHorizon()), towards the sun
		// (its first texel) or away from it (its last)
		const auto horizon = [&](bool bTowards)
		{
			const std::vector<glm::vec4> skyView = Read(gpu, view, ESkyTable::SkyView);
			constexpr uint32_t Width = VulkanSkyAtmosphere::SkyViewWidth;
			const size_t row = static_cast<size_t>(VulkanSkyAtmosphere::SkyViewHeight / 2 - 1) * Width;
			return glm::vec3(skyView[row + (bTowards ? 0 : Width - 1)]);
		};

		const RgbaImage towards = Draw(gpu, view, 3);
		SaveIfAsked(towards, "FogTowardsSun");
		const glm::vec3 towardsHorizon = horizon(true);
		scene.SetCamera(2.0f, 0.0f);
		scene.World.Get<TransformComponent>(scene.Camera).Rotation = glm::angleAxis(Pi, glm::vec3(0.0f, 1.0f, 0.0f));
		const RgbaImage away = Draw(gpu, view, 3);
		SaveIfAsked(away, "FogAwayFromSun");
		const glm::vec3 awayHorizon = horizon(false);

		const std::array<uint8_t, 3> towardsPixel = PixelAt(towards, ViewWidth / 2, ViewHeight / 2 + 4);
		const std::array<uint8_t, 3> awayPixel = PixelAt(away, ViewWidth / 2, ViewHeight / 2 + 4);
		std::cout << "The fog towards the sun " << RenderTests::Text(towardsPixel) << " (the horizon's " << RenderTests::Text(Tonemapped(towardsHorizon)) << "), away "
				  << RenderTests::Text(awayPixel) << " (" << RenderTests::Text(Tonemapped(awayHorizon)) << ")\n";
		Require(Near(towardsPixel, Tonemapped(towardsHorizon), 3), "The fog towards the sun isn't the sky's horizon");
		Require(Near(awayPixel, Tonemapped(awayHorizon), 3), "The fog away from the sun isn't the sky's horizon");
		// Both lit by the low sun's reddened light (away from it, the dusk's pink band); towards it
		// far brighter, by the haze's forward scattering
		Require(Luminance(towardsPixel) > Luminance(awayPixel) * 2.0f, "The fog doesn't glow towards the sun");
		EndView(gpu, view);
	}

	// Without the component, and with the plain cube map, the same picture to the bit, and no tables;
	// the tables made again only as what they depend on changes
	void TestUnchangedAndMadeWhenNeeded(SceneRenderer& gpu)
	{
		SkyWorld scene;
		Material* plastic = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		AddMesh(scene.World, Placed(glm::vec3(0.0f), glm::vec3(100.0f, 1.0f, 100.0f)), gpu.Renderer.GetPlaneMesh(), plastic);
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 1.0f, -6.0f), glm::vec3(2.0f)), gpu.Renderer.GetCubeMesh(), plastic);
		scene.SetCamera(2.0f, -5.0f);
		scene.SetSun(25.0f, true);
		const uint64_t view = MakeView(gpu, scene.World);

		// Made once, then left alone while nothing changes
		const RgbaImage on = Draw(gpu, view, 3);
		SaveIfAsked(on, "Scene");
		const SceneViewSkyTables& tables = TablesOf(gpu, view);
		const uint32_t airBuilds = tables.AirBuilds;
		const uint32_t skyViewBuilds = tables.SkyViewBuilds;
		gpu.DrawFrames(5);
		Require(tables.AirBuilds == airBuilds && tables.SkyViewBuilds == skyViewBuilds, "The sky's tables are made again with nothing changed");
		Require(gpu.Renderer.GetSceneViewGpuTimes(view)->Sky == 0.0f, "The sky takes GPU time with nothing changed");
		// The sun turned: the sky view, not the air's tables; the air changed: all of them
		scene.SetSun(26.0f, true);
		gpu.DrawFrames(1);
		Require(tables.AirBuilds == airBuilds && tables.SkyViewBuilds == skyViewBuilds + 1, "The sun turning makes the wrong tables again");
		scene.Air().MieScatteringScale *= 2.0f;
		gpu.DrawFrames(1);
		Require(tables.AirBuilds == airBuilds + 1 && tables.SkyViewBuilds == skyViewBuilds + 2, "Hazier air doesn't make every table again");
		// The camera moving sideways, or a metre up, changes nothing
		scene.World.Get<TransformComponent>(scene.Camera).Position += glm::vec3(50.0f, 0.5f, 0.0f);
		gpu.DrawFrames(2);
		Require(tables.SkyViewBuilds == skyViewBuilds + 2, "The camera moving sideways makes the sky view again");
		// A studio sky in the view: no tables
		gpu.Renderer.SetSceneViewSky(view, ESceneViewSky::Studio, 0.0f);
		gpu.DrawFrames(1);
		Require(!gpu.Renderer.GetSceneViewSkyTables(view)->IsMade(), "A view with the studio sky keeps the air's tables");
		gpu.Renderer.SetSceneViewSky(view, ESceneViewSky::Scene, 0.0f);

		// The plain cube map, and no component: the same picture, to the bit, no tables
		scene.Air().SkyMode = ESkyMode::CubeMap;
		const RgbaImage off = Draw(gpu, view);
		Require(!gpu.Renderer.GetSceneViewSkyTables(view)->IsMade(), "The plain cube map keeps the air's tables");
		scene.World.Remove<EnvironmentComponent>(scene.Environment);
		const RgbaImage none = Draw(gpu, view);
		SaveIfAsked(none, "NoEnvironment");
		Require(off.Pixels == none.Pixels, "The plain cube map doesn't draw the picture as without the component");
		Require(on.Pixels != none.Pixels, "The air's sky changes nothing");
		EndView(gpu, view);
	}

	// A sun 4 degrees below the horizon: a time of day fades its light out (it lights nothing), but
	// the air above still scatters it, so the dusk sky is the same with the time of day as without
	void TestDuskWithTimeOfDay(SceneRenderer& gpu)
	{
		SkyWorld scene;
		scene.SetSun(-4.0f, true);
		const uint64_t view = MakeView(gpu, scene.World);
		Draw(gpu, view);
		const std::vector<glm::vec4> without = Read(gpu, view, ESkyTable::SkyView);
		scene.World.Add<TimeOfDayComponent>(scene.World.CreateEntity(), TimeOfDayComponent{});
		const RgbaImage dusk = Draw(gpu, view);
		SaveIfAsked(dusk, "DuskWithTimeOfDay");
		const std::vector<glm::vec4> with = Read(gpu, view, ESkyTable::SkyView);
		Require(with == without, "A time of day fading the set sun's light changes the air's dusk");
		glm::vec3 sum(0.0f);
		for (uint32_t x = 0; x < VulkanSkyAtmosphere::SkyViewWidth; ++x)
		{
			sum += glm::vec3(with[static_cast<size_t>(VulkanSkyAtmosphere::SkyViewHeight / 2 - 1) * VulkanSkyAtmosphere::SkyViewWidth + x]);
		}
		Require(Luminance(sum) / VulkanSkyAtmosphere::SkyViewWidth > 1e-3f, "The dusk sky 4 degrees after sunset is black");

		// Midnight: the night's faint light, a small part of the day's (the sky's mean above the
		// horizon is about 0.12 at noon)
		scene.SetSun(-60.0f, true);
		Draw(gpu, view);
		const std::vector<glm::vec4> night = Read(gpu, view, ESkyTable::SkyView);
		glm::vec3 nightSum(0.0f);
		for (size_t texel = 0; texel < night.size() / 2; ++texel)
		{
			nightSum += glm::vec3(night[texel]);
		}
		const float nightLuminance = Luminance(nightSum) / static_cast<float>(night.size() / 2);
		std::cout << "The night sky's mean above the horizon: " << nightLuminance << "\n";
		Require(nightLuminance > 1e-4f && nightLuminance < 0.02f, "The night sky isn't a faint part of the day's: " + std::to_string(nightLuminance));
		EndView(gpu, view);
	}

	// What the tonemapped picture shows as light again: sRGB decoded, its luminance
	float LinearLuminance(const std::array<uint8_t, 3>& rgb)
	{
		const auto decode = [](uint8_t value)
		{
			const float c = static_cast<float>(value) / 255.0f;
			return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
		};
		return Luminance(glm::vec3(decode(rgb[0]), decode(rgb[1]), decode(rgb[2])));
	}

	// A sun 2 degrees up: its forward glow is a halo round it, not a band up the sky above it. 60
	// degrees up on the sun's side is no brighter than 60 degrees up at its side but for the air's
	// own phases, with the fog and without: the molecules' phase is 1.28 times stronger 58 degrees
	// from the sun than 88 (1 + cos²), and the haze, which scatters forwards far more, is thin that
	// high, so 1.5 times at most. The fog over the sky above the horizon takes the sky's own colour
	// there, so it changes nothing; the halo 5 degrees above the sun is far brighter than both.
	// (The fog's colour came from the horizon by the angle round from the sun alone, so it carried
	// the horizon's glow straight up the sky over a low sun, a wedge narrowing to the zenith.)
	void TestNoBandAboveLowSun(SceneRenderer& gpu)
	{
		SkyWorld scene;
		scene.SetSun(2.0f, true);
		const uint64_t view = MakeView(gpu, scene.World);
		const auto at = [&](float yaw, float pitch, const std::string& name)
		{
			TransformComponent& camera = scene.World.Get<TransformComponent>(scene.Camera);
			camera.Rotation = glm::angleAxis(glm::radians(yaw), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(glm::radians(pitch), glm::vec3(1.0f, 0.0f, 0.0f));
			const RgbaImage image = Draw(gpu, view);
			SaveIfAsked(image, name);
			return PixelAt(image, ViewWidth / 2, ViewHeight / 2);
		};
		std::array<uint8_t, 3> unfogged{};
		for (const bool bFog : { false, true })
		{
			scene.Air().bFog = bFog;
			const std::string name = bFog ? "LowSunFog" : "LowSun";
			const std::array<uint8_t, 3> sunSide = at(0.0f, 60.0f, name + "Up");
			const std::array<uint8_t, 3> side = at(90.0f, 60.0f, name + "UpAside");
			const std::array<uint8_t, 3> halo = at(0.0f, 7.0f, name + "Halo");
			std::cout << "A sun 2 degrees up" << (bFog ? ", fog" : "") << ": 60 degrees up on its side " << RenderTests::Text(sunSide) << ", at its side "
					  << RenderTests::Text(side) << ", 5 degrees above it " << RenderTests::Text(halo) << "\n";
			Require(LinearLuminance(sunSide) <= LinearLuminance(side) * 1.5f, "The sky high above a low sun is brighter than at its side: a band up the sky");
			Require(LinearLuminance(halo) > LinearLuminance(sunSide) * 3.0f, "No halo round a low sun");
			if (!bFog)
			{
				unfogged = sunSide;
			}
			else
			{
				Require(Near(sunSide, unfogged, 2), "The fog changes the sky high above the horizon: " + RenderTests::Text(sunSide) + " against " + RenderTests::Text(unfogged));
			}
		}
		EndView(gpu, view);
	}

	// The night's light (Night Sky Brightness): at 0 none, the air's own sky, black at midnight; above
	// 0 the midnight sky and a wall lit by the sky alone brighter; the noon sky the same either way
	void TestNightSky(SceneRenderer& gpu)
	{
		SkyWorld scene;
		LiveMaterialFile whiteFile("Materials/SkyAtmosphereTestNight.nyxmat");
		whiteFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 1 1 1\nMetallic = 0\nRoughness = 1\n");
		AddMesh(scene.World, Placed(glm::vec3(0.0f, 2.0f, -10.0f), glm::vec3(40.0f, 1.0f, 40.0f), glm::angleAxis(glm::radians(90.0f), glm::vec3(1.0f, 0.0f, 0.0f))),
			gpu.Renderer.GetPlaneMesh(), gpu.Renderer.GetMaterial(whiteFile.Path));
		const uint64_t view = MakeView(gpu, scene.World);
		struct Seen
		{
			std::vector<glm::vec4> SkyView;
			std::array<uint8_t, 3> Wall{};
			std::array<uint8_t, 3> Sky{};
		};
		const auto seen = [&](float sunDegrees, float brightness, const std::string& name)
		{
			scene.Air().NightSkyBrightness = brightness;
			scene.SetSun(sunDegrees, true);
			Seen result;
			scene.SetCamera(2.0f, 0.0f);
			const RgbaImage wall = Draw(gpu, view);
			SaveIfAsked(wall, name + "Wall");
			result.Wall = PixelAt(wall, ViewWidth / 2, ViewHeight / 2);
			scene.SetCamera(2.0f, 60.0f);
			const RgbaImage sky = Draw(gpu, view);
			SaveIfAsked(sky, name + "Sky");
			result.Sky = PixelAt(sky, ViewWidth / 2, ViewHeight / 2);
			result.SkyView = Read(gpu, view, ESkyTable::SkyView);
			return result;
		};

		const Seen black = seen(-60.0f, 0.0f, "Midnight0");
		const Seen lit = seen(-60.0f, EnvironmentComponent{}.NightSkyBrightness, "MidnightDefault");
		float brightest = 0.0f;
		for (const glm::vec4& texel : black.SkyView)
		{
			brightest = std::max(brightest, std::max(texel.r, std::max(texel.g, texel.b)));
		}
		std::cout << "Midnight at a night brightness of 0: the sky " << RenderTests::Text(black.Sky) << ", the wall " << RenderTests::Text(black.Wall)
				  << " (the sky view's brightest texel " << brightest << "); at the default: the sky " << RenderTests::Text(lit.Sky) << ", the wall "
				  << RenderTests::Text(lit.Wall) << "\n";
		Require(brightest == 0.0f && black.Sky == std::array<uint8_t, 3>{ 0, 0, 0 }, "At a night brightness of 0 the midnight sky isn't the air's own, black");
		Require(LinearLuminance(lit.Sky) > LinearLuminance(black.Sky) + 1e-3f, "The night's light doesn't brighten the midnight sky");
		Require(LinearLuminance(lit.Wall) > LinearLuminance(black.Wall) + 1e-3f, "The night's light doesn't light a wall by the sky");

		const Seen noonPhysical = seen(60.0f, 0.0f, "Noon0");
		const Seen noonLit = seen(60.0f, 0.2f, "NoonBright");
		Require(noonPhysical.SkyView == noonLit.SkyView && noonPhysical.Sky == noonLit.Sky && noonPhysical.Wall == noonLit.Wall,
			"The night's light changes the noon sky");
		EndView(gpu, view);
	}

	// The sun's disk: far brighter than white, so it blooms, and red at its edge low down
	void TestSunDisk(SceneRenderer& gpu)
	{
		SkyWorld scene;
		scene.Air().bBloom = true;
		scene.Air().SunDiskSize = 2.0f;
		scene.SetSun(10.0f, true);
		scene.SetCamera(2.0f, 10.0f);
		const uint64_t view = MakeView(gpu, scene.World);
		const RgbaImage bloomed = Draw(gpu, view);
		SaveIfAsked(bloomed, "SunDisk");
		const std::array<uint8_t, 3> disk = PixelAt(bloomed, ViewWidth / 2, ViewHeight / 2);
		Require(disk[0] > 250 && disk[1] > 240, "The sun's disk isn't white hot: " + RenderTests::Text(disk));
		// Beside it, a glow by bloom: brighter with bloom than without
		const uint32_t beside = ViewWidth / 2 + 8;
		scene.Air().bBloom = false;
		const RgbaImage plain = Draw(gpu, view);
		std::cout << "Beside the sun: " << RenderTests::Text(PixelAt(bloomed, beside, ViewHeight / 2)) << " with bloom, " << RenderTests::Text(PixelAt(plain, beside, ViewHeight / 2)) << " without\n";
		Require(Luminance(PixelAt(bloomed, beside, ViewHeight / 2)) > Luminance(PixelAt(plain, beside, ViewHeight / 2)) + 10.0f, "The sun's disk doesn't bloom");
		EndView(gpu, view);
	}

	// A crate yard: a grey ground and rows of grey crates under the primary light, a camera 3 m up
	// looking along them, and an environment at its defaults in a sky mode: the lit scene the
	// Atmosphere and Cube Map modes are looked at side by side in
	struct CrateYard
	{
		Registry World;
		LiveMaterialFile GreyFile{ "Materials/SkyAtmosphereTestGrey.nyxmat" };
		Entity Sun;
		Entity Air;

		explicit CrateYard(VulkanRenderer& renderer)
		{
			GreyFile.Write("Shader = Shaders/Standard.slang\nBaseColor = 0.6 0.6 0.6\nMetallic = 0\nRoughness = 0.8\n");
			Material* grey = renderer.GetMaterial(GreyFile.Path);
			AddMesh(World, Placed(glm::vec3(0.0f), glm::vec3(300.0f, 1.0f, 300.0f)), renderer.GetPlaneMesh(), grey);
			for (int row = 0; row < 6; ++row)
			{
				for (int column = -2; column <= 2; ++column)
				{
					AddMesh(World, Placed(glm::vec3(column * 6.0f + (row % 2) * 3.0f, 1.0f, -8.0f - row * 14.0f), glm::vec3(2.0f)), renderer.GetCubeMesh(), grey);
				}
			}
			const Entity camera = World.CreateEntity();
			World.Add<TransformComponent>(camera, Placed(glm::vec3(0.0f, 3.0f, 6.0f), glm::vec3(1.0f), glm::quat(glm::radians(glm::vec3(-4.0f, 0.0f, 0.0f)))));
			CameraComponent lens;
			lens.FarPlane = 10000.0f;
			World.Add<CameraComponent>(camera, lens);
			Sun = World.CreateEntity();
			World.Add<TransformComponent>(Sun, Placed(glm::vec3(0.0f)));
			World.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});
			Air = World.CreateEntity();
			World.Add<EnvironmentComponent>(Air, EnvironmentComponent{});
		}

		// The sun `degrees` up, behind the camera and to its right
		void SetSun(float degrees)
		{
			World.Get<TransformComponent>(Sun).Rotation = glm::quat(glm::radians(glm::vec3(-degrees, 155.0f, 0.0f)));
		}
	};

	// The mean light the picture shows below its horizon (the yard), sRGB decoded
	float YardLight(const RgbaImage& image)
	{
		double sum = 0.0;
		for (uint32_t y = image.Height / 2; y < image.Height; ++y)
		{
			for (uint32_t x = 0; x < image.Width; ++x)
			{
				sum += LinearLuminance(PixelAt(image, x, y));
			}
		}
		return static_cast<float>(sum / (static_cast<double>(image.Width) * (image.Height - image.Height / 2)));
	}

	// The Environment's look settings at their neutral values: today's picture
	void SetNeutralLook(EnvironmentComponent& environment)
	{
		environment.SkyLightIntensity = 1.0f;
		environment.ExposureCompensation = 0.0f;
		environment.HazeBrightness = 1.0f;
		environment.HazeTint = glm::vec3(1.0f);
	}

	// The crate yard at noon and at sunset in the Atmosphere and the Cube Map modes, at the
	// environment's defaults, for looking at side by side; the yard's mean light printed
	void TestLookAgainstCubeMap(SceneRenderer& gpu)
	{
		CrateYard yard(gpu.Renderer);
		const uint64_t view = MakeView(gpu, yard.World);
		for (const float degrees : { 50.0f, 5.0f })
		{
			for (const ESkyMode mode : { ESkyMode::Atmosphere, ESkyMode::CubeMap })
			{
				yard.SetSun(degrees);
				yard.World.Get<EnvironmentComponent>(yard.Air).SkyMode = mode;
				const RgbaImage image = Draw(gpu, view, 3);
				const std::string name = std::string("Yard") + (mode == ESkyMode::Atmosphere ? "Atmosphere" : "CubeMap") + (degrees > 20.0f ? "Noon" : "Sunset");
				SaveIfAsked(image, name);
				std::cout << name << ": the yard's mean light " << YardLight(image) << "\n";
			}
		}
		EndView(gpu, view);
	}

	// The look settings at their neutral values draw today's picture: the crate yard at sunset in the
	// Atmosphere mode with fog, bloom and ambient occlusion, against References/SkyAtmosphereNeutral.png,
	// made before the settings were there
	void TestNeutralLookIsToday(SceneRenderer& gpu)
	{
		CrateYard yard(gpu.Renderer);
		yard.SetSun(6.0f);
		SetNeutralLook(yard.World.Get<EnvironmentComponent>(yard.Air));
		const uint64_t view = MakeView(gpu, yard.World);
		const RgbaImage image = Draw(gpu, view, 3);
		SaveIfAsked(image, "Neutral");
		RequireLooksLike(image, std::filesystem::path(NYX_TEST_REFERENCES_DIR) / "SkyAtmosphereNeutral.png");
		EndView(gpu, view);
	}

	// The look settings, each on its own, in the crate yard at noon in the Atmosphere mode: the sky's
	// light on surfaces scales the shade and not the sky; the exposure compensation brightens all of
	// it by its stops; the haze's brightness and tint change the fogged distance, not the near
	// ground's sunlit part; in the cube map modes the sky's light and the haze are the cube map's,
	// unchanged
	void TestLookSettings(SceneRenderer& gpu)
	{
		CrateYard yard(gpu.Renderer);
		yard.SetSun(50.0f);
		EnvironmentComponent& air = yard.World.Get<EnvironmentComponent>(yard.Air);
		SetNeutralLook(air);
		air.bBloom = false;
		const uint64_t view = MakeView(gpu, yard.World);
		// A crate's face in its own shade, the sky above the horizon, the near sunlit ground, the far
		// fogged ground
		const glm::uvec2 shade(240, 160);
		const glm::uvec2 sky(240, 20);
		const glm::uvec2 near(240, 250);
		const glm::uvec2 far(100, 128);
		const auto at = [&](const RgbaImage& image, const glm::uvec2& pixel)
		{
			return LinearLuminance(PixelAt(image, pixel.x, pixel.y));
		};
		const RgbaImage neutral = Draw(gpu, view, 3);

		air.SkyLightIntensity = 2.0f;
		const RgbaImage brighterLight = Draw(gpu, view, 3);
		std::cout << "Sky Light Intensity 2: the shade " << at(neutral, shade) << " -> " << at(brighterLight, shade) << ", the sky " << at(neutral, sky) << " -> "
				  << at(brighterLight, sky) << "\n";
		Require(at(brighterLight, shade) > at(neutral, shade) * 1.3f, "The sky's light on surfaces doesn't brighten the shade");
		Require(PixelAt(brighterLight, sky.x, sky.y) == PixelAt(neutral, sky.x, sky.y), "The sky's light on surfaces changes how the sky looks");
		air.SkyLightIntensity = 1.0f;

		air.ExposureCompensation = 1.0f;
		const RgbaImage brighter = Draw(gpu, view, 3);
		std::cout << "Exposure +1: the shade " << at(neutral, shade) << " -> " << at(brighter, shade) << ", the sky " << at(neutral, sky) << " -> " << at(brighter, sky) << "\n";
		Require(at(brighter, shade) > at(neutral, shade) * 1.3f && at(brighter, sky) > at(neutral, sky) * 1.3f, "One stop of exposure doesn't brighten the picture");
		air.ExposureCompensation = 0.0f;

		air.HazeBrightness = 2.0f;
		air.HazeTint = glm::vec3(1.0f, 0.6f, 0.3f);
		const RgbaImage hazy = Draw(gpu, view, 3);
		const std::array<uint8_t, 3> farNeutral = PixelAt(neutral, far.x, far.y);
		const std::array<uint8_t, 3> farHazy = PixelAt(hazy, far.x, far.y);
		std::cout << "Haze x2, warm: the far ground " << RenderTests::Text(farNeutral) << " -> " << RenderTests::Text(farHazy) << ", the near " << RenderTests::Text(PixelAt(neutral, near.x, near.y))
				  << " -> " << RenderTests::Text(PixelAt(hazy, near.x, near.y)) << "\n";
		Require(float(farHazy[0]) / std::max(float(farHazy[2]), 1.0f) > float(farNeutral[0]) / std::max(float(farNeutral[2]), 1.0f) * 1.1f, "The haze's tint doesn't tint the fogged distance");
		Require(std::abs(at(hazy, near) - at(neutral, near)) < std::abs(at(hazy, far) - at(neutral, far)), "The haze changes the near ground more than the far");

		// In the cube map modes, the sky's light and the haze are the cube map's: unchanged
		air.SkyMode = ESkyMode::TintedCubeMap;
		SetNeutralLook(air);
		const RgbaImage tinted = Draw(gpu, view, 3);
		air.SkyLightIntensity = 3.0f;
		air.HazeBrightness = 3.0f;
		air.HazeTint = glm::vec3(1.0f, 0.2f, 0.2f);
		const RgbaImage tintedSet = Draw(gpu, view, 3);
		Require(tinted.Pixels == tintedSet.Pixels, "The Atmosphere mode's sky light and haze change the tinted cube map");
		EndView(gpu, view);
	}

	// The sky's passes on the GPU at 1280 x 720: the tables made each frame (the sun turned a little
	// each frame), and the scene pass with the air's sky against the cube map's
	void TestGpuTimes(SceneRenderer& gpu)
	{
		SkyWorld scene;
		Material* plastic = gpu.Renderer.GetMaterial("Materials/Untextured.nyxmat");
		AddMesh(scene.World, Placed(glm::vec3(0.0f), glm::vec3(100.0f, 1.0f, 100.0f)), gpu.Renderer.GetPlaneMesh(), plastic);
		scene.SetCamera(2.0f, 10.0f);
		const uint64_t view = MakeView(gpu, scene.World, 1280, 720);
		const auto median = [&](const auto& sample)
		{
			std::vector<float> times;
			for (int frame = 0; frame < 60; ++frame)
			{
				times.push_back(sample(frame));
			}
			std::sort(times.begin(), times.end());
			return times[times.size() / 2];
		};
		const float tables = median([&](int frame)
			{
				scene.SetSun(20.0f + 0.1f * static_cast<float>(frame), false);
				gpu.DrawFrames(2);
				return gpu.Renderer.GetSceneViewGpuTimes(view)->Sky;
			});
		Require(tables > 0.0f, "The sky's tables take no GPU time");
		const float sceneWithAir = median([&](int)
			{
				gpu.DrawFrames(1);
				return gpu.Renderer.GetSceneViewGpuTimes(view)->Scene;
			});
		scene.Air().SkyMode = ESkyMode::TintedCubeMap;
		const float sceneWithCube = median([&](int)
			{
				gpu.DrawFrames(1);
				return gpu.Renderer.GetSceneViewGpuTimes(view)->Scene;
			});
		std::cout << "GPU at 1280 x 720: the sky's tables " << tables << " ms when made; the scene pass " << sceneWithAir << " ms with the air's sky, " << sceneWithCube
				  << " ms with the tinted cube map\n";
		EndView(gpu, view);
	}
}

int main(int argc, char** argv)
{
	Nyx::Logger::Get().Init();
	try
	{
		RegisterComponentTypes();
		SceneRenderer gpu("NyxSkyAtmosphereGpuTests", MsaaSamplesFromArguments(argc, argv));
		TestTables(gpu);
		TestCameraHighUp(gpu);
		TestSkyLight(gpu);
		TestFogFromTheSky(gpu);
		TestUnchangedAndMadeWhenNeeded(gpu);
		TestSunDisk(gpu);
		TestDuskWithTimeOfDay(gpu);
		TestNoBandAboveLowSun(gpu);
		TestNightSky(gpu);
		TestLookAgainstCubeMap(gpu);
		TestNeutralLookIsToday(gpu);
		TestLookSettings(gpu);
		TestGpuTimes(gpu);
	}
	catch (const std::exception& error)
	{
		std::cerr << "NyxSkyAtmosphereGpuTests: " << error.what() << "\n";
		return 1;
	}
	std::cout << "NyxSkyAtmosphereGpuTests: all passed\n";
	return 0;
}
