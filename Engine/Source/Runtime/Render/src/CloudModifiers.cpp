#include "NyxPCH.h"
#include "CloudModifiers.h"

#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>

namespace Nyx
{
	namespace
	{
		float Saturate(float value)
		{
			return std::clamp(value, 0.0f, 1.0f);
		}

		float SmoothStep(float edge0, float edge1, float x)
		{
			const float t = Saturate((x - edge0) / std::max(edge1 - edge0, 1e-6f));
			return t * t * (3.0f - 2.0f * t);
		}

		// The whole band, for a shape whose Top isn't above its Bottom
		constexpr float WholeBand = 1.0e6f;
	}

	float GetCloudModifierFade(const CloudModifier& modifier, float time)
	{
		const float elapsed = time - modifier.StartTime;
		if (elapsed < 0.0f)
		{
			return 0.0f;
		}
		const float in = modifier.FadeIn > 0.0f ? Saturate(elapsed / modifier.FadeIn) : 1.0f;
		if (modifier.Duration <= 0.0f)
		{
			return in;
		}
		const float past = elapsed - modifier.Duration;
		if (past <= 0.0f)
		{
			return in;
		}
		const float out = modifier.FadeOut > 0.0f ? Saturate(1.0f - past / modifier.FadeOut) : 0.0f;
		return in * out;
	}

	std::optional<RenderCloudModifier> MakeRenderCloudModifier(const CloudModifier& modifier, const glm::vec3& center, float time, Entity entity)
	{
		const float fade = GetCloudModifierFade(modifier, time);
		if (fade <= 0.0f || modifier.Radius <= 0.0f || modifier.Strength == 0.0f)
		{
			return std::nullopt;
		}
		RenderCloudModifier render;
		render.SourceEntity = entity;
		render.Shape = modifier.Shape;
		render.Center = glm::vec2(center.x, center.z);
		render.Height = center.y;
		render.Radius = modifier.Radius;
		render.Width = std::max(modifier.Width, 1.0f);
		const bool bBand = modifier.Top > modifier.Bottom;
		render.Bottom = bBand ? modifier.Bottom : -WholeBand;
		render.Top = bBand ? modifier.Top : WholeBand;
		switch (modifier.Shape)
		{
		case ECloudModifierShape::ClearColumn:
			render.Strength = Saturate(modifier.Strength) * fade;
			break;
		case ECloudModifierShape::ThickenRing:
			render.Strength = std::clamp(modifier.Strength, 0.0f, 4.0f) * fade;
			break;
		case ECloudModifierShape::Swirl:
			// Turned further the longer it goes on, as fast as asked at its radius; unwinding as it fades
			render.Strength = fade;
			render.Angle = modifier.Strength * glm::two_pi<float>() / 60.0f * std::max(time - modifier.StartTime, 0.0f) * fade;
			break;
		}
		return render;
	}

	void ExtractCloudModifiers(const Registry& world, float time, std::vector<RenderCloudModifier>& outModifiers)
	{
		world.Each<CloudModifierComponent>([&](Entity entity, const CloudModifierComponent& component)
			{
				if (!component.bEnabled)
				{
					return;
				}
				const glm::vec3 position = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Position : glm::vec3(0.0f);
				for (const CloudModifier& modifier : component.Shapes)
				{
					if (const std::optional<RenderCloudModifier> render = MakeRenderCloudModifier(modifier, position, time, entity))
					{
						outModifiers.push_back(*render);
					}
				}
			});
	}

	void PickViewCloudModifiers(std::span<const RenderCloudModifier> modifiers, const glm::vec2& camera, std::vector<RenderCloudModifier>& outModifiers)
	{
		outModifiers.assign(modifiers.begin(), modifiers.end());
		if (outModifiers.size() <= MaxCloudModifiers)
		{
			return;
		}
		const auto edge = [&](const RenderCloudModifier& modifier)
		{
			const float reach = modifier.Shape == ECloudModifierShape::Swirl ? modifier.Radius * 4.0f : modifier.Radius + modifier.Width;
			return std::max(glm::distance(modifier.Center, camera) - reach, 0.0f);
		};
		std::stable_sort(outModifiers.begin(), outModifiers.end(), [&](const RenderCloudModifier& a, const RenderCloudModifier& b)
			{
				return edge(a) < edge(b);
			});
		outModifiers.resize(MaxCloudModifiers);
	}

	CloudModifierEffect GetCloudModifierEffect(std::span<const RenderCloudModifier> modifiers, const glm::vec2& point, float height)
	{
		CloudModifierEffect effect;
		effect.Lookup = point;
		for (const RenderCloudModifier& modifier : modifiers)
		{
			const float edge = std::max(modifier.Width * 0.5f, 1e-3f);
			const glm::vec2 offset = effect.Lookup - modifier.Center;
			const float distance = glm::length(offset);
			// Beyond a column's edge, or three times a ring's half width from its middle, nothing
			if ((modifier.Shape == ECloudModifierShape::ClearColumn && distance >= modifier.Radius + edge) ||
				(modifier.Shape == ECloudModifierShape::ThickenRing && std::abs(distance - modifier.Radius) >= 3.0f * edge))
			{
				continue;
			}
			// Soft at its top and bottom over half its width
			const float vertical = Saturate((height - modifier.Bottom) / edge) * Saturate((modifier.Top - height) / edge);
			if (vertical <= 0.0f)
			{
				continue;
			}
			switch (modifier.Shape)
			{
			case ECloudModifierShape::ClearColumn:
			{
				const float clear = modifier.Strength * vertical * (1.0f - SmoothStep(modifier.Radius - edge, modifier.Radius + edge, distance));
				effect.Keep *= 1.0f - clear;
				break;
			}
			case ECloudModifierShape::ThickenRing:
			{
				const float across = (distance - modifier.Radius) / edge;
				effect.Add += modifier.Strength * vertical * std::exp(-across * across);
				break;
			}
			case ECloudModifierShape::Swirl:
			{
				// Turned as much as Angle within the radius, less as 1 / the distance squared beyond; looked
				// up turned back, so the clouds show turned forwards
				const float ratio = distance > modifier.Radius ? modifier.Radius / distance : 1.0f;
				const float angle = -modifier.Angle * vertical * ratio * ratio;
				const float cosine = std::cos(angle);
				const float sine = std::sin(angle);
				effect.Lookup = modifier.Center + glm::vec2(offset.x * cosine - offset.y * sine, offset.x * sine + offset.y * cosine);
				break;
			}
			}
		}
		return effect;
	}
}
