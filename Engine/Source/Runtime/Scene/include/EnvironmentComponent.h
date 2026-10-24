#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

namespace Nyx
{
	// The world's air and sky, as every view of the world draws them: height fog, thick near the
	// ground and thinning upwards, which hides what is far away in its colour and glows towards the
	// sun; and a sky that follows the primary light, its colour tinted by the sun's, dimmer as the
	// sun goes down, its light warmer and weaker the lower the sun is, through more air. The world's
	// first such component counts; without one, nothing of this is drawn.
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
	};
}

#include "Generated/Runtime/EnvironmentComponent.reflect.h"
