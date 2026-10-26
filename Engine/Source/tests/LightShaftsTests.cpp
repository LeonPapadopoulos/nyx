// Light shafts without a GPU (LightShafts.h): off by default and with no intensity; a sun ahead in
// the middle of the picture at its place, the shafts its colour times the intensity and the tint;
// the same sun from a camera kilometres up; a sun behind the camera, level with it, or far off the
// picture gives none, one just past its edge still some; the fade as the sun leaves the picture and
// as it sets, by the light the air leaves it; the decay, so that the light from a Length's part of
// the way falls to a tenth; values out of range in range; the shafts' size; and the component
// saved in a scene and read back.
#include "Atmosphere.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "LightShafts.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	std::string Text(const glm::vec3& v)
	{
		return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
	}

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return Near(a.x, b.x, tolerance) && Near(a.y, b.y, tolerance) && Near(a.z, b.z, tolerance);
	}

	EnvironmentComponent ShaftsOn()
	{
		EnvironmentComponent environment;
		environment.bLightShafts = true;
		return environment;
	}

	// A camera at `position` looking along -Z, 60 degrees high, 16:9
	glm::mat4 ViewProjection(const glm::vec3& position = glm::vec3(0.0f))
	{
		const TransformComponent transform{ .Position = position };
		return MakePerspectiveProjection(glm::radians(60.0f), 16.0f / 9.0f, 0.1f, 1000.0f) * GetCameraView(transform);
	}

	glm::vec3 Towards(float yawDegrees, float pitchDegrees)
	{
		const float yaw = glm::radians(yawDegrees);
		const float pitch = glm::radians(pitchDegrees);
		return glm::normalize(glm::vec3(std::sin(yaw) * std::cos(pitch), std::sin(pitch), -std::cos(yaw) * std::cos(pitch)));
	}

	const glm::vec3 Warm{ 1.0f, 0.8f, 0.5f };

	void TestOffByDefault()
	{
		const EnvironmentComponent environment;
		Require(!environment.bLightShafts, "Light shafts are on by default");
		const SceneViewLightShaftsSettings off = MakeSceneViewLightShaftsSettings(environment, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm);
		Require(!off.bLightShafts && !off.IsDrawn(), "Shafts that are off are drawn");
		EnvironmentComponent dark = ShaftsOn();
		dark.LightShaftsIntensity = 0.0f;
		Require(!MakeSceneViewLightShaftsSettings(dark, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm).IsDrawn(), "Shafts of no intensity are drawn");
	}

	void TestSunAhead()
	{
		EnvironmentComponent environment = ShaftsOn();
		environment.LightShaftsIntensity = 2.0f;
		environment.LightShaftsTint = glm::vec3(1.0f, 0.5f, 1.0f);
		const SceneViewLightShaftsSettings shafts = MakeSceneViewLightShaftsSettings(environment, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm);
		Require(shafts.IsDrawn(), "A sun in the middle of the picture gives no shafts");
		Require(Near(shafts.SunUv.x, 0.5f) && Near(shafts.SunUv.y, 0.5f), "The sun ahead isn't in the middle of the picture");
		const glm::vec3 expected = GetChromaticity(Warm) * glm::vec3(1.0f, 0.5f, 1.0f) * 2.0f;
		Require(Near(shafts.Color, expected), "The shafts aren't the sun's colour, tinted, times the intensity: " + Text(shafts.Color));
		Require(Near(shafts.Density, 0.9f) && Near(shafts.DepthThreshold, 1000.0f), "The density and the depth threshold aren't the environment's");

		// Above the middle: up the picture, y down from its top. Right of it: further across.
		const SceneViewLightShaftsSettings high = MakeSceneViewLightShaftsSettings(environment, ViewProjection(), Towards(10.0f, 20.0f), Warm, Warm);
		Require(high.SunUv.y < 0.5f && high.SunUv.x > 0.5f && high.IsDrawn(), "A sun up and to the right isn't up and to the right on the picture");

		// From a camera kilometres up, the sun is where it was on the picture: it is as far away
		const SceneViewLightShaftsSettings aloft = MakeSceneViewLightShaftsSettings(environment, ViewProjection(glm::vec3(300.0f, 2000.0f, -50.0f)), Towards(10.0f, 20.0f), Warm, Warm);
		Require(Near(aloft.SunUv.x, high.SunUv.x) && Near(aloft.SunUv.y, high.SunUv.y) && Near(aloft.Color, high.Color), "The sun moves on the picture as the camera rises");
	}

	void TestSunOffThePicture()
	{
		const EnvironmentComponent environment = ShaftsOn();
		const auto drawn = [&](float yaw, float pitch)
		{
			return MakeSceneViewLightShaftsSettings(environment, ViewProjection(), Towards(yaw, pitch), Warm, Warm).IsDrawn();
		};
		Require(!drawn(180.0f, 10.0f), "A sun behind the camera gives shafts");
		Require(!drawn(90.0f, 0.0f) && !drawn(-90.0f, 0.0f), "A sun level with the camera, to its side, gives shafts");
		Require(!drawn(0.0f, 89.0f), "A sun straight above the camera gives shafts");
		// The picture is about 91 degrees across: just past its edge, some; far past it, none
		Require(drawn(50.0f, 0.0f), "A sun just past the picture's edge gives none");
		Require(!drawn(70.0f, 0.0f), "A sun far past the picture's edge gives shafts");

		Require(GetLightShaftsScreenFade(glm::vec2(0.0f)) == 1.0f && GetLightShaftsScreenFade(glm::vec2(1.0f, -0.3f)) == 1.0f, "On the picture, the shafts fade");
		Require(GetLightShaftsScreenFade(glm::vec2(0.0f, -LightShaftsScreenFadeEnd)) == 0.0f, "Half a picture past its edge, the shafts don't fade out");
		float last = 1.0f;
		for (float edge = 1.0f; edge <= LightShaftsScreenFadeEnd + 0.01f; edge += 0.05f)
		{
			const float fade = GetLightShaftsScreenFade(glm::vec2(edge, 0.2f));
			Require(fade <= last + 1e-6f, "The shafts grow as the sun leaves the picture");
			last = fade;
		}
		Require(Near(GetLightShaftsScreenFade(glm::vec2(0.5f * (1.0f + LightShaftsScreenFadeEnd), 0.0f)), 0.5f), "Halfway out, the shafts aren't half");
	}

	void TestSunSets()
	{
		// The light the air leaves the sun, as the tinted sky's air reddens it, at each height
		const auto fade = [](float elevationDegrees)
		{
			const glm::vec3 given(1.0f, 0.98f, 0.95f);
			const glm::vec3 reaching = given * GetSunTransmittance(std::sin(glm::radians(elevationDegrees)), 1.0f);
			return GetLightShaftsSunFade(reaching, given);
		};
		Require(fade(60.0f) == 1.0f && fade(10.0f) == 1.0f && fade(3.0f) == 1.0f, "A sun well above the horizon fades its shafts: " + std::to_string(fade(3.0f)));
		Require(fade(-4.0f) == 0.0f && fade(-30.0f) == 0.0f, "A sun below the horizon gives shafts: " + std::to_string(fade(-4.0f)));
		float last = 1.0f;
		for (float elevation = 3.0f; elevation >= -4.0f; elevation -= 0.25f)
		{
			Require(fade(elevation) <= last + 1e-6f, "The shafts grow as the sun sets");
			last = fade(elevation);
		}
		Require(GetLightShaftsSunFade(glm::vec3(0.0f), glm::vec3(0.0f)) == 0.0f && GetLightShaftsSunFade(glm::vec3(0.0f), glm::vec3(2.0f)) == 0.0f, "A light that gives none gives shafts");

		// A set sun, through the shafts' settings: none, even in the middle of the picture
		const SceneViewLightShaftsSettings night = MakeSceneViewLightShaftsSettings(ShaftsOn(), ViewProjection(), Towards(0.0f, 0.0f), glm::vec3(0.0f), Warm);
		Require(night.bLightShafts && !night.IsDrawn(), "A sun whose light has gone gives shafts");
	}

	void TestDecay()
	{
		// The light from Length's part of the way to the sun falls to a tenth, whatever the density
		for (const float density : { 0.3f, 0.9f, 1.0f })
		{
			for (const float length : { 0.25f, 1.0f, 3.0f })
			{
				const float decay = GetLightShaftsDecay(length, density);
				const float samples = static_cast<float>(LightShaftsSamples) * length / density;
				Require(decay > 0.0f && decay < 1.0f && Near(std::pow(decay, samples), LightShaftsTenthFalloff, 1e-3f),
					"The light from " + std::to_string(length) + " of the way doesn't fall to a tenth: " + std::to_string(std::pow(decay, samples)));
			}
		}
		Require(GetLightShaftsDecay(2.0f, 1.0f) > GetLightShaftsDecay(1.0f, 1.0f), "Longer shafts don't keep more of their light");
		const EnvironmentComponent environment = ShaftsOn();
		const SceneViewLightShaftsSettings shafts = MakeSceneViewLightShaftsSettings(environment, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm);
		Require(Near(shafts.Decay, GetLightShaftsDecay(environment.LightShaftsLength, environment.LightShaftsDensity)), "The view's decay isn't the environment's");
	}

	void TestInRange()
	{
		EnvironmentComponent wild = ShaftsOn();
		wild.LightShaftsIntensity = 1000.0f;
		wild.LightShaftsLength = -3.0f;
		wild.LightShaftsDensity = 7.0f;
		wild.LightShaftsDepthThreshold = -10.0f;
		wild.LightShaftsTint = glm::vec3(-1.0f, 2.0f, 1.0f);
		const SceneViewLightShaftsSettings shafts = MakeSceneViewLightShaftsSettings(wild, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm);
		Require(shafts.Intensity == 16.0f && shafts.Density == 1.0f && shafts.DepthThreshold == 0.0f, "Values out of range stay out of range");
		Require(shafts.Decay > 0.0f && shafts.Decay < 1.0f && std::isfinite(shafts.Decay), "A length out of range gives a decay out of range");
		Require(shafts.Color.r == 0.0f && shafts.Color.g > 0.0f && std::isfinite(shafts.Color.g), "A tint below 0 isn't held at 0");
		wild.LightShaftsDensity = 0.0f;
		Require(MakeSceneViewLightShaftsSettings(wild, ViewProjection(), Towards(0.0f, 0.0f), Warm, Warm).Density == 0.05f, "No density isn't held at the least");

		Require(GetLightShaftsSize(1280, 720) == glm::uvec2(640, 360) && GetLightShaftsSize(1, 3) == glm::uvec2(1, 1), "The shafts aren't half the view's size");
	}

	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		EnvironmentComponent environment = ShaftsOn();
		environment.LightShaftsIntensity = 1.5f;
		environment.LightShaftsLength = 0.75f;
		environment.LightShaftsDensity = 0.6f;
		environment.LightShaftsDepthThreshold = 250.0f;
		environment.LightShaftsTint = glm::vec3(1.0f, 0.9f, 0.7f);
		world.Add<EnvironmentComponent>(entity, environment);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0x5AF7 } });

		Nyx::Tests::TestDirectory directory("NyxLightShaftsTests");
		const std::filesystem::path path = directory.Path / "LightShafts.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<EnvironmentComponent>([&](Entity, const EnvironmentComponent& read)
			{
				bFound = true;
				Require(read.bLightShafts && read.LightShaftsIntensity == 1.5f && read.LightShaftsLength == 0.75f && read.LightShaftsDensity == 0.6f &&
						read.LightShaftsDepthThreshold == 250.0f && read.LightShaftsTint == glm::vec3(1.0f, 0.9f, 0.7f),
					"The light shafts aren't read back as saved");
			});
		Require(bFound, "The environment isn't saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestOffByDefault();
		TestSunAhead();
		TestSunOffThePicture();
		TestSunSets();
		TestDecay();
		TestInRange();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All light shafts tests passed\n";
	return 0;
}
