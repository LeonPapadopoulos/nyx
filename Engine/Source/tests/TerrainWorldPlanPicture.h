#pragma once

// A world plan drawn as a map from above for the tests' pictures (DrawTerrainWorldPlanMap() in
// TerrainWorldPlanMap.h), off the landmass white

#include "ImageFile.h"
#include "TerrainWorldPlanMap.h"

namespace Nyx::Tests
{
	inline RgbaImage DrawTerrainWorldPlanMap(const TerrainWorldPlan& plan, ETerrainPlanMapOverlay overlay = ETerrainPlanMapOverlay::Kinds)
	{
		const TerrainPlanMapImage map = Nyx::DrawTerrainWorldPlanMap(plan, overlay);
		RgbaImage image;
		image.Width = map.Width;
		image.Height = map.Height;
		image.Pixels = map.Pixels;
		for (size_t at = 0; at < image.Pixels.size(); at += 4)
		{
			if (image.Pixels[at + 3] == 0)
			{
				image.Pixels[at] = image.Pixels[at + 1] = image.Pixels[at + 2] = image.Pixels[at + 3] = 255;
			}
		}
		return image;
	}
}
