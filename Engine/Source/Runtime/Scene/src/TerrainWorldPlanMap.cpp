#include "NyxPCH.h"
#include "TerrainWorldPlanMap.h"

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		glm::vec3 KindColour(ESdfTerrainLandform landform)
		{
			switch (landform)
			{
			case ESdfTerrainLandform::Roughland:
				return glm::vec3(0.55f, 0.42f, 0.28f);
			case ESdfTerrainLandform::Rift:
				return glm::vec3(0.08f, 0.07f, 0.07f);
			case ESdfTerrainLandform::Sea:
				return glm::vec3(0.12f, 0.30f, 0.62f);
			default:
				return glm::vec3(0.35f, 0.62f, 0.25f);
			}
		}

		// The plan's ground's slope at a node, in degrees, from its neighbours
		float SlopeAt(const TerrainWorldPlan& plan, int i, int j)
		{
			auto height = [&](int x, int z) { return plan.Height[plan.IndexOf(std::clamp(x, 0, plan.Nodes - 1), std::clamp(z, 0, plan.Nodes - 1))]; };
			const float dx = (height(i + 1, j) - height(i - 1, j)) / (2.0f * TerrainWorldPlan::CellSize);
			const float dz = (height(i, j + 1) - height(i, j - 1)) / (2.0f * TerrainWorldPlan::CellSize);
			return glm::degrees(std::atan(std::sqrt(dx * dx + dz * dz)));
		}
	}

	TerrainPlanMapImage DrawTerrainWorldPlanMap(const TerrainWorldPlan& plan, ETerrainPlanMapOverlay overlay)
	{
		TerrainPlanMapImage image;
		image.Width = uint32_t(plan.Nodes);
		image.Height = uint32_t(plan.Nodes);
		image.Pixels.assign(size_t(image.Width) * image.Height * 4, 0);
		auto put = [&](int i, int j, const glm::vec3& colour)
		{
			if (i < 0 || j < 0 || i >= plan.Nodes || j >= plan.Nodes)
			{
				return;
			}
			const size_t at = (size_t(i) + size_t(j) * size_t(plan.Nodes)) * 4;
			image.Pixels[at] = uint8_t(std::clamp(colour.r, 0.0f, 1.0f) * 255.0f);
			image.Pixels[at + 1] = uint8_t(std::clamp(colour.g, 0.0f, 1.0f) * 255.0f);
			image.Pixels[at + 2] = uint8_t(std::clamp(colour.b, 0.0f, 1.0f) * 255.0f);
			image.Pixels[at + 3] = 255;
		};

		// The ground's heights over the landmass, for the Height overlay
		float lowest = 1e30f;
		float highest = -1e30f;
		for (size_t index = 0; index < plan.Landform.size(); ++index)
		{
			if (plan.Landform[index] != TerrainWorldPlan::NotLand)
			{
				lowest = std::min(lowest, plan.Height[index]);
				highest = std::max(highest, plan.Height[index]);
			}
		}
		const float span = std::max(highest - lowest, 1.0f);

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
				glm::vec3 colour = KindColour(ESdfTerrainLandform(landform));
				switch (overlay)
				{
				case ETerrainPlanMapOverlay::Kinds:
					colour *= 0.7f + 0.3f * (plan.Height[index] - lowest) / span;
					break;
				case ETerrainPlanMapOverlay::Height:
					colour = glm::mix(colour * 0.25f, glm::vec3(0.95f), 0.15f + 0.85f * (plan.Height[index] - lowest) / span);
					break;
				case ETerrainPlanMapOverlay::Slope:
					colour = glm::mix(colour * 0.35f, glm::vec3(1.0f, 0.9f, 0.4f), std::clamp(SlopeAt(plan, i, j) / 45.0f, 0.0f, 1.0f));
					break;
				case ETerrainPlanMapOverlay::Tunnels:
					colour = glm::mix(colour * 0.6f, glm::vec3(0.61f, 0.42f, 0.82f), std::clamp(plan.Tunnels[index], 0.0f, 1.0f));
					break;
				}
				put(i, j, colour);
			}
		}

		const glm::vec3 water(0.35f, 0.65f, 0.95f);
		for (const TerrainWorldPlan::Lake& lake : plan.Lakes)
		{
			const glm::ivec2 low = plan.NodeOf(lake.Center - glm::vec2(plan.Made.LakeRadius * 1.6f));
			const glm::ivec2 high = plan.NodeOf(lake.Center + glm::vec2(plan.Made.LakeRadius * 1.6f));
			for (int j = low.y; j <= high.y; ++j)
			{
				for (int i = low.x; i <= high.x; ++i)
				{
					const size_t index = plan.IndexOf(i, j);
					if (plan.Landform[index] != TerrainWorldPlan::NotLand && plan.Height[index] < lake.Level - 0.5f
						&& glm::length(plan.PointOf(i, j) - lake.Center) < plan.Made.LakeRadius * 1.6f)
					{
						put(i, j, water);
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
					put(node.x, node.y, water);
				}
			}
		}
		return image;
	}

	TerrainPlanReadout ReadTerrainWorldPlan(const TerrainWorldPlan& plan, const glm::vec2& point)
	{
		TerrainPlanReadout readout;
		if (plan.Nodes == 0)
		{
			return readout;
		}
		const glm::ivec2 node = plan.NodeOf(point);
		const size_t index = plan.IndexOf(node.x, node.y);
		const glm::vec2 inside = (point - plan.Origin) / TerrainWorldPlan::CellSize;
		readout.bLand = plan.Landform[index] != TerrainWorldPlan::NotLand && inside.x >= -0.5f && inside.y >= -0.5f && inside.x <= float(plan.Nodes) - 0.5f &&
						inside.y <= float(plan.Nodes) - 0.5f;
		if (!readout.bLand)
		{
			return readout;
		}
		readout.Landform = ESdfTerrainLandform(plan.Landform[index]);
		readout.Height = plan.At(plan.Height, point.x, point.y);
		readout.SlopeDegrees = SlopeAt(plan, node.x, node.y);
		readout.Tunnels = plan.At(plan.Tunnels, point.x, point.y);
		readout.RiftDistance = plan.At(plan.Rift, point.x, point.y);
		return readout;
	}

	TerrainPlanShares GetTerrainWorldPlanShares(const TerrainWorldPlan& plan)
	{
		TerrainPlanShares shares;
		size_t land = 0;
		size_t sea = 0;
		size_t rough = 0;
		size_t rift = 0;
		size_t tunnels = 0;
		for (size_t index = 0; index < plan.Landform.size(); ++index)
		{
			if (plan.Landform[index] == TerrainWorldPlan::NotLand)
			{
				continue;
			}
			++land;
			sea += plan.Landform[index] == uint8_t(ESdfTerrainLandform::Sea) ? 1 : 0;
			rough += plan.Landform[index] == uint8_t(ESdfTerrainLandform::Roughland) ? 1 : 0;
			rift += plan.Landform[index] == uint8_t(ESdfTerrainLandform::Rift) ? 1 : 0;
			tunnels += plan.Tunnels[index] > 0.5f ? 1 : 0;
		}
		const float of = float(std::max<size_t>(land, 1));
		shares.Sea = float(sea) / of;
		shares.Roughland = float(rough) / of;
		shares.Rift = float(rift) / of;
		shares.Tunnels = float(tunnels) / of;
		shares.Rivers = plan.Rivers.size();
		shares.Lakes = plan.Lakes.size();
		return shares;
	}
}
