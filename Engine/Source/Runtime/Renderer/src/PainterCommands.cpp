#include "PainterCommands.h"

#include <algorithm>
#include <cmath>

namespace Nyx::Engine
{
	PainterRect Intersect(const PainterRect& a, const PainterRect& b)
	{
		return PainterRect{ glm::max(a.Min, b.Min), glm::min(a.Max, b.Max) };
	}

	PainterRect Union(const PainterRect& a, const PainterRect& b)
	{
		return PainterRect{ glm::min(a.Min, b.Min), glm::max(a.Max, b.Max) };
	}

	namespace
	{
		uint32_t ToByte(float channel)
		{
			return static_cast<uint32_t>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
		}

		float FromByte(uint32_t packedColor, int byteIndex)
		{
			return static_cast<float>((packedColor >> (byteIndex * 8)) & 0xFFu) / 255.0f;
		}

		PainterRect AroundPoint(glm::vec2 center, float reach)
		{
			return PainterRect{ center - glm::vec2(reach), center + glm::vec2(reach) };
		}

		PainterRect Grow(const PainterRect& rect, float amount)
		{
			return PainterRect{ rect.Min - glm::vec2(amount), rect.Max + glm::vec2(amount) };
		}
	}

	uint32_t PackPremultipliedColor(const glm::vec4& straightColor)
	{
		const float alpha = std::clamp(straightColor.a, 0.0f, 1.0f);
		return ToByte(straightColor.r * alpha) | (ToByte(straightColor.g * alpha) << 8) |
			(ToByte(straightColor.b * alpha) << 16) | (ToByte(alpha) << 24);
	}

	glm::vec4 UnpackPremultipliedColor(uint32_t packedColor)
	{
		return glm::vec4(FromByte(packedColor, 0), FromByte(packedColor, 1), FromByte(packedColor, 2), FromByte(packedColor, 3));
	}

	PainterRect GetShapeBounds(const PainterCommand& command)
	{
		// An outline is centred on the edge, so half of it lies outside the shape.
		const float halfOutline = command.OutlineWidth * 0.5f;

		switch (static_cast<EPainterCommandType>(command.Type))
		{
		case EPainterCommandType::Rect:
		{
			const auto payload = GetPayload<PainterRectPayload>(command);
			return Grow(payload.Rect, halfOutline);
		}
		case EPainterCommandType::Circle:
		{
			const auto payload = GetPayload<PainterCirclePayload>(command);
			return AroundPoint(payload.Center, payload.Radius + halfOutline);
		}
		case EPainterCommandType::CircleSector:
		{
			// The whole circle, even for a small sector: simple, and binning only needs bounds
			// that are never too small.
			const auto payload = GetPayload<PainterCircleSectorPayload>(command);
			return AroundPoint(payload.Center, payload.OuterRadius + halfOutline);
		}
		case EPainterCommandType::Line:
		{
			// Half the width in every direction covers any angle, and round caps too, since they
			// reach exactly half the width past the end points.
			const auto payload = GetPayload<PainterLinePayload>(command);
			const PainterRect ends{ glm::min(payload.From, payload.To), glm::max(payload.From, payload.To) };
			return Grow(ends, payload.Width * 0.5f);
		}
		case EPainterCommandType::Triangle:
		{
			const auto payload = GetPayload<PainterTrianglePayload>(command);
			const PainterRect corners{ glm::min(payload.A, glm::min(payload.B, payload.C)), glm::max(payload.A, glm::max(payload.B, payload.C)) };
			return Grow(corners, halfOutline);
		}
		case EPainterCommandType::Glyph:
		case EPainterCommandType::Image:
			// Nothing records these before change 4 of the plan.
			break;
		}
		return PainterRect{};
	}
}
