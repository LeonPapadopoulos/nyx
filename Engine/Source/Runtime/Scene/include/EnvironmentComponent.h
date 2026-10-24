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

	// The world's air and sky, and what the camera makes of bright light, as every view of the
	// world draws them: height fog, thick near the ground and thinning upwards, which hides what is
	// far away in its colour and glows towards the sun; a sky that follows the primary light, its
	// colour tinted by the sun's, dimmer as the sun goes down, its light warmer and weaker the lower
	// the sun is, through more air; bloom, the light of what is brightest spread round it; and
	// ambient occlusion, the sky's light kept out of creases, corners and the feet of things. The
	// world's first such component counts; without one, nothing of this is drawn.
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

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Color", UI = Color, Tooltip = "The fog's own colour, as the sky lights it; the sky's tint tints it too"))
		glm::vec3 FogColor{ 0.62f, 0.7f, 0.8f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Max Opacity", DragSpeed = 0.01, Tooltip = "How much the fog hides at most, 0 to 1: below 1, the far hills and the horizon still show through"))
		float FogMaxOpacity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Sun Scattering", DragSpeed = 0.01, Tooltip = "How much of the sun's light the fog scatters towards the camera, tinted by the light: the glow of haze towards the sun"))
		float FogSunScattering = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Fog", DisplayName = "Sun Lobe", DragSpeed = 0.01, Tooltip = "How much the scattered sunlight gathers round the sun, 0 to 0.95: 0 the same all round, near 1 a tight glow"))
		float FogSunLobe = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Follow Sun", Tooltip = "Whether the sky, its light and the fog follow the primary light: tinted by its colour, dimmer as it goes down, its light warmer the lower it is"))
		bool bSkyFollowsSun = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Tint", DragSpeed = 0.01, Tooltip = "How much the sun's colour tints the sky and its light, 0 to 1"))
		float SunTint = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Reddening", DragSpeed = 0.01, Tooltip = "How much the air warms and dims the sun's light as it nears the horizon: 0 none, 1 about as the earth's air does, more for a hazy sky"))
		float SunReddening = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Night Brightness", DragSpeed = 0.005, Tooltip = "How bright the sky and its light are with the sun below the horizon, against the day's, 0 to 1"))
		float NightBrightness = 0.05f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Glow", DragSpeed = 0.01, Tooltip = "How bright the glow round the sun is in the sky"))
		float SunGlow = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Sky", DisplayName = "Sun Glow Lobe", DragSpeed = 0.01, Tooltip = "How much the glow gathers round the sun, 0 to 0.99: near 1 a small, bright glow"))
		float SunGlowLobe = 0.8f;

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
	};
}

#include "Generated/Runtime/EnvironmentComponent.reflect.h"
