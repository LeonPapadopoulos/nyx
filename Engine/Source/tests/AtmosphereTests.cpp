// The world's air without a GPU (Atmosphere.h): the sun's light through the air, white overhead,
// redder and weaker lower down, gone once it has set; the daylight; the view's air made from an
// environment, in range, the sky tinted by the sun's hue without its brightness; the height fog's
// depth along a ray against the fog summed in small steps, level, up, down, from where it starts,
// from below and above its height, held at its most opacity; the light it scatters, gathered round
// the sun; and the component saved in a scene and read back.
#include "Atmosphere.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <string>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float Pi = 3.14159265358979f;

	std::string Text(const glm::vec3& v)
	{
		return std::to_string(v.x) + " " + std::to_string(v.y) + " " + std::to_string(v.z);
	}

	float Luminance(const glm::vec3& color)
	{
		return glm::dot(color, glm::vec3(0.2126f, 0.7152f, 0.0722f));
	}

	float SinOf(float degrees)
	{
		return std::sin(degrees * Pi / 180.0f);
	}

	void TestSunTransmittance()
	{
		// Overhead, the light as it is
		const glm::vec3 overhead = GetSunTransmittance(1.0f, 1.0f);
		Require(glm::all(glm::lessThan(glm::abs(overhead - glm::vec3(1.0f)), glm::vec3(1e-4f))), "The sun overhead isn't white: " + Text(overhead));

		// Lower, weaker and redder: red keeps the most, blue the least
		glm::vec3 above = overhead;
		for (const float degrees : { 60.0f, 30.0f, 15.0f, 5.0f, 1.0f })
		{
			const glm::vec3 t = GetSunTransmittance(SinOf(degrees), 1.0f);
			Require(t.r > t.g && t.g > t.b, "The sun at " + std::to_string(degrees) + " degrees isn't redder: " + Text(t));
			Require(glm::all(glm::lessThan(t, above)), "The sun at " + std::to_string(degrees) + " degrees isn't weaker than higher up: " + Text(t));
			above = t;
		}
		// Low down, about the sun's colours at sunset: warm, a fraction of the light
		const glm::vec3 sunset = GetSunTransmittance(SinOf(5.0f), 1.0f);
		Require(sunset.r > 0.5f && sunset.r < 0.8f && sunset.b < 0.2f, "The sun 5 degrees up isn't a sunset's: " + Text(sunset));
		const glm::vec3 afternoon = GetSunTransmittance(SinOf(30.0f), 1.0f);
		Require(afternoon.b > 0.7f, "The sun 30 degrees up is too warm: " + Text(afternoon));

		// Set: none
		Require(GetSunTransmittance(SinOf(-5.0f), 1.0f) == glm::vec3(0.0f), "The sun below the horizon still shines");
		// Without reddening, white, but still gone once set
		Require(GetSunTransmittance(SinOf(5.0f), 0.0f) == glm::vec3(1.0f), "The sun without reddening isn't white");
		Require(GetSunTransmittance(SinOf(-5.0f), 0.0f) == glm::vec3(0.0f), "The sun without reddening shines below the horizon");
		// More reddening, redder
		const glm::vec3 hazy = GetSunTransmittance(SinOf(10.0f), 2.0f);
		const glm::vec3 clear = GetSunTransmittance(SinOf(10.0f), 1.0f);
		Require(hazy.b / hazy.r < clear.b / clear.r, "More reddening isn't redder");
	}

	void TestDaylight()
	{
		Require(std::abs(GetDaylight(1.0f, 0.05f) - 1.0f) < 1e-6f && std::abs(GetDaylight(SinOf(20.0f), 0.05f) - 1.0f) < 1e-6f, "The day's sky isn't at full brightness");
		Require(std::abs(GetDaylight(-0.5f, 0.05f) - 0.05f) < 1e-6f, "The night's sky isn't at the night's brightness");
		const float dusk = GetDaylight(SinOf(0.0f), 0.05f);
		Require(dusk > 0.05f && dusk < 1.0f, "Dusk isn't between night and day: " + std::to_string(dusk));
		Require(GetDaylight(-0.5f, 2.0f) == 1.0f && GetDaylight(-0.5f, -1.0f) == 0.0f, "The night's brightness isn't kept from 0 to 1");
	}

	void TestViewAtmosphere()
	{
		// Without fog or the sky following, nothing: the light as it is, a white sky
		EnvironmentComponent off;
		off.bFog = false;
		off.bSkyFollowsSun = false;
		const SceneViewAtmosphere none = MakeSceneViewAtmosphere(off, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f, 1.0f, 0.5f));
		Require(none.FogDensity == 0.0f && none.SkyTint == glm::vec3(1.0f) && none.SunGlowColor == glm::vec3(0.0f) && none.SunColor == glm::vec3(2.0f, 1.0f, 0.5f),
			"An environment with all off changes the view");
		Require(GetHeightFogTransmittance(none, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1000.0f)) == 1.0f, "No fog hides something");

		// A white sun overhead: a white sky at full brightness, the light unchanged
		const EnvironmentComponent defaults;
		const SceneViewAtmosphere noon = MakeSceneViewAtmosphere(defaults, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f));
		Require(glm::all(glm::lessThan(glm::abs(noon.SkyTint - glm::vec3(1.0f)), glm::vec3(1e-3f))), "The noon sky isn't white: " + Text(noon.SkyTint));
		Require(glm::all(glm::lessThan(glm::abs(noon.SunColor - glm::vec3(1.0f)), glm::vec3(1e-3f))), "The noon sun isn't the light's colour: " + Text(noon.SunColor));
		Require(noon.FogDensity == defaults.FogDensity && noon.FogColor == defaults.FogColor * noon.SkyTint, "The fog isn't the environment's");

		// A low sun: a warmer sky, as bright as the daylight says (the hue has no brightness of its
		// own), a weaker and redder light, warmer fog
		const glm::vec3 low = glm::normalize(glm::vec3(0.0f, SinOf(4.0f), -1.0f));
		const SceneViewAtmosphere sunset = MakeSceneViewAtmosphere(defaults, low, glm::vec3(3.0f));
		Require(sunset.SkyTint.r > sunset.SkyTint.b * 1.5f, "The sky under a low sun isn't warm: " + Text(sunset.SkyTint));
		Require(std::abs(Luminance(sunset.SkyTint) - GetDaylight(low.y, defaults.NightBrightness)) < 1e-3f, "The sky's tint has a brightness of its own: " + Text(sunset.SkyTint));
		Require(sunset.SunColor.r < 3.0f && sunset.SunColor.r > sunset.SunColor.b * 3.0f, "The low sun isn't weaker and redder: " + Text(sunset.SunColor));
		Require(sunset.FogColor.r / sunset.FogColor.b > defaults.FogColor.r / defaults.FogColor.b, "The fog isn't warmer under a low sun");
		Require(sunset.FogSunColor == sunset.SunColor * defaults.FogSunScattering, "The fog doesn't scatter the light as it reaches the ground");

		// A warm light tints the sky by half its hue at the default sun tint, none at 0
		EnvironmentComponent untinted = defaults;
		untinted.SunTint = 0.0f;
		Require(glm::all(glm::lessThan(glm::abs(MakeSceneViewAtmosphere(untinted, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.6f, 0.3f)).SkyTint - glm::vec3(1.0f)), glm::vec3(1e-4f))),
			"A sun tint of 0 tints the sky");

		// Night: the night's brightness, no glow, no light
		const SceneViewAtmosphere night = MakeSceneViewAtmosphere(defaults, glm::normalize(glm::vec3(0.0f, -0.5f, -1.0f)), glm::vec3(1.0f));
		Require(std::abs(Luminance(night.SkyTint) - defaults.NightBrightness) < 1e-3f && night.SunGlowColor == glm::vec3(0.0f) && night.SunColor == glm::vec3(0.0f),
			"The night isn't dark: tint " + Text(night.SkyTint) + ", sun " + Text(night.SunColor));

		// Values out of range brought into it
		EnvironmentComponent wild;
		wild.FogHeightFalloff = -1.0f;
		wild.FogStartDistance = -5.0f;
		wild.FogMaxOpacity = 4.0f;
		wild.FogSunLobe = 3.0f;
		wild.SunGlowLobe = -2.0f;
		wild.FogColor = glm::vec3(-1.0f, 0.5f, 2.0f);
		const SceneViewAtmosphere tamed = MakeSceneViewAtmosphere(wild, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f));
		Require(tamed.FogHeightFalloff == 0.0f && tamed.FogStartDistance == 0.0f && tamed.FogMaxOpacity == 1.0f && tamed.FogSunLobe == 0.95f && tamed.SunGlowLobe == 0.0f && tamed.FogColor.r == 0.0f,
			"The environment's values aren't brought into range");
		EnvironmentComponent negative = defaults;
		negative.FogDensity = -0.1f;
		Require(MakeSceneViewAtmosphere(negative, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f)).FogDensity == 0.0f, "A negative density makes fog");
	}

	// The fog along the ray, summed in many small steps
	float SummedDepth(const SceneViewAtmosphere& atmosphere, const glm::vec3& from, const glm::vec3& to)
	{
		const glm::vec3 ray = to - from;
		const float distance = glm::length(ray);
		const glm::vec3 direction = ray / distance;
		constexpr int Steps = 200000;
		const float reach = std::max(distance - atmosphere.FogStartDistance, 0.0f);
		const float step = reach / Steps;
		double depth = 0.0;
		for (int index = 0; index < Steps; ++index)
		{
			const glm::vec3 point = from + direction * (atmosphere.FogStartDistance + (index + 0.5f) * step);
			depth += atmosphere.FogDensity * std::exp(-atmosphere.FogHeightFalloff * (point.y - atmosphere.FogHeight)) * step;
		}
		return static_cast<float>(depth);
	}

	void TestHeightFog()
	{
		SceneViewAtmosphere atmosphere;
		atmosphere.FogDensity = 0.02f;
		atmosphere.FogHeightFalloff = 0.2f;
		atmosphere.FogHeight = 1.0f;
		atmosphere.FogMaxOpacity = 1.0f;

		struct Ray
		{
			glm::vec3 From;
			glm::vec3 To;
			const char* What;
		};
		const Ray rays[] = {
			{ glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, -100.0f), "level at the fog's height" },
			{ glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(30.0f, 40.0f, -200.0f), "up" },
			{ glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(10.0f, 0.0f, -60.0f), "down from above" },
			{ glm::vec3(0.0f, -5.0f, 0.0f), glm::vec3(0.0f, -6.0f, -80.0f), "below the fog's height" },
			{ glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0001f, -50.0f), "almost level" },
			{ glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f, 2.0f + SkyFogDistance * 0.05f, -SkyFogDistance), "to the sky a little up" },
		};
		for (const float start : { 0.0f, 15.0f })
		{
			atmosphere.FogStartDistance = start;
			for (const Ray& ray : rays)
			{
				const float depth = GetHeightFogDepth(atmosphere, ray.From, ray.To);
				const float summed = SummedDepth(atmosphere, ray.From, ray.To);
				Require(std::abs(depth - summed) <= 1e-3f + summed * 2e-3f,
					std::string("The fog ") + ray.What + (start > 0.0f ? " from 15 m on" : "") + " is " + std::to_string(depth) + " deep, summed " + std::to_string(summed));
			}
		}

		// Level at its height: the density times the distance, from where the fog starts
		atmosphere.FogStartDistance = 10.0f;
		Require(std::abs(GetHeightFogDepth(atmosphere, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, -50.0f)) - 0.8f) < 1e-5f, "Level fog isn't its density times the distance");
		Require(GetHeightFogDepth(atmosphere, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 1.0f, -9.0f)) == 0.0f, "Fog before it starts");
		atmosphere.FogStartDistance = 0.0f;

		// Further, more; the same distance higher up, less
		const glm::vec3 camera(0.0f, 2.0f, 0.0f);
		Require(GetHeightFogDepth(atmosphere, camera, glm::vec3(0.0f, 2.0f, -80.0f)) > GetHeightFogDepth(atmosphere, camera, glm::vec3(0.0f, 2.0f, -40.0f)), "More distance isn't more fog");
		Require(GetHeightFogDepth(atmosphere, camera + glm::vec3(0.0f, 10.0f, 0.0f), glm::vec3(0.0f, 12.0f, -40.0f)) < GetHeightFogDepth(atmosphere, camera, glm::vec3(0.0f, 2.0f, -40.0f)),
			"Fog higher up isn't thinner");
		// Without falloff, the same at every height
		atmosphere.FogHeightFalloff = 0.0f;
		Require(std::abs(GetHeightFogDepth(atmosphere, glm::vec3(0.0f, 50.0f, 0.0f), glm::vec3(0.0f, 0.0f, -50.0f)) - 0.02f * std::sqrt(5000.0f)) < 1e-4f, "Fog without falloff isn't even");
		atmosphere.FogHeightFalloff = 0.2f;

		// The transmittance: exp of the depth, held at the most opacity
		const float depth = GetHeightFogDepth(atmosphere, camera, glm::vec3(0.0f, 2.0f, -60.0f));
		Require(std::abs(GetHeightFogTransmittance(atmosphere, camera, glm::vec3(0.0f, 2.0f, -60.0f)) - std::exp(-depth)) < 1e-6f, "The transmittance isn't the depth's");
		atmosphere.FogMaxOpacity = 0.25f;
		Require(GetHeightFogTransmittance(atmosphere, camera, glm::vec3(0.0f, 2.0f, -6000.0f)) == 0.75f, "Fog past its most opacity");
		// Far down from very high, or far below its height: finite, all hidden
		atmosphere.FogMaxOpacity = 1.0f;
		const float deep = GetHeightFogTransmittance(atmosphere, glm::vec3(0.0f, -400.0f, 0.0f), glm::vec3(0.0f, -900.0f, -100.0f));
		const float steep = GetHeightFogTransmittance(atmosphere, glm::vec3(0.0f, 900.0f, 0.0f), glm::vec3(0.0f, -900.0f, -1.0f));
		Require(std::isfinite(deep) && deep == 0.0f && std::isfinite(steep) && steep < 1e-6f, "Fog far below its height isn't wholly opaque: " + std::to_string(deep) + ", " + std::to_string(steep));

		// The inscattering: the fog's colour, and the sun's round it
		atmosphere.FogColor = glm::vec3(0.5f, 0.6f, 0.7f);
		atmosphere.FogSunColor = glm::vec3(2.0f, 1.5f, 1.0f);
		atmosphere.FogSunLobe = 0.6f;
		const glm::vec3 towardsLight(0.0f, 0.0f, -1.0f);
		const glm::vec3 toSun = GetHeightFogInscattering(atmosphere, towardsLight, towardsLight);
		const glm::vec3 away = GetHeightFogInscattering(atmosphere, -towardsLight, towardsLight);
		Require(toSun.r > away.r * 3.0f && away.r > atmosphere.FogColor.r, "The fog doesn't glow towards the sun: " + Text(toSun) + " against " + Text(away));
		atmosphere.FogSunLobe = 0.0f;
		Require(glm::all(glm::lessThan(glm::abs(GetHeightFogInscattering(atmosphere, towardsLight, towardsLight) - GetHeightFogInscattering(atmosphere, -towardsLight, towardsLight)), glm::vec3(1e-6f))),
			"A lobe of 0 isn't the same all round");
	}

	void TestHenyeyGreenstein()
	{
		// Over the sphere it sums to 1, for any g
		for (const float g : { 0.0f, 0.3f, 0.6f, 0.9f })
		{
			double sum = 0.0;
			constexpr int Steps = 20000;
			for (int index = 0; index < Steps; ++index)
			{
				const float cosine = -1.0f + (index + 0.5f) * 2.0f / Steps;
				sum += GetHenyeyGreenstein(cosine, g) * 2.0 * Pi * (2.0 / Steps);
			}
			Require(std::abs(sum - 1.0) < 1e-3, "Henyey-Greenstein with g " + std::to_string(g) + " sums to " + std::to_string(sum));
		}
		Require(std::abs(GetHenyeyGreenstein(0.3f, 0.0f) - 1.0f / (4.0f * Pi)) < 1e-6f, "Henyey-Greenstein with g 0 isn't the same every way");
	}

	void TestChromaticity()
	{
		Require(GetChromaticity(glm::vec3(0.0f)) == glm::vec3(1.0f), "Black's hue isn't white");
		const glm::vec3 warm = GetChromaticity(glm::vec3(3.0f, 1.5f, 0.3f));
		Require(std::abs(Luminance(warm) - 1.0f) < 1e-5f && std::abs(warm.r / warm.b - 10.0f) < 1e-3f, "The hue isn't the colour at luminance 1: " + Text(warm));
	}

	// Saved with a scene and read back as it was
	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		EnvironmentComponent environment;
		environment.FogDensity = 0.0123f;
		environment.FogColor = glm::vec3(0.1f, 0.2f, 0.3f);
		environment.bSkyFollowsSun = false;
		environment.SunGlowLobe = 0.5f;
		world.Add<EnvironmentComponent>(entity, environment);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xE2F } });

		Nyx::Tests::TestDirectory directory("NyxAtmosphereTests");
		const std::filesystem::path path = directory.Path / "Environment.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		bool bFound = false;
		loaded.Each<EnvironmentComponent>([&](Entity, const EnvironmentComponent& read)
			{
				bFound = true;
				Require(read.FogDensity == 0.0123f && read.FogColor == glm::vec3(0.1f, 0.2f, 0.3f) && !read.bSkyFollowsSun && read.SunGlowLobe == 0.5f && read.bFog,
					"The environment isn't read back as saved");
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
		TestSunTransmittance();
		TestDaylight();
		TestViewAtmosphere();
		TestHeightFog();
		TestHenyeyGreenstein();
		TestChromaticity();
		TestSavedWithTheScene();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All atmosphere tests passed\n";
	return 0;
}
