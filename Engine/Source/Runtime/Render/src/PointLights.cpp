#include "NyxPCH.h"
#include "PointLights.h"

#include "PointLightComponent.h"
#include "PreviousTransformComponent.h"
#include "TransformComponent.h"

#include <algorithm>
#include <numeric>

namespace Nyx
{
	float GetPointLightFalloff(float distance, float range)
	{
		if (range <= 0.0f || distance >= range)
		{
			return 0.0f;
		}
		const float ratio = distance / range;
		const float ratio4 = ratio * ratio * ratio * ratio;
		const float window = (1.0f - ratio4) * (1.0f - ratio4);
		const float nearest = std::max(distance, MinPointLightDistance);
		return window / (nearest * nearest);
	}

	void ExtractPointLights(const Registry& world, std::optional<float> interpolation, std::vector<RenderPointLight>& outLights)
	{
		outLights.clear();
		world.Each<PointLightComponent>([&](Entity entity, const PointLightComponent& light)
			{
				const glm::vec3 color = glm::max(light.Color, glm::vec3(0.0f)) * light.Intensity;
				if (light.Range <= 0.0f || light.Intensity <= 0.0f || std::max(color.r, std::max(color.g, color.b)) <= 0.0f)
				{
					return;
				}
				RenderPointLight render;
				render.SourceEntity = entity;
				render.Position = GetDrawnTransform(world, entity, interpolation).Position;
				render.WorldPosition = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Position : glm::vec3(0.0f);
				render.Range = light.Range;
				render.Color = color;
				outLights.push_back(render);
			});
	}

	bool IsSphereVisible(const Frustum& frustum, const glm::vec3& center, float radius)
	{
		for (const glm::vec4& plane : frustum.Planes)
		{
			if (glm::dot(glm::vec3(plane), center) + plane.w < -radius)
			{
				return false;
			}
		}
		return true;
	}

	PointLightCullStats CullPointLights(std::span<const RenderPointLight> lights, const Frustum& frustum, const glm::vec3& camera, uint32_t maxLights,
		std::vector<ViewPointLight>& outLights)
	{
		outLights.clear();
		PointLightCullStats stats;
		stats.Lights = static_cast<uint32_t>(lights.size());

		std::vector<uint32_t> visible;
		for (uint32_t index = 0; index < lights.size(); ++index)
		{
			const RenderPointLight& light = lights[index];
			if (IsSphereVisible(frustum, light.Position, light.Range))
			{
				visible.push_back(index);
			}
		}
		stats.Culled = stats.Lights - static_cast<uint32_t>(visible.size());

		// Too many: those that reach nearest the camera, the camera inside a light's range first
		if (visible.size() > maxLights)
		{
			const auto reach = [&](uint32_t index)
			{
				return std::max(glm::distance(lights[index].Position, camera) - lights[index].Range, 0.0f);
			};
			std::stable_sort(visible.begin(), visible.end(), [&](uint32_t a, uint32_t b)
				{
					return reach(a) < reach(b);
				});
			stats.Dropped = static_cast<uint32_t>(visible.size()) - maxLights;
			visible.resize(maxLights);
			std::sort(visible.begin(), visible.end());
		}

		outLights.reserve(visible.size());
		for (const uint32_t index : visible)
		{
			const RenderPointLight& light = lights[index];
			outLights.push_back(ViewPointLight{ .Position = light.Position, .InverseRangeSquared = 1.0f / (light.Range * light.Range), .Color = light.Color });
		}
		return stats;
	}
}
