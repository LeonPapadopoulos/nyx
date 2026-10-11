#include "Painter.h"

#include <algorithm>
#include <utility>

namespace Nyx::Engine
{
	namespace
	{
		// A rect given the other way round (Max left of or above Min) means the same area.
		PainterRect Normalized(const PainterRect& rect)
		{
			return PainterRect{ glm::min(rect.Min, rect.Max), glm::max(rect.Min, rect.Max) };
		}
	}

	void Painter::BeginFrame(uint32_t targetWidth, uint32_t targetHeight)
	{
		TargetWidth = targetWidth;
		TargetHeight = targetHeight;

		Commands.clear();
		Groups.clear();
		Problems.clear();

		ClipStack.assign(1, PainterRect{ glm::vec2(0.0f), glm::vec2(static_cast<float>(targetWidth), static_cast<float>(targetHeight)) });
		TransformStack.assign(1, PainterTransform{});

		OpenGroup = PainterNoGroup;
		GroupDepth = 0;
	}

	void Painter::EndFrame()
	{
		if (GroupDepth > 0)
		{
			ReportProblem("BeginGroup() without an EndGroup() by the end of the frame");
			OpenGroup = PainterNoGroup;
			GroupDepth = 0;
		}
		if (ClipStack.size() > 1)
		{
			ReportProblem("PushClipRect() without a PopClipRect() by the end of the frame");
			ClipStack.resize(1);
		}
		if (TransformStack.size() > 1)
		{
			ReportProblem("PushTransform() without a PopTransform() by the end of the frame");
			TransformStack.resize(1);
		}
	}

	void Painter::AddRect(const PainterRect& rect, const PainterStyle& style)
	{
		AddRoundedRect(rect, PainterCornerRadii{}, style);
	}

	void Painter::AddRoundedRect(const PainterRect& rect, float radius, const PainterStyle& style)
	{
		AddRoundedRect(rect, PainterCornerRadii{ radius, radius, radius, radius }, style);
	}

	void Painter::AddRoundedRect(const PainterRect& rect, const PainterCornerRadii& radii, const PainterStyle& style)
	{
		const PainterTransform& transform = GetTransform();

		PainterRectPayload payload;
		payload.Rect = transform.Apply(Normalized(rect));

		// A radius larger than half the shorter side would make corners overlap, which the
		// distance function doesn't handle; such a rect becomes a pill or a circle instead.
		const float largestRadius = 0.5f * std::min(payload.Rect.GetWidth(), payload.Rect.GetHeight());
		const auto fit = [&](float radius)
		{
			return std::clamp(radius * transform.Scale, 0.0f, largestRadius);
		};
		payload.Radii = PainterCornerRadii{ fit(radii.TopLeft), fit(radii.TopRight), fit(radii.BottomRight), fit(radii.BottomLeft) };

		AddShape(EPainterCommandType::Rect, EPainterCommandFlags::None, style, payload);
	}

	void Painter::AddCircle(glm::vec2 center, float radius, const PainterStyle& style)
	{
		const PainterTransform& transform = GetTransform();

		PainterCirclePayload payload;
		payload.Center = transform.Apply(center);
		payload.Radius = std::max(radius, 0.0f) * transform.Scale;

		AddShape(EPainterCommandType::Circle, EPainterCommandFlags::None, style, payload);
	}

	void Painter::AddCircleSector(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, const PainterStyle& style)
	{
		const PainterTransform& transform = GetTransform();

		// Given the other way round, the radii and the angles still mean the same sector.
		if (innerRadius > outerRadius)
		{
			std::swap(innerRadius, outerRadius);
		}
		if (startAngle > endAngle)
		{
			std::swap(startAngle, endAngle);
		}

		// The transform has no rotation, so the angles stay as they are.
		PainterCircleSectorPayload payload;
		payload.Center = transform.Apply(center);
		payload.InnerRadius = std::max(innerRadius, 0.0f) * transform.Scale;
		payload.OuterRadius = std::max(outerRadius, 0.0f) * transform.Scale;
		payload.StartAngle = startAngle;
		payload.EndAngle = endAngle;

		AddShape(EPainterCommandType::CircleSector, EPainterCommandFlags::None, style, payload);
	}

	void Painter::AddArc(glm::vec2 center, float radius, float width, float startAngle, float endAngle, const PainterStyle& style)
	{
		const float halfWidth = 0.5f * std::max(width, 0.0f);
		AddCircleSector(center, std::max(radius - halfWidth, 0.0f), radius + halfWidth, startAngle, endAngle, style);
	}

	void Painter::AddLine(glm::vec2 from, glm::vec2 to, float width, EPainterLineCap cap, const PainterStyle& style)
	{
		const PainterTransform& transform = GetTransform();

		PainterLinePayload payload;
		payload.From = transform.Apply(from);
		payload.To = transform.Apply(to);
		payload.Width = std::max(width, 0.0f) * transform.Scale;

		// A line's width is its own; an outline width would mean nothing.
		PainterStyle lineStyle = style;
		lineStyle.OutlineWidth = 0.0f;

		const EPainterCommandFlags flags = cap == EPainterLineCap::Round ? EPainterCommandFlags::RoundCaps : EPainterCommandFlags::None;
		AddShape(EPainterCommandType::Line, flags, lineStyle, payload);
	}

	void Painter::AddTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const PainterStyle& style)
	{
		const PainterTransform& transform = GetTransform();

		PainterTrianglePayload payload;
		payload.A = transform.Apply(a);
		payload.B = transform.Apply(b);
		payload.C = transform.Apply(c);

		AddShape(EPainterCommandType::Triangle, EPainterCommandFlags::None, style, payload);
	}

	void Painter::PushClipRect(const PainterRect& rect)
	{
		const PainterRect inTarget = GetTransform().Apply(Normalized(rect));
		ClipStack.push_back(Intersect(inTarget, GetClipRect()));
	}

	void Painter::PopClipRect()
	{
		if (ClipStack.size() <= 1)
		{
			ReportProblem("PopClipRect() without a PushClipRect()");
			return;
		}
		ClipStack.pop_back();
	}

	void Painter::PushTransform(float scale, glm::vec2 offset)
	{
		const PainterTransform& outer = GetTransform();

		if (scale <= 0.0f)
		{
			// A scale of 0 or less would flip or collapse every shape. The outer transform is
			// pushed again, so the matching PopTransform() still pops one.
			ReportProblem("PushTransform() with a scale of 0 or less; it is ignored");
			TransformStack.push_back(outer);
			return;
		}

		// The new transform applies first, then the outer one:
		// outer(p * scale + offset) = p * (outer.Scale * scale) + (outer.Scale * offset + outer.Offset)
		TransformStack.push_back(PainterTransform{ outer.Scale * scale, outer.Apply(offset) });
	}

	void Painter::PopTransform()
	{
		if (TransformStack.size() <= 1)
		{
			ReportProblem("PopTransform() without a PushTransform()");
			return;
		}
		TransformStack.pop_back();
	}

	void Painter::BeginGroup(const PainterEffects& effects)
	{
		++GroupDepth;
		if (GroupDepth > 1)
		{
			ReportProblem("BeginGroup() inside a group: groups don't nest, so the inner one is ignored");
			return;
		}
		OpenGroup = AddGroup(effects);
	}

	void Painter::EndGroup()
	{
		if (GroupDepth == 0)
		{
			ReportProblem("EndGroup() without a BeginGroup()");
			return;
		}
		--GroupDepth;
		if (GroupDepth == 0)
		{
			OpenGroup = PainterNoGroup;
		}
	}

	template<typename TPayload>
	void Painter::AddShape(EPainterCommandType type, EPainterCommandFlags flags, const PainterStyle& style, const TPayload& payload)
	{
		PainterCommand command;
		command.Type = static_cast<uint32_t>(type);
		command.Flags = static_cast<uint32_t>(flags);
		command.Color = PackPremultipliedColor(style.Color);
		command.ClipRect = GetClipRect();
		command.OutlineWidth = std::max(style.OutlineWidth, 0.0f) * GetTransform().Scale;
		command.FillMode = static_cast<uint32_t>(EPainterFillMode::Solid);

		if (GroupDepth > 0)
		{
			command.Group = OpenGroup;
			if (style.Effects.HasAny())
			{
				ReportProblem("A shape inside BeginGroup()/EndGroup() has its own shadow or glow; the group's apply instead");
			}
		}
		else if (style.Effects.HasAny())
		{
			// Shadow and glow belong to groups, so the shader handles them in one place. A shape
			// with effects of its own is a group of one.
			command.Group = AddGroup(style.Effects);
		}

		SetPayload(command, payload);
		Commands.push_back(command);
	}

	uint32_t Painter::AddGroup(const PainterEffects& effects)
	{
		const float scale = GetTransform().Scale;

		PainterGroup group;
		if (effects.HasShadow())
		{
			group.ShadowColor = PackPremultipliedColor(effects.Shadow.Color);
			// An offset is a distance, not a position, so it scales but doesn't move.
			group.ShadowOffset = effects.Shadow.Offset * scale;
			group.ShadowBlur = std::max(effects.Shadow.Blur, 0.0f) * scale;
		}
		if (effects.HasGlow())
		{
			group.GlowColor = PackPremultipliedColor(effects.Glow.Color);
			group.GlowDistance = effects.Glow.Distance * scale;
			group.GlowStrength = std::clamp(effects.Glow.Strength, 0.0f, 1.0f);
		}

		Groups.push_back(group);
		return static_cast<uint32_t>(Groups.size() - 1);
	}

	void Painter::ReportProblem(const std::string& problem)
	{
		if (std::find(Problems.begin(), Problems.end(), problem) == Problems.end())
		{
			Problems.push_back(problem);
		}
	}
}
