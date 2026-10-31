#pragma once

// A world plan drawn as a map from above, a pixel a node: plains green, rough land brown, rifts
// black, sea blue, off the landmass white, darker where the ground is lower; rivers and lakes
// light blue; where tunnels are, dotted

#include "ImageFile.h"
#include "TerrainWorldPlan.h"

#include <algorithm>
#include <cstdint>

namespace Nyx::Tests
{
	inline RgbaImage DrawTerrainWorldPlanMap(const TerrainWorldPlan& plan)
	{
		RgbaImage image;
		image.Width = uint32_t(plan.Nodes);
		image.Height = uint32_t(plan.Nodes);
		image.Pixels.assign(size_t(image.Width) * image.Height * 4, 255);
		auto put = [&](int i, int j, glm::vec3 colour)
		{
			if (i < 0 || j < 0 || i >= plan.Nodes || j >= plan.Nodes)
			{
				return;
			}
			const size_t at = (size_t(i) + size_t(j) * size_t(plan.Nodes)) * 4;
			image.Pixels[at] = uint8_t(std::clamp(colour.r, 0.0f, 1.0f) * 255.0f);
			image.Pixels[at + 1] = uint8_t(std::clamp(colour.g, 0.0f, 1.0f) * 255.0f);
			image.Pixels[at + 2] = uint8_t(std::clamp(colour.b, 0.0f, 1.0f) * 255.0f);
		};
		const SdfTerrainWorldPlan& made = plan.Made;
		for (int j = 0; j < plan.Nodes; ++j)
		{
			for (int i = 0; i < plan.Nodes; ++i)
			{
				const size_t index = plan.IndexOf(i, j);
				const uint8_t landform = plan.Landform[index];
				if (landform == TerrainWorldPlan::NotLand)
				{
					continue;
				}
				glm::vec3 colour(0.35f, 0.62f, 0.25f);
				switch (ESdfTerrainLandform(landform))
				{
				case ESdfTerrainLandform::Plains:
					break;
				case ESdfTerrainLandform::Roughland:
					colour = glm::vec3(0.55f, 0.42f, 0.28f);
					break;
				case ESdfTerrainLandform::Rift:
					colour = glm::vec3(0.08f, 0.07f, 0.07f);
					break;
				case ESdfTerrainLandform::Sea:
					colour = glm::vec3(0.12f, 0.30f, 0.62f);
					break;
				case ESdfTerrainLandform::Tunnels:
					break;
				}
				const float lift = std::clamp((plan.Height[index] - (made.SeaLevel - made.SeaDepth)) / std::max(made.RoughLift + made.Height - made.SeaLevel + made.SeaDepth, 1.0f), 0.0f, 1.0f);
				colour *= 0.7f + 0.3f * lift;
				if (plan.Tunnels[index] > 0.5f && (i + j) % 4 == 0 && landform != uint8_t(ESdfTerrainLandform::Rift))
				{
					colour *= 0.55f;
				}
				put(i, j, colour);
			}
		}
		for (const TerrainWorldPlan::Lake& lake : plan.Lakes)
		{
			for (int j = 0; j < plan.Nodes; ++j)
			{
				for (int i = 0; i < plan.Nodes; ++i)
				{
					const size_t index = plan.IndexOf(i, j);
					if (plan.Landform[index] != TerrainWorldPlan::NotLand && plan.Height[index] < lake.Level - 0.5f
						&& glm::length(plan.PointOf(i, j) - lake.Center) < made.LakeRadius * 1.6f)
					{
						put(i, j, glm::vec3(0.35f, 0.65f, 0.95f));
					}
				}
			}
		}
		for (const TerrainWorldPlan::River& river : plan.Rivers)
		{
			for (size_t n = 0; n + 1 < river.Points.size(); ++n)
			{
				for (int step = 0; step <= 8; ++step)
				{
					const glm::ivec2 node = plan.NodeOf(river.Points[n] + (river.Points[n + 1] - river.Points[n]) * (float(step) / 8.0f));
					put(node.x, node.y, glm::vec3(0.35f, 0.65f, 0.95f));
				}
			}
		}
		return image;
	}
}
