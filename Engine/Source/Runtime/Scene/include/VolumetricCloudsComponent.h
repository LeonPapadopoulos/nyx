#pragma once

#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>

namespace Nyx
{
	// A look the volumetric clouds' values can be set to at once (ApplyVolumetricCloudsPreset()).
	// Saved as its number: new presets go at the end.
	NYX_ENUM()
	enum class EVolumetricCloudsPreset : uint8_t
	{
		// The values as they are: choosing it changes none
		Custom,
		// A dense field of small, puffy, cauliflower-topped clouds, low in the band
		SmallPuffs,
		// Big cumulus with bright tops, shaded grey-violet bellies and wispy edges, small puffs
		// round them, open blue between
		Cumulus,
		// Broken cloud whose thin edges glow against a low sun, dark cores, light through the gaps
		Broken,
	};

	// What the volumetric clouds show instead of their light, to see how they are made
	NYX_ENUM()
	enum class EVolumetricCloudsDebug : uint8_t
	{
		// Their light
		Off,
		// The weather map where each ray enters the band: coverage red, type green, density blue
		WeatherMap,
		// The sun's light alone, scattered by them
		SunLight,
		// The sky's light alone, scattered by them
		SkyLight,
		// Which of their pixels were marched this frame (red), which came from the frame before
		// (grey) and which had nothing there to come from (blue)
		Updates,
	};

	// Clouds with volume: a band between two altitudes round the planet's curve, marched along each
	// pixel's ray, as every view of the world draws them with the sky made by the air's scattering
	// (EnvironmentComponent's Atmosphere mode). A weather map, tiling and carried by the wind with the
	// world's time, places them: their coverage, type and density, so the sky has patches, bands and
	// open blue. Their shapes come from 3D noise made from the seed (billowy, rounded tops, wispy
	// bases), their height by their type: flat stratus, small puffs, towering cumulus. Lit by the sun
	// through the air (gold at sunset), through themselves (Beer's law, the powder of their edges),
	// scattered many times, so their bellies aren't black, with a silver lining against the sun, and
	// by the sky above and the ground below; fading into the haze far off. The world's first such
	// component counts; without one, or with the sky in another mode, there are none and nothing
	// changes. A high Cloud Layer can stay above them for thin cirrus.
	NYX_REFLECT(Component, meta = (DisplayName = "Volumetric Clouds"))
	struct VolumetricCloudsComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Clouds", Tooltip = "Whether the clouds are drawn (with the sky's Atmosphere mode only)"))
		bool bClouds = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", Tooltip = "Sets the clouds' and their lighting's values to a look: small puffs, cumulus, broken cloud against a low sun. The values can be changed after; Custom leaves them as they are"))
		EVolumetricCloudsPreset Preset = EVolumetricCloudsPreset::Custom;

		// The preset the values were last set to: a preset chosen that isn't it sets them
		NYX_PROPERTY(Serialize, Hidden)
		EVolumetricCloudsPreset AppliedPreset = EVolumetricCloudsPreset::Custom;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Bottom Altitude", DragSpeed = 10.0, Tooltip = "Where the clouds' band starts above the ground, in metres, 0 to 20 000: their flat bases"))
		float BottomAltitude = 1500.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Top Altitude", DragSpeed = 10.0, Tooltip = "Where the band ends, in metres, at least 100 above its bottom: the tallest clouds' tops"))
		float TopAltitude = 5000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How much of the sky the clouds cover, 0 to 1: 0 clear, 1 overcast"))
		float Coverage = 0.45f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Coverage Variation", DragSpeed = 0.005, Tooltip = "How much the weather map moves the coverage up and down, 0 to 1: 0 an even field, 1 patches and bands of cloud with open blue between"))
		float CoverageVariation = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "The clouds' kind, by how tall they grow, 0 to 1: 0 flat stratus, 0.5 small puffs, 1 towering cumulus"))
		float Type = 0.75f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Type Variation", DragSpeed = 0.005, Tooltip = "How much the weather map moves the type, 0 to 1: tall clouds here, small puffs there"))
		float TypeVariation = 0.4f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.01, Tooltip = "How dense the clouds are, 0 to 10: denser clouds are more opaque, with darker cores and sharper edges"))
		float Density = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Cloud Size", DragSpeed = 10.0, Tooltip = "How wide the clouds' shapes are, in metres, 100 to 50 000: the 3D noise they are made from repeats over four times this"))
		float CloudSize = 2500.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Weather Size", DragSpeed = 100.0, Tooltip = "How wide the weather map's patches are, in metres, 1000 to 500 000: the map repeats over four times this"))
		float WeatherSize = 12000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Detail Size", DragSpeed = 1.0, Tooltip = "How wide the small billows and wisps that wear the shapes' edges away are, in metres, 10 to 5000"))
		float DetailSize = 350.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How much the small billows and wisps wear the shapes' edges away, 0 to 1"))
		float Detail = 0.6f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DragSpeed = 0.005, Tooltip = "How round and billowy the clouds' tops are worn, against wispy, 0 to 1: their bases stay wispy"))
		float Billowiness = 0.8f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", DisplayName = "Edge Softness", DragSpeed = 0.005, Tooltip = "How softly the clouds thin out at their edges, 0.01 to 1: low hard and crisp, high hazy"))
		float EdgeSoftness = 0.15f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Clouds", Tooltip = "Another seed, other clouds of the same kind: the same seed at the same world time gives the same sky"))
		uint32_t Seed = 1;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Wind Direction", DragSpeed = 0.5, Tooltip = "Where the wind blows to, in degrees round the up axis from +X towards -Z"))
		float WindDirectionDegrees = 30.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Wind", DisplayName = "Wind Speed", DragSpeed = 0.1, Tooltip = "How fast the clouds move, in metres a second of the world's time, 0 to 200"))
		float WindSpeed = 10.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.01, Tooltip = "What the clouds' light is multiplied by, 0 to 10"))
		float Brightness = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Silver Lining", DragSpeed = 0.01, Tooltip = "How brightly the clouds' thin edges glow with the sun behind them, 0 to 10: their forward scattering"))
		float SilverLining = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Silver Lining Spread", DragSpeed = 0.005, Tooltip = "How far round the sun the silver lining reaches, 0 to 1: low a tight glow next to the sun, high wide"))
		float SilverLiningSpread = 0.4f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.005, Tooltip = "How much darker the clouds' sunlit edges and creases are than their insides with the sun behind the camera, 0 to 1 (the powder of their surface)"))
		float Powder = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Multiple Scattering", DragSpeed = 0.005, Tooltip = "How much of the light scattered more than once reaches the clouds' insides, 0 to 1: higher, brighter and softer bellies; 0 the light scattered once alone, dark"))
		float MultipleScattering = 0.5f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DragSpeed = 0.01, Tooltip = "How much the sky above and the ground below light the clouds, 0 to 10: their shaded sides and bellies, and at night all of them"))
		float Ambient = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Lighting", DisplayName = "Shadow Tint", UI = Color, Tooltip = "The colour of the light deep in the clouds, where the sun reaches only by scattering: grey-violet bellies"))
		glm::vec3 ShadowTint{ 0.85f, 0.85f, 0.95f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Quality", DisplayName = "Steps", Tooltip = "How many samples each ray takes through the band, 16 to 512: more are smoother and cost more"))
		uint32_t Steps = 128;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Quality", DisplayName = "Light Steps", Tooltip = "How many samples towards the sun each sample in a cloud takes for its shadow, 1 to 16"))
		uint32_t LightSteps = 6;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Quality", DisplayName = "Max Distance", DragSpeed = 100.0, Tooltip = "How far from the camera the clouds are marched, in metres, 1000 to 500 000: beyond it, none"))
		float MaxDistance = 150000.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Quality", Tooltip = "Whether each frame marches one pixel of every 4 x 4 and carries the others from the frames before (16 times cheaper); off, every pixel every frame"))
		bool bTemporal = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Quality", DisplayName = "Debug View", Tooltip = "What the clouds show instead of their light: the weather map, the sun's or the sky's light alone, which pixels were marched this frame"))
		EVolumetricCloudsDebug DebugView = EVolumetricCloudsDebug::Off;
	};

	// Sets the clouds' and their lighting's values to the preset chosen, when it isn't the one they
	// were last set to (AppliedPreset); Custom sets nothing. Run after the component is loaded or
	// edited (its post-load step), so choosing a preset in the Details panel, in a scene or in a
	// running game sets the values once, and they can be changed after.
	void ApplyVolumetricCloudsPreset(VolumetricCloudsComponent& clouds);

	// The component with a preset's values, the rest at their defaults
	VolumetricCloudsComponent MakeVolumetricCloudsPreset(EVolumetricCloudsPreset preset);
}

#include "Generated/Runtime/VolumetricCloudsComponent.reflect.h"
