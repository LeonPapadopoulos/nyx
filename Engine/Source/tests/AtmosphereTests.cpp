// The world's air without a GPU (Atmosphere.h): the sun's light through the air, white overhead,
// redder and weaker lower down, gone once it has set; the daylight; the view's air made from an
// environment, in range, the sky tinted by the sun's hue without its brightness; the height fog's
// depth along a ray against the fog summed in small steps, level, up, down, from where it starts,
// from below and above its height, held at its most opacity; the light it scatters, gathered round
// the sun; bloom's levels, their sizes and its threshold's knee; ambient occlusion's size, samples
// and settings, and the visible part of a slice of the sky, open, beside a wall, between two, and
// averaged round the view for a tilted open surface; the sky made by the air's scattering: its
// transmittance, 1 at no distance and falling with the path, the sun's light redder low, the horizon
// lower for a camera high up, its settings in range and what is made again as they change; and the
// component saved in a scene and read back.
#include "AmbientOcclusion.h"
#include "Atmosphere.h"
#include "Bloom.h"
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SceneSerializer.h"
#include "SkyAtmosphere.h"
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

	// The environment's defaults with the sky's cube map tinted by the sun, as before the sky made
	// by the air's scattering
	EnvironmentComponent TintedSky()
	{
		EnvironmentComponent environment;
		environment.SkyMode = ESkyMode::TintedCubeMap;
		return environment;
	}

	void TestViewAtmosphere()
	{
		// Without fog or the sky following, nothing: the light as it is, a white sky
		EnvironmentComponent off;
		off.bFog = false;
		off.SkyMode = ESkyMode::CubeMap;
		const SceneViewAtmosphere none = MakeSceneViewAtmosphere(off, glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(2.0f, 1.0f, 0.5f));
		Require(none.FogDensity == 0.0f && none.SkyTint == glm::vec3(1.0f) && none.SunGlowColor == glm::vec3(0.0f) && none.SunColor == glm::vec3(2.0f, 1.0f, 0.5f),
			"An environment with all off changes the view");
		Require(GetHeightFogTransmittance(none, glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, -1000.0f)) == 1.0f, "No fog hides something");

		// A white sun overhead: a white sky at full brightness, the light unchanged
		const EnvironmentComponent defaults = TintedSky();
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

	glm::vec3 SunAt(float degrees)
	{
		return glm::normalize(glm::vec3(0.0f, SinOf(degrees), -std::cos(degrees * Pi / 180.0f)));
	}

	float RedOverBlue(const glm::vec3& color)
	{
		return color.r / std::max(color.b, 1e-12f);
	}

	// The sky made by the air's scattering, on the CPU (SkyAtmosphere.h)
	void TestSkyAtmosphere()
	{
		const EnvironmentComponent defaults;
		const glm::vec3 ground(0.0f, 2.0f, 0.0f);
		const SceneViewSkyAtmosphere sky = MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), ground);
		Require(sky.bAtmosphere, "The defaults don't make the sky by the air's scattering");
		Require(std::abs(sky.CameraHeight - 0.002f) < 1e-6f && sky.PlanetRadius == 6360.0f && sky.AtmosphereHeight == 100.0f, "The camera's height or the planet isn't in kilometres");
		Require(std::abs(sky.SunIlluminance.g - Pi) < 1e-5f, "The sun's illuminance isn't pi times its light");

		// The transmittance: 1 at no distance, less with every kilometre further
		Require(GetAtmosphereTransmittance(sky, 0.002f, 0.3f, 0.0f) == glm::vec3(1.0f), "Light doesn't all get through no air");
		glm::vec3 previous(1.0f);
		for (const float distance : { 0.5f, 2.0f, 10.0f, 40.0f, 150.0f, 400.0f })
		{
			const glm::vec3 through = GetAtmosphereTransmittance(sky, 0.002f, 0.05f, distance);
			Require(glm::all(glm::lessThan(through, previous)), "The transmittance doesn't fall over " + std::to_string(distance) + " km: " + Text(through));
			previous = through;
		}
		// Straight up most gets through, blue least; along the horizon far less, red most; into the
		// ground nothing
		const glm::vec3 up = GetAtmosphereTransmittance(sky, 0.002f, 1.0f);
		Require(up.b > 0.6f && up.b < up.g && up.g < up.r && up.r < 1.0f, "Straight up, the air isn't clear and bluish: " + Text(up));
		const glm::vec3 horizon = GetAtmosphereTransmittance(sky, 0.002f, 0.0f);
		Require(horizon.r < up.r * 0.5f && RedOverBlue(horizon) > 100.0f, "Along the horizon the air doesn't take most, the blue most of all: " + Text(horizon));
		Require(GetAtmosphereTransmittance(sky, 0.002f, -0.05f) == glm::vec3(0.0f), "Light gets through the ground");

		// The sun's light: redder and dimmer the lower the sun, gone once it has set
		float lastRedness = 0.0f;
		float lastLuminance = 1e9f;
		for (const float degrees : { 60.0f, 20.0f, 6.0f, 2.0f, 0.5f })
		{
			const SceneViewSkyAtmosphere at = MakeSceneViewSkyAtmosphere(defaults, SunAt(degrees), glm::vec3(1.0f), ground);
			const glm::vec3 light = GetSunLightThroughAtmosphere(at, glm::vec3(1.0f));
			Require(RedOverBlue(light) > lastRedness && Luminance(light) < lastLuminance, "The sun's light isn't redder and dimmer at " + std::to_string(degrees) + " degrees: " + Text(light));
			lastRedness = RedOverBlue(light);
			lastLuminance = Luminance(light);
		}
		const glm::vec3 noon = GetSunLightThroughAtmosphere(MakeSceneViewSkyAtmosphere(defaults, SunAt(90.0f), glm::vec3(1.0f), ground), glm::vec3(1.0f));
		Require(noon.r > 0.9f && noon.b > 0.6f, "The sun overhead isn't nearly as bright as its light: " + Text(noon));
		Require(GetSunLightThroughAtmosphere(MakeSceneViewSkyAtmosphere(defaults, SunAt(-2.0f), glm::vec3(1.0f), ground), glm::vec3(1.0f)) == glm::vec3(0.0f),
			"The sun shines below the horizon");
		// The Atmosphere mode's view air: the light through the air, no tint, no glow
		const SceneViewAtmosphere air = MakeSceneViewAtmosphere(defaults, SunAt(6.0f), glm::vec3(2.0f), ground);
		const glm::vec3 through = GetSunLightThroughAtmosphere(MakeSceneViewSkyAtmosphere(defaults, SunAt(6.0f), glm::vec3(2.0f), ground), glm::vec3(2.0f));
		Require(air.SkyTint == glm::vec3(1.0f) && air.SunGlowColor == glm::vec3(0.0f), "The air's sky is tinted, or has the cube map's glow");
		Require(glm::all(glm::lessThan(glm::abs(air.SunColor - through), glm::vec3(1e-6f))), "The light isn't the sun's through the air");

		// A camera high up: the horizon dips below the level (1.44 degrees at 2 km), and the low
		// sun shines through less air
		Require(GetHorizonCosine(6360.0f, 0.0f) == 0.0f, "On the ground the horizon isn't level");
		const float dip = std::acos(GetHorizonCosine(6360.0f, 2.0f)) * 180.0f / Pi - 90.0f;
		Require(std::abs(dip - 1.4367f) < 0.002f, "The horizon 2 km up doesn't dip 1.44 degrees: " + std::to_string(dip));
		// A metre apart, a few metres up, told apart
		Require(GetHorizonCosine(6360.0f, 0.002f) < GetHorizonCosine(6360.0f, 0.001f), "The horizon 1 m and 2 m up are the same");
		const SceneViewSkyAtmosphere high = MakeSceneViewSkyAtmosphere(defaults, SunAt(3.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2000.0f, 0.0f));
		Require(std::abs(high.CameraHeight - 2.0f) < 1e-6f, "The camera's height isn't 2 km");
		const glm::vec3 lowSunFromGround = GetSunLightThroughAtmosphere(MakeSceneViewSkyAtmosphere(defaults, SunAt(3.0f), glm::vec3(1.0f), ground), glm::vec3(1.0f));
		const glm::vec3 lowSunFromHigh = GetSunLightThroughAtmosphere(high, glm::vec3(1.0f));
		Require(glm::all(glm::greaterThan(lowSunFromHigh, lowSunFromGround)), "The low sun isn't brighter 2 km up: " + Text(lowSunFromHigh) + " against " + Text(lowSunFromGround));
		// 2 km up, a sun a degree below the level is still above the horizon
		Require(Luminance(GetSunLightThroughAtmosphere(MakeSceneViewSkyAtmosphere(defaults, SunAt(-1.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2000.0f, 0.0f)), glm::vec3(1.0f))) > 0.0f,
			"2 km up, the sun a degree below the level has set");

		// Off: the cube map as it is, or tinted
		EnvironmentComponent off;
		off.SkyMode = ESkyMode::CubeMap;
		Require(!MakeSceneViewSkyAtmosphere(off, SunAt(60.0f), glm::vec3(1.0f), ground).bAtmosphere, "The plain cube map makes the air's sky");
		const SceneViewAtmosphere plain = MakeSceneViewAtmosphere(off, SunAt(6.0f), glm::vec3(2.0f), ground);
		Require(plain.SkyTint == glm::vec3(1.0f) && plain.SunGlowColor == glm::vec3(0.0f) && plain.SunColor == glm::vec3(2.0f), "The plain cube map follows the sun");

		// The night's light: none by day, the Night Sky Color times its brightness at night, none
		// at 0
		const SceneViewSkyAtmosphere midnight = MakeSceneViewSkyAtmosphere(defaults, SunAt(-60.0f), glm::vec3(1.0f), ground);
		Require(sky.NightLight == glm::vec3(0.0f), "The night's light shines by day");
		Require(glm::all(glm::lessThan(glm::abs(midnight.NightLight - defaults.NightSkyColor * defaults.NightSkyBrightness), glm::vec3(1e-6f))),
			"The night's light isn't the Night Sky Color times its brightness: " + Text(midnight.NightLight));
		EnvironmentComponent physical = defaults;
		physical.NightSkyBrightness = 0.0f;
		Require(MakeSceneViewSkyAtmosphere(physical, SunAt(-60.0f), glm::vec3(1.0f), ground).NightLight == glm::vec3(0.0f), "A night brightness of 0 adds light");
		Require(GetSkyAtmosphereChanges(midnight, MakeSceneViewSkyAtmosphere(physical, SunAt(-60.0f), glm::vec3(1.0f), ground)).bSkyView,
			"The night's light changing doesn't make the sky view again");
		Require(!MakeSceneViewSkyAtmosphere(TintedSky(), SunAt(60.0f), glm::vec3(1.0f), ground).bAtmosphere, "The tinted cube map makes the air's sky");

		// Values out of range brought into it
		EnvironmentComponent wild;
		wild.RayleighScatteringScale = -1.0f;
		wild.RayleighHeight = 0.0f;
		wild.MieAnisotropy = 2.0f;
		wild.GroundAlbedo = glm::vec3(-1.0f, 0.5f, 3.0f);
		wild.PlanetRadius = 0.0f;
		wild.SkyWarmth = 5.0f;
		wild.SunDiskSize = -1.0f;
		const SceneViewSkyAtmosphere tamed = MakeSceneViewSkyAtmosphere(wild, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, -50.0f, 0.0f));
		Require(tamed.RayleighScattering == glm::vec3(0.0f) && tamed.RayleighHeight > 0.0f && tamed.MieAnisotropy == 0.95f && tamed.GroundAlbedo == glm::vec3(0.0f, 0.5f, 1.0f)
				&& tamed.PlanetRadius == 1.0f && tamed.WarmthTint == GetSkyWarmthTint(1.0f) && tamed.SunDiskCosine == std::cos(glm::radians(0.005f)) && tamed.CameraHeight == 0.001f,
			"The sky's values aren't brought into range");

		// Warmth: warmer above 0, cooler below, as bright
		const glm::vec3 warm = GetSkyWarmthTint(1.0f);
		const glm::vec3 cool = GetSkyWarmthTint(-1.0f);
		Require(GetSkyWarmthTint(0.0f) == glm::vec3(1.0f), "No warmth tints the sky");
		Require(RedOverBlue(warm) > 1.5f && RedOverBlue(cool) < 0.7f && std::abs(Luminance(warm) - 1.0f) < 1e-5f && std::abs(Luminance(cool) - 1.0f) < 1e-5f,
			"The warmth isn't warmer or cooler at the same brightness: " + Text(warm) + ", " + Text(cool));

		// What is made again: nothing for the same; the sky view for the sun turned noticeably, a
		// brighter light or a camera risen a noticeable part of its height; everything for the air
		const auto changes = [&](const SceneViewSkyAtmosphere& wanted)
		{
			const SkyAtmosphereChanges made = GetSkyAtmosphereChanges(sky, wanted);
			return std::to_string(made.bAir) + std::to_string(made.bSkyView);
		};
		Require(changes(sky) == "00", "The same sky is made again");
		const auto turned = [&](float degrees)
		{
			return MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f + degrees), glm::vec3(1.0f), ground);
		};
		Require(changes(turned(0.01f)) == "00" && changes(turned(0.1f)) == "01", "The sun turning 0.01 and 0.1 degrees: " + changes(turned(0.01f)) + ", " + changes(turned(0.1f)));
		Require(changes(MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.1f), ground)) == "01", "A brighter sun doesn't make the sky view again");
		Require(changes(MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2.8f, 0.0f))) == "00"
				&& changes(MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, 3.5f, 0.0f))) == "01",
			"The camera rising 0.8 m and 1.5 m");
		const SceneViewSkyAtmosphere atHeight = MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2000.0f, 0.0f));
		Require(!GetSkyAtmosphereChanges(atHeight, MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2015.0f, 0.0f))).bSkyView
				&& GetSkyAtmosphereChanges(atHeight, MakeSceneViewSkyAtmosphere(defaults, SunAt(60.0f), glm::vec3(1.0f), glm::vec3(0.0f, 2030.0f, 0.0f))).bSkyView,
			"2 km up, the camera rising 15 m and 30 m");
		EnvironmentComponent hazy = defaults;
		hazy.MieScatteringScale *= 3.0f;
		Require(changes(MakeSceneViewSkyAtmosphere(hazy, SunAt(60.0f), glm::vec3(1.0f), ground)) == "11", "Hazier air doesn't make everything again");
		EnvironmentComponent bigSun = defaults;
		bigSun.SunDiskSize = 3.0f;
		Require(changes(MakeSceneViewSkyAtmosphere(bigSun, SunAt(60.0f), glm::vec3(1.0f), ground)) == "00", "The sun's disk, drawn per pixel, makes the tables again");
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

	void TestBloom()
	{
		// Levels: the view halved again and again, rounded down, at least a pixel; as many as asked
		// for while the last keeps 8 pixels both ways, at least 1, at most 8
		Require(GetBloomLevelSize(1280, 720, 0) == glm::uvec2(640, 360) && GetBloomLevelSize(1280, 720, 5) == glm::uvec2(20, 11), "A level's size isn't the view halved");
		Require(GetBloomLevelSize(3, 1, 0) == glm::uvec2(1, 1), "A level is less than a pixel");
		Require(GetBloomLevelCount(1280, 720, 6) == 6 && GetBloomLevelCount(1280, 720, 3) == 3 && GetBloomLevelCount(1280, 720, 0) == 1, "The level count isn't as asked");
		Require(GetBloomLevelCount(1280, 720, 20) == 6 && GetBloomLevelCount(1920, 1080, 20) == 7 && GetBloomLevelCount(7680, 4320, 20) == MaxBloomLevels,
			"The level count doesn't stop where the last level would be under 8 pixels, or at 8 levels");
		Require(GetBloomLevelCount(480, 270, 6) == 5 && GetBloomLevelCount(64, 16, 8) == 1, "A small view's levels go under 8 pixels");
		Require(GetBloomLevelCount(1, 1, 6) == 1, "A view of a pixel has no level");

		// The threshold and its knee: nothing well below, the part above it well above, smoothly
		// from threshold - knee
		Require(GetBloomFactor(0.4f, 1.0f, 0.5f) == 0.0f, "Light below the knee blooms");
		Require(std::abs(GetBloomFactor(3.0f, 1.0f, 0.5f) - 2.0f / 3.0f) < 1e-6f, "Light above the knee doesn't bloom by its part above the threshold");
		float last = 0.0f;
		for (float brightness = 0.5f; brightness <= 1.6f; brightness += 0.01f)
		{
			const float bloomed = GetBloomFactor(brightness, 1.0f, 0.5f) * brightness;
			Require(bloomed >= last - 1e-6f, "The bloomed light isn't rising with the brightness at " + std::to_string(brightness));
			last = bloomed;
		}
		Require(GetBloomFactor(0.99f, 1.0f, 0.0f) == 0.0f && GetBloomFactor(1.5f, 1.0f, 0.0f) > 0.33f, "A knee of 0 isn't a hard start");

		// The view's settings from the environment, in range
		EnvironmentComponent environment;
		const SceneViewBloomSettings defaults = MakeSceneViewBloomSettings(environment);
		Require(defaults.bBloom && defaults.Threshold == 1.0f && defaults.Levels == 6, "The environment's bloom isn't on by its defaults");
		environment.BloomLevels = 40;
		environment.BloomRadius = 2.0f;
		environment.BloomKnee = -1.0f;
		const SceneViewBloomSettings tamed = MakeSceneViewBloomSettings(environment);
		Require(tamed.Levels == MaxBloomLevels && tamed.Radius == 1.0f && tamed.Knee == 0.0f, "The bloom's values aren't brought into range");
		environment.BloomIntensity = 0.0f;
		Require(!MakeSceneViewBloomSettings(environment).bBloom, "Bloom of no intensity is drawn");
	}

	// The visible part of an open surface, averaged over the slices round the view as
	// AmbientOcclusion.slang takes them: each slice's plane through the view direction, the normal
	// projected into it, its horizons as low as the surface lets them be
	float OpenSurfaceVisibility(const glm::vec3& normal, int slices)
	{
		const glm::vec3 toCamera(0.0f, 0.0f, -1.0f);
		double sum = 0.0;
		for (int slice = 0; slice < slices; ++slice)
		{
			const float phi = (slice + 0.5f) / slices * Pi;
			const glm::vec3 direction(std::cos(phi), std::sin(phi), 0.0f);
			const glm::vec3 ortho = direction - glm::dot(direction, toCamera) * toCamera;
			const glm::vec3 axis = glm::normalize(glm::cross(ortho, toCamera));
			const glm::vec3 projected = normal - axis * glm::dot(normal, axis);
			const float length = glm::length(projected);
			const float sign = glm::dot(ortho, projected) < 0.0f ? -1.0f : 1.0f;
			const float n = sign * std::acos(std::clamp(glm::dot(projected, toCamera) / length, -1.0f, 1.0f));
			sum += length * GetSliceVisibility(n, n - Pi * 0.5f, n + Pi * 0.5f);
		}
		return static_cast<float>(sum / slices);
	}

	void TestAmbientOcclusion()
	{
		// Half the view, at least a pixel
		Require(GetAmbientOcclusionSize(1280, 720) == glm::uvec2(640, 360) && GetAmbientOcclusionSize(3, 5) == glm::uvec2(1, 2) && GetAmbientOcclusionSize(1, 1) == glm::uvec2(1, 1),
			"The occlusion's size isn't half the view's");
		Require(GetAmbientOcclusionSamples(EAmbientOcclusionQuality::Low) == glm::uvec2(2, 4) && GetAmbientOcclusionSamples(EAmbientOcclusionQuality::Medium) == glm::uvec2(3, 6) &&
				GetAmbientOcclusionSamples(EAmbientOcclusionQuality::High) == glm::uvec2(4, 8),
			"The qualities' samples aren't 2 x 4, 3 x 6 and 4 x 8");

		// The settings from the environment, in range
		EnvironmentComponent environment;
		const SceneViewAmbientOcclusionSettings defaults = MakeSceneViewAmbientOcclusionSettings(environment);
		Require(defaults.bAmbientOcclusion && defaults.Radius == 1.0f && defaults.Slices == 3 && defaults.Steps == 6, "The environment's occlusion isn't on by its defaults");
		environment.OcclusionQuality = EAmbientOcclusionQuality::High;
		environment.OcclusionIntensity = 20.0f;
		environment.OcclusionRadius = 500.0f;
		const SceneViewAmbientOcclusionSettings tamed = MakeSceneViewAmbientOcclusionSettings(environment);
		Require(tamed.Slices == 4 && tamed.Steps == 8 && tamed.Intensity == 8.0f && tamed.Radius == 100.0f, "The occlusion's values aren't brought into range");
		environment.OcclusionIntensity = 0.0f;
		Require(!MakeSceneViewAmbientOcclusionSettings(environment).bAmbientOcclusion, "Occlusion of no intensity is drawn");

		// One slice of a surface facing the camera: open, all of it; a wall right beside it on one
		// side, half; on both, none; horizons beyond the surface's own count as its own
		Require(std::abs(GetSliceVisibility(0.0f, -Pi * 0.5f, Pi * 0.5f) - 1.0f) < 1e-6f, "An open slice isn't wholly visible");
		Require(std::abs(GetSliceVisibility(0.0f, -Pi * 0.5f, 0.0f) - 0.5f) < 1e-6f, "A slice with a wall on one side isn't half visible");
		Require(std::abs(GetSliceVisibility(0.0f, 0.0f, 0.0f)) < 1e-6f, "A slice walled in on both sides is visible");
		Require(GetSliceVisibility(0.0f, -3.0f, 3.0f) == GetSliceVisibility(0.0f, -Pi * 0.5f, Pi * 0.5f), "Horizons below the surface aren't its own");
		Require(GetSliceVisibility(0.0f, -Pi * 0.5f, 0.6f) < GetSliceVisibility(0.0f, -Pi * 0.5f, 1.2f), "A higher horizon doesn't hide more");

		// A tilted open surface, its slices round the view averaged: all of it visible
		for (const float tilt : { 0.0f, 0.4f, 0.9f, 1.3f })
		{
			const glm::vec3 normal(std::sin(tilt) * 0.6f, std::sin(tilt) * 0.8f, -std::cos(tilt));
			const float visibility = OpenSurfaceVisibility(normal, 4000);
			Require(std::abs(visibility - 1.0f) < 2e-3f, "An open surface tilted by " + std::to_string(tilt) + " isn't wholly visible: " + std::to_string(visibility));
		}
	}

	// Saved with a scene and read back as it was
	void TestSavedWithTheScene()
	{
		Registry world;
		const Entity entity = world.CreateEntity();
		EnvironmentComponent environment;
		environment.FogDensity = 0.0123f;
		environment.FogColor = glm::vec3(0.1f, 0.2f, 0.3f);
		environment.SunGlowLobe = 0.5f;
		environment.SkyMode = ESkyMode::TintedCubeMap;
		environment.RayleighScattering = glm::vec3(0.3f, 0.5f, 0.9f);
		environment.PlanetRadius = 2000.0f;
		environment.SkyWarmth = -0.25f;
		environment.NightSkyBrightness = 0.07f;
		world.Add<EnvironmentComponent>(entity, environment);
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid{ 0xE2F } });
		// Scenes saved before the sky had three modes: Follow Sun off made the sky the cube map as
		// it is, in either mode; on, the mode as it was
		const auto addLegacy = [&](uint64_t guid, bool bFollowsSun, ESkyMode mode)
		{
			const Entity legacy = world.CreateEntity();
			EnvironmentComponent old;
			old.bSkyFollowsSun = bFollowsSun;
			old.SkyMode = mode;
			world.Add<EnvironmentComponent>(legacy, old);
			world.Add<GuidComponent>(legacy, GuidComponent{ EntityGuid{ guid } });
		};
		addLegacy(0xE30, false, ESkyMode::TintedCubeMap);
		addLegacy(0xE31, false, ESkyMode::Atmosphere);
		addLegacy(0xE32, true, ESkyMode::TintedCubeMap);
		addLegacy(0xE33, true, ESkyMode::Atmosphere);

		Nyx::Tests::TestDirectory directory("NyxAtmosphereTests");
		const std::filesystem::path path = directory.Path / "Environment.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "The scene can't be saved");
		Registry loaded;
		ScenePostLoadContext context{};
		Require(SceneSerializer::LoadFromFile(path, loaded, context), "The scene can't be loaded");
		size_t found = 0;
		loaded.Each<EnvironmentComponent>([&](Entity entity, const EnvironmentComponent& read)
			{
				++found;
				Require(read.bSkyFollowsSun, "Follow Sun isn't folded into the mode as the scene loads");
				const uint64_t guid = loaded.Get<GuidComponent>(entity).Guid.Value;
				if (guid == 0xE2F)
				{
					Require(read.FogDensity == 0.0123f && read.FogColor == glm::vec3(0.1f, 0.2f, 0.3f) && read.SunGlowLobe == 0.5f && read.bFog,
						"The environment isn't read back as saved");
					Require(read.SkyMode == ESkyMode::TintedCubeMap && read.RayleighScattering == glm::vec3(0.3f, 0.5f, 0.9f) && read.PlanetRadius == 2000.0f
							&& read.SkyWarmth == -0.25f && read.NightSkyBrightness == 0.07f,
						"The sky's settings aren't read back as saved");
					return;
				}
				const ESkyMode expected = guid == 0xE30 || guid == 0xE31 ? ESkyMode::CubeMap : guid == 0xE32 ? ESkyMode::TintedCubeMap : ESkyMode::Atmosphere;
				Require(read.SkyMode == expected, "An old scene's sky doesn't load as it looked: " + std::to_string(guid));
			});
		Require(found == 5, "The environments aren't saved");
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
		TestSkyAtmosphere();
		TestHeightFog();
		TestHenyeyGreenstein();
		TestChromaticity();
		TestBloom();
		TestAmbientOcclusion();
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
