#include "NyxPCH.h"
#include "VolumetricCloudsComponent.h"

namespace Nyx
{
	namespace
	{
		// The clouds' and their lighting's values of a preset onto `clouds`; the rest (whether they
		// are drawn, the seed, the wind, the quality) stays
		void SetPresetValues(VolumetricCloudsComponent& clouds, EVolumetricCloudsPreset preset)
		{
			switch (preset)
			{
			case EVolumetricCloudsPreset::SmallPuffs:
				// Low, even and dense: many small cells, short, crisp and billowy, lit from above
				clouds.BottomAltitude = 1800.0f;
				clouds.TopAltitude = 4500.0f;
				clouds.Coverage = 0.5f;
				clouds.CoverageVariation = 0.3f;
				clouds.Type = 0.5f;
				clouds.TypeVariation = 0.15f;
				clouds.Density = 1.2f;
				clouds.CloudSize = 700.0f;
				clouds.WeatherSize = 8000.0f;
				clouds.DetailSize = 120.0f;
				clouds.Detail = 0.7f;
				clouds.Billowiness = 0.9f;
				clouds.EdgeSoftness = 0.12f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 1.0f;
				clouds.SilverLiningSpread = 0.4f;
				clouds.Powder = 0.6f;
				clouds.MultipleScattering = 0.5f;
				clouds.Ambient = 1.0f;
				clouds.ShadowTint = glm::vec3(0.9f, 0.9f, 0.85f);
				break;
			case EVolumetricCloudsPreset::Cumulus:
				// Tall and patchy: big heaps with room between, smaller puffs round them, violet bellies
				clouds.BottomAltitude = 1500.0f;
				clouds.TopAltitude = 6000.0f;
				clouds.Coverage = 0.5f;
				clouds.CoverageVariation = 0.7f;
				clouds.Type = 0.85f;
				clouds.TypeVariation = 0.5f;
				clouds.Density = 1.5f;
				clouds.CloudSize = 2500.0f;
				clouds.WeatherSize = 15000.0f;
				clouds.DetailSize = 180.0f;
				clouds.Detail = 0.85f;
				clouds.Billowiness = 0.8f;
				clouds.EdgeSoftness = 0.12f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 1.0f;
				clouds.SilverLiningSpread = 0.4f;
				clouds.Powder = 0.5f;
				clouds.MultipleScattering = 0.45f;
				clouds.Ambient = 1.0f;
				clouds.ShadowTint = glm::vec3(0.7f, 0.66f, 0.92f);
				break;
			case EVolumetricCloudsPreset::Broken:
				// A dense, broken layer: thick cores, thin worn edges that glow against the sun
				clouds.BottomAltitude = 1500.0f;
				clouds.TopAltitude = 4500.0f;
				clouds.Coverage = 0.48f;
				clouds.CoverageVariation = 0.6f;
				clouds.Type = 0.55f;
				clouds.TypeVariation = 0.3f;
				clouds.Density = 2.0f;
				clouds.CloudSize = 2000.0f;
				clouds.WeatherSize = 10000.0f;
				clouds.DetailSize = 200.0f;
				clouds.Detail = 0.75f;
				clouds.Billowiness = 0.7f;
				clouds.EdgeSoftness = 0.12f;
				clouds.Brightness = 1.0f;
				clouds.SilverLining = 2.5f;
				clouds.SilverLiningSpread = 0.3f;
				clouds.Powder = 0.4f;
				clouds.MultipleScattering = 0.45f;
				clouds.Ambient = 0.8f;
				clouds.ShadowTint = glm::vec3(0.75f, 0.7f, 0.85f);
				break;
			case EVolumetricCloudsPreset::Custom:
				break;
			}
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
