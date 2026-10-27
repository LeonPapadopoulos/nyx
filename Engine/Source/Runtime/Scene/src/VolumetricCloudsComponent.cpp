#include "NyxPCH.h"
#include "VolumetricCloudsComponent.h"

namespace Nyx
{
	namespace
	{
		// The clouds' and their lighting's values of a preset onto `clouds`, at a game's scale (bases
		// a few hundred metres up, tops under 2 km); the rest (whether they are drawn, the seed, the
		// wind, the quality) stays
		void SetPresetValues(VolumetricCloudsComponent& clouds, EVolumetricCloudsPreset preset)
		{
			switch (preset)
			{
			case EVolumetricCloudsPreset::SmallPuffs:
				// Low and even: a field of small separate cells, short, crisp and billowy, cream in the sun
				clouds.BottomAltitude = 500.0f;
				clouds.TopAltitude = 1500.0f;
				clouds.Coverage = 0.5f;
				clouds.CoverageVariation = 0.25f;
				clouds.Type = 0.5f;
				clouds.TypeVariation = 0.15f;
				clouds.Density = 2.0f;
				clouds.CloudSize = 200.0f;
				clouds.WeatherSize = 3000.0f;
				clouds.DetailSize = 40.0f;
				clouds.Detail = 0.8f;
				clouds.Billowiness = 0.9f;
				clouds.EdgeSoftness = 0.22f;
				clouds.Wispiness = 0.3f;
				clouds.Cellularity = 0.8f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 1.0f;
				clouds.SilverLiningSpread = 0.4f;
				clouds.Powder = 0.5f;
				clouds.MultipleScattering = 0.45f;
				clouds.Ambient = 0.7f;
				clouds.SunlitTint = glm::vec3(1.0f, 0.96f, 0.88f);
				clouds.ShadowTint = glm::vec3(0.78f, 0.84f, 0.76f);
				clouds.AerialPerspective = 2.0f;
				clouds.Translucency = 0.3f;
				break;
			case EVolumetricCloudsPreset::Cumulus:
				// Tall and patchy: big many-lobed heaps with room between, puffs round them, violet bellies
				clouds.BottomAltitude = 400.0f;
				clouds.TopAltitude = 1800.0f;
				clouds.Coverage = 0.5f;
				clouds.CoverageVariation = 0.7f;
				clouds.Type = 0.85f;
				clouds.TypeVariation = 0.5f;
				clouds.Density = 1.4f;
				clouds.CloudSize = 650.0f;
				clouds.WeatherSize = 5000.0f;
				clouds.DetailSize = 70.0f;
				clouds.Detail = 0.8f;
				clouds.Billowiness = 0.85f;
				clouds.EdgeSoftness = 0.22f;
				clouds.Wispiness = 0.4f;
				clouds.Cellularity = 0.2f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 1.0f;
				clouds.SilverLiningSpread = 0.4f;
				clouds.Powder = 0.5f;
				clouds.MultipleScattering = 0.5f;
				clouds.Ambient = 1.0f;
				clouds.SunlitTint = glm::vec3(1.0f, 0.96f, 0.88f);
				clouds.ShadowTint = glm::vec3(0.72f, 0.68f, 0.92f);
				clouds.AerialPerspective = 1.5f;
				clouds.Translucency = 0.5f;
				break;
			case EVolumetricCloudsPreset::Broken:
				// Many small torn fragments: thin, wispy, glowing against a low sun, warm undersides
				clouds.BottomAltitude = 400.0f;
				clouds.TopAltitude = 1600.0f;
				clouds.Coverage = 0.4f;
				clouds.CoverageVariation = 0.6f;
				clouds.Type = 0.55f;
				clouds.TypeVariation = 0.3f;
				clouds.Density = 1.5f;
				clouds.CloudSize = 260.0f;
				clouds.WeatherSize = 3000.0f;
				clouds.DetailSize = 40.0f;
				clouds.Detail = 0.95f;
				clouds.Billowiness = 0.6f;
				clouds.EdgeSoftness = 0.35f;
				clouds.Wispiness = 1.0f;
				clouds.Cellularity = 0.3f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 2.5f;
				clouds.SilverLiningSpread = 0.3f;
				clouds.Powder = 0.3f;
				clouds.MultipleScattering = 0.5f;
				clouds.Ambient = 0.8f;
				clouds.SunlitTint = glm::vec3(1.0f, 0.9f, 0.78f);
				clouds.ShadowTint = glm::vec3(0.85f, 0.66f, 0.62f);
				clouds.AerialPerspective = 1.5f;
				clouds.Translucency = 0.8f;
				break;
			case EVolumetricCloudsPreset::Custom:
				break;
			}
		}
	}

	void FoldLegacyVolumetricCloudsUpdate(VolumetricCloudsComponent& clouds)
	{
		if (!clouds.bTemporal)
		{
			clouds.Update = EVolumetricCloudsUpdate::EveryPixel;
			clouds.bTemporal = true;
		}
	}

	void ApplyVolumetricCloudsPreset(VolumetricCloudsComponent& clouds)
	{
		if (clouds.Preset == clouds.AppliedPreset)
		{
			return;
		}
		SetPresetValues(clouds, clouds.Preset);
		clouds.AppliedPreset = clouds.Preset;
	}

	VolumetricCloudsComponent MakeVolumetricCloudsPreset(EVolumetricCloudsPreset preset)
	{
		VolumetricCloudsComponent clouds;
		clouds.Preset = preset;
		ApplyVolumetricCloudsPreset(clouds);
		return clouds;
	}
}
