#pragma once

#include "ReflectionMacros.h"

#include <cstdint>

namespace Nyx
{
	// A thin layer of high cloud round the planet, at an altitude, as every view of the world draws
	// it with the sky made by the air's scattering (EnvironmentComponent's Atmosphere mode): a field
	// of small cloudlets (a mackerel sky) or streaks of cirrus, made from seeded noise and carried by
	// the wind with the world's time, lit by the sun through the air at its height (gold at sunset,
	// dark after it), with bright edges against the sun and the sky's own light on its shaded side,
	// fading into the haze towards the horizon; and in the sky's light on the world, which they
	// brighten, warm or soften. Seen from below, at its height and from above it. The world's first
	// such component counts; without one, or with the sky in another mode, there are no clouds and
	// nothing changes.
	NYX_REFLECT(Component, meta = (DisplayName = "Cloud Layer"))
	struct CloudLayerComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Clouds", Tooltip = "Whether the layer is drawn (with the sky's Atmosphere mode only)"))
		bool bClouds = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 10.0, Tooltip = "How high the layer is above the ground, in metres, 10 to 50 000"))
		float Altitude = 4000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 1.0, Tooltip = "How thick the layer's clouds are, in metres, 1 to 5000: thicker clouds are more opaque and darker where the sun shines through them; a camera this near the layer sees the clouds round it fade"))
		float Thickness = 300.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How much of the sky the clouds cover, 0 to 1: 0 clear, 1 overcast"))
		float Coverage = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "The clouds' shape, 0 to 1: 0 a field of small round cloudlets (a mackerel sky), 1 streaks of cirrus along the wind, blended between"))
		float Type = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Cloudlet Size", DragSpeed = 1.0, Tooltip = "How far apart the cloudlets are, in metres, 10 to 10 000; the streaks are as wide and longer"))
		float CloudletSize = 400.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How soft the clouds' edges are, 0.01 to 1"))
		float Softness = 0.25f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How much finer cells break up the clouds' edges, 0 to 1"))
		float Detail = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How much the pattern is bent by larger, slower noise, 0 to 1: 0 regular rows, more swirled and broken"))
		float Warp = 0.35f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", Tooltip = "Another seed, other clouds of the same kind: the same seed at the same world time gives the same sky"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Wind Direction", DragSpeed = 0.5, Tooltip = "Where the wind blows to, in degrees round the up axis from +X towards -Z; the streaks lie along it, the rows of cloudlets across it"))
		float WindDirectionDegrees = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Wind Speed", DragSpeed = 0.1, Tooltip = "How fast the clouds move, in metres a second of the world's time, 0 to 200"))
		float WindSpeed = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.01, Tooltip = "What the clouds' light is multiplied by, 0 to 10"))
		float Brightness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.01, Tooltip = "How bright their thin edges glow with the sun behind them (a silver lining), 0 to 10"))
		float Rim = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.01, Tooltip = "How much the sky's own light lights them, on the side the sun doesn't reach and at night, 0 to 10"))
		float Ambient = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Lights The World", Tooltip = "Whether the clouds are in the sky's light on the world: white clouds brighten and whiten it, golden ones warm it, heavy cover softens it; off, the world is lit as without them"))
		bool bLightsWorld = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Light Strength", DragSpeed = 0.01, Tooltip = "How much of the clouds is in the sky's light on the world, 0 to 4: 1 as they look, 0 none"))
		float LightStrength = 1.0f;
	};
}

#include "Generated/Runtime/CloudLayerComponent.reflect.h"
