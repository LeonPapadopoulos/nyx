#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// How many samples ambient occlusion takes a pixel: more are smoother and cost more
	NYX_ENUM()
	enum class EAmbientOcclusionQuality : uint8_t
	{
		Low,
		Medium,
		High
	};

	// How the sky is made. Saved as its number: new modes go at the end.
	NYX_ENUM()
	enum class ESkyMode : uint8_t
	{
		// From the sun's place, by the light the air scatters (Hillaire's sky): blue by day,
		// orange and red at sunset, darker above and the horizon lower for a camera high up; its
		// light lights the world, and the fog takes its colour
		Atmosphere,
		// The sky's cube map, tinted by the sun's colour and dimmed as it goes down
		TintedCubeMap,
		// The sky's cube map as it is, whatever the sun does
		CubeMap
	};

	// The world's air and sky, and what the camera makes of bright light, as every view of the
	// world draws them: height fog, thick near the ground and thinning upwards, which hides what is
	// far away in its colour and glows towards the sun; a sky that follows the primary light, made
	// by the air's scattering from the sun's place (or its cube map tinted by the sun's colour),
	// dimmer as the sun goes down, its light warmer and weaker the lower the sun is; bloom, the light of what is brightest spread round it; and
	// ambient occlusion, the sky's light kept out of creases, corners and the feet of things; light
	// shafts, the sun streaming through gaps in what stands before it; and for particles, the wind that carries them and how many the world has at most. The world's
	// first such component counts; without one, nothing of this is drawn, no wind blows and the
	// world has up to 50 000 particles.
	NYX_REFLECT(Component, meta = (DisplayName = "Environment"))
	struct EnvironmentComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Height Fog", Tooltip = "Whether the air is thick with fog near the ground"))
		bool bFog = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Density", DragSpeed = 0.0005, Tooltip = "How thick the fog is at its height: the part of the light it takes away a metre, 0.01 about 1 in 100"))
		float FogDensity = 0.006f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Height Falloff", DragSpeed = 0.005, Tooltip = "How fast the fog thins upwards: 0.1 halves it about every 7 m, 0 the same at every height"))
		float FogHeightFalloff = 0.1f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Height", DragSpeed = 0.1, Tooltip = "Where the fog is as thick as its density says, in metres: thicker below, thinner above"))
		float FogHeight = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Start Distance", DragSpeed = 0.1, Tooltip = "How far from the camera the fog starts, in metres: nothing nearer is fogged"))
		float FogStartDistance = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Color", UI = Color, Tooltip = "The fog's own colour, as the sky lights it; the sky's tint tints it too. Under the Atmosphere sky, the fog takes the sky's colour at the horizon instead"))
		glm::vec3 FogColor{ 0.62f, 0.7f, 0.8f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Max Opacity", DragSpeed = 0.01, Tooltip = "How much the fog hides at most, 0 to 1: below 1, the far hills and the horizon still show through"))
		float FogMaxOpacity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Sun Scattering", DragSpeed = 0.01, Tooltip = "How much of the sun's light the fog scatters towards the camera, tinted by the light: the glow of haze towards the sun"))
		float FogSunScattering = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Sun Lobe", DragSpeed = 0.01, Tooltip = "How much the scattered sunlight gathers round the sun, 0 to 0.95: 0 the same all round, near 1 a tight glow"))
		float FogSunLobe = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Haze Brightness", DragSpeed = 0.01, Tooltip = "Atmosphere: what the fog's colour, the sky's the way each ray looks, is multiplied by: above 1 a brighter, more luminous haze, below 1 a darker one; 1 the sky's own"))
		float HazeBrightness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Haze Tint", UI = Color, Tooltip = "Atmosphere: a colour the fog's colour, the sky's the way each ray looks, is multiplied by, as Color is the fog's own in the cube map modes; white the sky's own"))
		glm::vec3 HazeTint{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mode", Tooltip = "Atmosphere: the sky made from the sun's place by the air's scattering, which lights the world. Tinted Cube Map: the sky's cube map tinted by the sun (the Tinted Cube Map settings below). Cube Map: the sky's cube map as it is"))
		ESkyMode SkyMode = ESkyMode::Atmosphere;

		// Follow Sun, as scenes saved before the sky had three modes have it: off, the sky was the
		// cube map as it is, whatever the mode said. Read from those scenes and folded into the
		// mode as they load (FoldLegacySkyMode()), then on; not shown, and nothing reads it after
		NYX_PROPERTY(Serialize, Hidden)
		bool bSkyFollowsSun = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Tint", DragSpeed = 0.01, Tooltip = "Tinted Cube Map: how much the sun's colour tints the sky and its light, 0 to 1"))
		float SunTint = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Reddening", DragSpeed = 0.01, Tooltip = "Tinted Cube Map: how much the air warms and dims the sun's light as it nears the horizon: 0 none, 1 about as the earth's air does, more for a hazy sky"))
		float SunReddening = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Night Brightness", DragSpeed = 0.005, Tooltip = "Tinted Cube Map: how bright the sky and its light are with the sun below the horizon, against the day's, 0 to 1"))
		float NightBrightness = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Glow", DragSpeed = 0.01, Tooltip = "Tinted Cube Map: how bright the glow round the sun is in the sky"))
		float SunGlow = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Glow Lobe", DragSpeed = 0.01, Tooltip = "Tinted Cube Map: how much the glow gathers round the sun, 0 to 0.99: near 1 a small, bright glow"))
		float SunGlowLobe = 0.8f;

		// The Atmosphere mode's air (SkyAtmosphere.h): what a kilometre of it at the ground
		// scatters and takes away, as a colour times a scale, thinning upwards; the earth's
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Rayleigh Scattering", UI = Color, Tooltip = "Atmosphere: the colour the air's molecules scatter, times the scale: more of the blue, which makes the sky blue and the low sun red"))
		glm::vec3 RayleighScattering{ 0.175287f, 0.409607f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Rayleigh Scale", DragSpeed = 0.0005, Tooltip = "Atmosphere: how much of the light a kilometre of air at the ground scatters, of the colour's brightest part: the earth's about 0.033"))
		float RayleighScatteringScale = 0.0331f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Rayleigh Height", DragSpeed = 0.05, Tooltip = "Atmosphere: how high the molecules reach, in kilometres: their density falls to about a third every this much higher, the earth's 8"))
		float RayleighHeight = 8.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mie Scattering", UI = Color, Tooltip = "Atmosphere: the colour haze and dust scatter, times the scale: about white, mostly forwards, the glow round the sun"))
		glm::vec3 MieScattering{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mie Scale", DragSpeed = 0.0001, Tooltip = "Atmosphere: how much of the light a kilometre of haze at the ground scatters: the earth's clear air about 0.004, more for a hazy sky"))
		float MieScatteringScale = 0.003996f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mie Absorption", DragSpeed = 0.0001, Tooltip = "Atmosphere: how much of the light a kilometre of haze at the ground takes away without scattering it"))
		float MieAbsorptionScale = 0.000444f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mie Height", DragSpeed = 0.01, Tooltip = "Atmosphere: how high the haze reaches, in kilometres: its density falls to about a third every this much higher, the earth's 1.2"))
		float MieHeight = 1.2f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Mie Anisotropy", DragSpeed = 0.01, Tooltip = "Atmosphere: how much the haze scatters forwards, 0 to 0.95: near 1 a tight, bright glow round the sun"))
		float MieAnisotropy = 0.8f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Ozone Absorption", UI = Color, Tooltip = "Atmosphere: the colour the ozone high up takes away, times the scale: mostly orange, which keeps the sky blue at dusk"))
		glm::vec3 OzoneAbsorption{ 0.345561f, 1.0f, 0.045189f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Ozone Scale", DragSpeed = 0.0001, Tooltip = "Atmosphere: how much of the light a kilometre of the ozone layer takes away where it is thickest, 25 km up: the earth's about 0.0019"))
		float OzoneAbsorptionScale = 0.001881f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Ground Albedo", UI = Color, Tooltip = "Atmosphere: how much of the light the planet's ground sends back up, as the sky sees it below the horizon: the light from below"))
		glm::vec3 GroundAlbedo{ 0.3f, 0.3f, 0.3f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Planet Radius", DragSpeed = 10.0, Tooltip = "Atmosphere: how big the planet under the world is, in kilometres: the earth's 6360. The world's height 0 is its ground; the smaller it is, the faster the horizon dips as the camera rises"))
		float PlanetRadius = 6360.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Atmosphere Height", DragSpeed = 1.0, Tooltip = "Atmosphere: how high the air reaches above the ground, in kilometres: the earth's 100"))
		float AtmosphereHeight = 100.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Disk Size", DragSpeed = 0.01, Tooltip = "Atmosphere: how wide the sun's disk looks, in degrees: the real sun's about 0.53"))
		float SunDiskSize = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Disk Brightness", DragSpeed = 0.5, Tooltip = "Atmosphere: how bright the sun's disk is, as a multiple of its light (what a white surface facing it shows): far above 1, so that it blooms"))
		float SunDiskBrightness = 40.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Warmth", DragSpeed = 0.01, Tooltip = "Atmosphere: turns the sky's light warmer (above 0) or cooler (below), as bright as it was, -1 to 1: a stylising offset on what the air gives"))
		float SkyWarmth = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Night Sky Brightness", DragSpeed = 0.001, Tooltip = "Atmosphere: an artistic light the sky keeps at night, so that it isn't black: its radiance in the Night Sky Color, fading in as the sun goes down and out as it rises, lighting the world as the sky does. 0: none, the air's own sky, black at night"))
		float NightSkyBrightness = 0.02f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Night Sky Color", UI = Color, Tooltip = "Atmosphere: the colour of the sky's light at night (Night Sky Brightness)"))
		glm::vec3 NightSkyColor{ 0.3f, 0.45f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sky Light Intensity", DragSpeed = 0.01, Tooltip = "Atmosphere: what the sky's light on surfaces is multiplied by, diffuse and reflected, not how the sky itself looks: above 1 brighter shade and fill, as a painted sky gives; 1 the air's own"))
		float SkyLightIntensity = 2.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Exposure", DisplayName = "Exposure Compensation", DragSpeed = 0.01, Tooltip = "How much brighter (above 0) or darker the whole picture is made before tonemapping, in stops (EV): +1 twice as bright; on top of the view's own exposure, in every sky mode"))
		float ExposureCompensation = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Bloom", Tooltip = "Whether light brighter than the threshold spreads round what gives it off, as a lens spreads it: emissive surfaces and decals glow"))
		bool bBloom = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Threshold", DragSpeed = 0.01, Tooltip = "How bright light must be to bloom, after the exposure: 1 is white on the screen"))
		float BloomThreshold = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Knee", DragSpeed = 0.01, Tooltip = "How softly blooming starts round the threshold: light this much below it starts to bloom a little; 0 a hard start"))
		float BloomKnee = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Intensity", DragSpeed = 0.01, Tooltip = "How much of the spread light is added to the picture"))
		float BloomIntensity = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Radius", DragSpeed = 0.01, Tooltip = "How far the light spreads, 0 to 1: how much each wider level of the blur weighs against the one above it"))
		float BloomRadius = 0.7f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Bloom", DisplayName = "Levels", Tooltip = "How many times the picture is halved, 1 to 8: each level spreads the light twice as far"))
		int32_t BloomLevels = 6;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ambient Occlusion", DisplayName = "Ambient Occlusion", Tooltip = "Whether the sky's light is kept out of creases, corners and the feet of things, by what is near them on the screen; the sun's light is left as it is"))
		bool bAmbientOcclusion = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ambient Occlusion", DisplayName = "Radius", DragSpeed = 0.01, Tooltip = "How far round a point what hides the sky from it is looked for, in metres"))
		float OcclusionRadius = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ambient Occlusion", DisplayName = "Intensity", DragSpeed = 0.01, Tooltip = "How dark the occlusion makes the sky's light, 0 to 8: 1 as found, more darker"))
		float OcclusionIntensity = 1.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Ambient Occlusion", DisplayName = "Quality", Tooltip = "How many samples a pixel takes: Low is cheapest and noisiest"))
		EAmbientOcclusionQuality OcclusionQuality = EAmbientOcclusionQuality::Medium;

		// Light shafts: the sky's light round the sun smeared out from it across the picture, held
		// back by what stands before it, so that the sun streams through gaps (VulkanLightShafts)
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Light Shafts", Tooltip = "Whether the sun's light streams through the gaps in what stands before it, in shafts out from the sun across the picture: while the sun is on or near the picture and above the horizon"))
		bool bLightShafts = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Intensity", DragSpeed = 0.01, Tooltip = "How bright the shafts are, 0 to 16: how much of the light they gather from the sky round the sun they add"))
		float LightShaftsIntensity = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Length", DragSpeed = 0.01, Tooltip = "How far the light of a shaft carries from the gap it comes through, as a part of the way to the sun: its light falls to a tenth over this part, 0.01 to 10"))
		float LightShaftsLength = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Density", DragSpeed = 0.01, Tooltip = "How far towards the sun each pixel gathers light, 0.05 to 1 of the way: less keeps the shafts away from the sun and makes them finer"))
		float LightShaftsDensity = 0.9f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Depth Threshold", DragSpeed = 1.0, Tooltip = "How far away, in metres, what stands before the sun starts to let its light through as the sky does, all of it at twice as far: nearer, it blocks the light"))
		float LightShaftsDepthThreshold = 1000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Light Shafts", DisplayName = "Tint", UI = Color, Tooltip = "Multiplies the shafts' colour, which is the sun's as it reaches the camera: white keeps the sun's"))
		glm::vec3 LightShaftsTint{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Particles", DragSpeed = 0.05, Tooltip = "The air's speed everywhere, in metres a second, which carries particles by their drag; a game's air field takes its place"))
		glm::vec3 Wind{ 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Particles", DisplayName = "Max Particles", Tooltip = "The most particles alive in the world at once, every emitter's together; no more are born until some die"))
		int32_t MaxParticles = 50000;
	};

	// A scene's Follow Sun, saved before the sky had three modes, folded into its mode: off, the
	// sky's cube map as it is (ESkyMode::CubeMap), whatever the mode said, as it looked; on, the
	// mode as it is. Then Follow Sun is on. The component's post-load step.
	void FoldLegacySkyMode(EnvironmentComponent& environment);
}

#include "Generated/Runtime/EnvironmentComponent.reflect.h"
