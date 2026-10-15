#include "Painter.h"

#include <imgui_internal.h> // ImTextCharFromUtf8()

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace Nyx::UI
{
	namespace
	{
		// A rect given the other way round (Max left of or above Min) means the same area.
		PainterRect Normalized(const PainterRect& rect)
		{
			return PainterRect{ glm::min(rect.Min, rect.Max), glm::max(rect.Min, rect.Max) };
		}

		bool IsSameTexture(const ImTextureRef& a, const ImTextureRef& b)
		{
			return a._TexData == b._TexData && a._TexID == b._TexID;
		}

		// The command types whose PainterCommand::Texture indexes the frame's textures
		bool UsesTexture(EPainterCommandType type)
		{
			return type == EPainterCommandType::Glyph || type == EPainterCommandType::Image || type == EPainterCommandType::ImageGlow ||
				type == EPainterCommandType::Glass;
		}
	}

	void Painter::BeginFrame(uint32_t targetWidth, uint32_t targetHeight)
	{
		TargetWidth = targetWidth;
		TargetHeight = targetHeight;

		Commands.clear();
		Groups.clear();
		Textures.clear();
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

	void Painter::AddText(ImFont* font, float size, glm::vec2 position, const glm::vec4& color, std::string_view text)
	{
		if (font == nullptr)
		{
			ReportProblem("AddText() without a font");
			return;
		}
		if (size <= 0.0f || text.empty())
		{
			return;
		}

		// Baked at the size the text ends up on the target, so zoomed text is as sharp as any.
		const PainterTransform& transform = GetTransform();
		const float sizeOnTarget = size * transform.Scale;
		ImFontBaked* baked = font->GetFontBaked(sizeOnTarget);
		uint32_t texture = 0;
		if (baked == nullptr || !FindOrAddTexture(font->OwnerAtlas->TexRef, texture))
		{
			return;
		}

		// ImGui bakes at a rounded size; the glyphs are scaled from it to the size asked for.
		const float glyphScale = sizeOnTarget / baked->Size;

		// Whole pixels, as ImGui does, so that the glyphs' texels land on pixels one to one.
		const glm::vec2 lineStart = glm::floor(transform.Apply(position));
		glm::vec2 pen = lineStart;

		const PainterStyle glyphStyle = PainterStyle::Filled(color);
		const char* cursor = text.data();
		const char* const end = text.data() + text.size();
		while (cursor < end)
		{
			unsigned int codepoint = 0;
			cursor += ImTextCharFromUtf8(&codepoint, cursor, end);

			if (codepoint == '\n')
			{
				pen = glm::vec2(lineStart.x, pen.y + sizeOnTarget);
				continue;
			}
			if (codepoint == '\r')
			{
				continue;
			}

			// FindGlyph() bakes a glyph that isn't in the atlas yet, and gives a replacement
			// glyph for a character the font doesn't have.
			const ImFontGlyph* glyph = baked->FindGlyph(static_cast<ImWchar>(codepoint));
			if (glyph == nullptr)
			{
				continue;
			}

			// Spaces and the like move the pen without drawing.
			if (glyph->Visible)
			{
				PainterTexturedRectPayload payload;
				payload.Rect = PainterRect{ pen + glm::vec2(glyph->X0, glyph->Y0) * glyphScale, pen + glm::vec2(glyph->X1, glyph->Y1) * glyphScale };
				payload.UVs = PainterRect{ { glyph->U0, glyph->V0 }, { glyph->U1, glyph->V1 } };
				AddShape(EPainterCommandType::Glyph, EPainterCommandFlags::None, glyphStyle, payload, texture);
			}
			pen.x += glyph->AdvanceX * glyphScale;
		}
	}

	void Painter::AddImage(ImTextureRef texture, const PainterRect& rect, const PainterStyle& style, const PainterRect& uvs)
	{
		uint32_t textureIndex = 0;
		if (!FindOrAddTexture(texture, textureIndex))
		{
			return;
		}

		PainterTexturedRectPayload payload;
		payload.Rect = GetTransform().Apply(Normalized(rect));
		payload.UVs = uvs;

		// An image fills its rect; an outline would mean nothing.
		PainterStyle imageStyle = style;
		imageStyle.OutlineWidth = 0.0f;

		AddShape(EPainterCommandType::Image, EPainterCommandFlags::None, imageStyle, payload, textureIndex);
	}

	void Painter::AddImageGlow(ImTextureRef texture, const PainterRect& rect, float reach, float strength, const PainterRect& uvs)
	{
		uint32_t textureIndex = 0;
		if (!FindOrAddTexture(texture, textureIndex))
		{
			return;
		}

		const PainterTransform& transform = GetTransform();
		PainterTexturedRectPayload payload;
		payload.Rect = transform.Apply(Normalized(rect));
		payload.UVs = uvs;

		// The colour tints the texture as an image's does: white keeps the texture's colours, and
		// its alpha is the strength.
		const PainterStyle glowStyle = PainterStyle::Filled(glm::vec4(1.0f, 1.0f, 1.0f, std::clamp(strength, 0.0f, 1.0f)));
		PainterCommand& command = AddShape(EPainterCommandType::ImageGlow, EPainterCommandFlags::None, glowStyle, payload, textureIndex);
		command.Reach = std::max(reach, 0.0f) * transform.Scale;
	}

	void Painter::AddGlass(ImTextureRef backdrop, const PainterRect& rect, float radius, const PainterStyle& style, const PainterGlass& glass)
	{
		uint32_t textureIndex = 0;
		if (!FindOrAddTexture(backdrop, textureIndex))
		{
			return;
		}

		const PainterTransform& transform = GetTransform();
		PainterGlassPayload payload;
		payload.Rect = transform.Apply(Normalized(rect));
		// As for rounded rects: no larger than half the shorter side
		payload.Radius = std::clamp(radius * transform.Scale, 0.0f, 0.5f * std::min(payload.Rect.GetWidth(), payload.Rect.GetHeight()));
		payload.Refraction = std::max(glass.Refraction, 0.0f) * transform.Scale;
		payload.EdgeWidth = std::max(glass.EdgeWidth, 0.0f) * transform.Scale;
		payload.RimStrength = std::clamp(glass.RimStrength, 0.0f, 1.0f);

		// Glass fills its shape; an outline would mean nothing.
		PainterStyle glassStyle = style;
		glassStyle.OutlineWidth = 0.0f;

		AddShape(EPainterCommandType::Glass, EPainterCommandFlags::None, glassStyle, payload, textureIndex);
	}

	void Painter::PushClipRect(const PainterRect& rect)
	{
		const PainterRect inTarget = GetTransform().Apply(Normalized(rect));
		ClipStack.push_back(Intersect(inTarget, GetClipRect()));
	}

	void Painter::PushTargetClipRect(const PainterRect& rect)
	{
		ClipStack.push_back(Intersect(Normalized(rect), GetClipRect()));
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

	bool Painter::AppendSegment(const Painter& source, const PainterSegment& segment)
	{
		const std::vector<PainterCommand>& sourceCommands = source.GetCommands();
		const size_t first = std::min<size_t>(segment.FirstCommand, sourceCommands.size());
		const size_t end = std::min<size_t>(first + segment.CommandCount, sourceCommands.size());

		// Which of the source's textures go where in this frame's: worked out before anything is
		// copied, so that a run without room changes nothing
		std::vector<uint32_t> textureIndices(source.GetTextures().size(), PainterMaxTextures);
		std::vector<ImTextureRef> textures = Textures;
		for (size_t index = first; index < end; ++index)
		{
			const PainterCommand& command = sourceCommands[index];
			if (!UsesTexture(static_cast<EPainterCommandType>(command.Type)) || command.Texture >= textureIndices.size() ||
				textureIndices[command.Texture] != PainterMaxTextures)
			{
				continue;
			}
			const ImTextureRef& texture = source.GetTextures()[command.Texture];
			const auto found = std::find_if(textures.begin(), textures.end(), [&](const ImTextureRef& other)
				{
					return IsSameTexture(other, texture);
				});
			if (found == textures.end() && textures.size() >= PainterMaxTextures)
			{
				return false;
			}
			textureIndices[command.Texture] = static_cast<uint32_t>(found != textures.end() ? found - textures.begin() : textures.size());
			if (found == textures.end())
			{
				textures.push_back(texture);
			}
		}
		Textures = std::move(textures);

		// Each group the run uses comes along once, in the order it is first used.
		std::vector<uint32_t> groupIndices(source.GetGroups().size(), PainterNoGroup);
		for (size_t index = first; index < end; ++index)
		{
			PainterCommand command = sourceCommands[index];
			if (command.Group != PainterNoGroup && command.Group < groupIndices.size())
			{
				if (groupIndices[command.Group] == PainterNoGroup)
				{
					groupIndices[command.Group] = static_cast<uint32_t>(Groups.size());
					Groups.push_back(source.GetGroups()[command.Group]);
				}
				command.Group = groupIndices[command.Group];
			}
			if (UsesTexture(static_cast<EPainterCommandType>(command.Type)) && command.Texture < textureIndices.size())
			{
				command.Texture = textureIndices[command.Texture];
			}
			Commands.push_back(command);
		}
		return true;
	}

	template<typename TPayload>
	PainterCommand& Painter::AddShape(EPainterCommandType type, EPainterCommandFlags flags, const PainterStyle& style, const TPayload& payload, uint32_t texture)
	{
		PainterCommand command;
		command.Type = static_cast<uint32_t>(type);
		command.Flags = static_cast<uint32_t>(flags);
		command.Color = PackPremultipliedColor(style.Color);
		command.ClipRect = GetClipRect();
		command.OutlineWidth = std::max(style.OutlineWidth, 0.0f) * GetTransform().Scale;
		command.FillMode = static_cast<uint32_t>(EPainterFillMode::Solid);
		command.Texture = texture;

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
		return Commands.emplace_back(command);
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

	bool Painter::FindOrAddTexture(const ImTextureRef& texture, uint32_t& outIndex)
	{
		for (uint32_t index = 0; index < Textures.size(); ++index)
		{
			if (IsSameTexture(Textures[index], texture))
			{
				outIndex = index;
				return true;
			}
		}

		if (Textures.size() >= PainterMaxTextures)
		{
			ReportProblem("More than " + std::to_string(PainterMaxTextures) + " textures in one frame; text and images with further textures are dropped");
			return false;
		}

		Textures.push_back(texture);
		outIndex = static_cast<uint32_t>(Textures.size() - 1);
		return true;
	}

	void Painter::ReportProblem(const std::string& problem)
	{
		if (std::find(Problems.begin(), Problems.end(), problem) == Problems.end())
		{
			Problems.push_back(problem);
		}
	}
}
