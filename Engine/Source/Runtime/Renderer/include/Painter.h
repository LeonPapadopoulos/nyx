#pragma once

#include "PainterCommands.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Engine
{
	// A shadow behind a shape, or behind all shapes of a group together
	struct PainterShadow
	{
		// Straight RGBA like PainterStyle::Color; fully transparent means no shadow
		glm::vec4 Color{ 0.0f };
		// Where the shadow sits relative to the shape, e.g. { 0, 4 } for a little below
		glm::vec2 Offset{ 0.0f };
		// How far the shadow fades out; 0 is a hard edge
		float Blur = 0.0f;
	};

	// Light around a shape, or around all shapes of a group together, e.g. for hover or focus
	struct PainterGlow
	{
		// Straight RGBA like PainterStyle::Color; fully transparent means no glow
		glm::vec4 Color{ 0.0f };
		// How far the glow reaches outside the shape
		float Distance = 0.0f;
		// The glow's opacity at the shape's edge, from 0 to 1, multiplied with Color's alpha
		float Strength = 1.0f;
	};

	struct PainterEffects
	{
		PainterShadow Shadow;
		PainterGlow Glow;

		bool HasShadow() const { return Shadow.Color.a > 0.0f; }
		bool HasGlow() const { return Glow.Color.a > 0.0f && Glow.Distance > 0.0f && Glow.Strength > 0.0f; }
		bool HasAny() const { return HasShadow() || HasGlow(); }
	};

	// How a shape is drawn
	struct PainterStyle
	{
		// Straight (not premultiplied) RGBA, each channel from 0 to 1, in the same sRGB-encoded
		// space as ImGui's colours, so the two match side by side.
		glm::vec4 Color{ 1.0f };

		// 0: filled. Otherwise only an outline this wide is drawn, centred on the shape's edge.
		// Lines ignore it; their width is their own.
		float OutlineWidth = 0.0f;

		// A shape with a shadow or glow becomes a primitive group of its own. Inside
		// BeginGroup()/EndGroup() the group's effects apply instead, and these are ignored.
		PainterEffects Effects;

		static PainterStyle Filled(const glm::vec4& color)
		{
			PainterStyle style;
			style.Color = color;
			return style;
		}

		static PainterStyle Outlined(const glm::vec4& color, float outlineWidth)
		{
			PainterStyle style;
			style.Color = color;
			style.OutlineWidth = outlineWidth;
			return style;
		}
	};

	enum class EPainterLineCap
	{
		Flat,  // The line ends at its end points
		Round, // A half circle past each end point
	};

	// Scale, then move: a point p ends up at p * Scale + Offset in the target. No rotation.
	struct PainterTransform
	{
		float Scale = 1.0f;
		glm::vec2 Offset{ 0.0f };

		glm::vec2 Apply(glm::vec2 point) const { return point * Scale + Offset; }
		PainterRect Apply(const PainterRect& rect) const { return PainterRect{ Apply(rect.Min), Apply(rect.Max) }; }
	};

	// Records 2D shapes as draw commands for the painter shader, which draws each one as a signed
	// distance field: edges stay exactly anti-aliased at any size, and shadows and glows are
	// cheap. One painter records one frame for one target (an offscreen image now; an ImGui
	// window later). The plan is in Design/ui-painter.md.
	//
	// A frame:
	//
	//     painter.BeginFrame(width, height);
	//     painter.AddRoundedRect({ { 10, 10 }, { 110, 40 } }, 6.0f, PainterStyle::Filled(orange));
	//     painter.BeginGroup(effects); // shapes that cast one shadow together
	//     ...
	//     painter.EndGroup();
	//     painter.EndFrame();
	//
	// Positions, sizes and widths are pixels of the target, origin top left, y pointing down,
	// unless a transform is pushed. Commands are drawn in the order they were added.
	//
	// The painter never logs or asserts. Misuse (an EndGroup() too many, a clip rect left pushed)
	// is collected in GetProblems(), once per kind and frame, for whoever draws the frame to show.
	// The frame is still drawn as well as possible.
	class Painter
	{
	public:
		// Starts a frame for a target of this size: drops the previous frame's commands, groups,
		// stacks and problems.
		void BeginFrame(uint32_t targetWidth, uint32_t targetHeight);

		// Closes groups, clip rects and transforms left open, and reports each kind as a problem.
		void EndFrame();

		// Shapes. Each records one command with the current clip rect and transform.

		void AddRect(const PainterRect& rect, const PainterStyle& style);
		void AddRoundedRect(const PainterRect& rect, float radius, const PainterStyle& style);
		void AddRoundedRect(const PainterRect& rect, const PainterCornerRadii& radii, const PainterStyle& style);
		void AddCircle(glm::vec2 center, float radius, const PainterStyle& style);

		// The part of a ring between two angles, in radians: 0 points right, and angles grow
		// clockwise on the screen. An inner radius of 0 makes a pie slice; an angle range of 2 pi
		// or more makes a full ring.
		void AddCircleSector(glm::vec2 center, float innerRadius, float outerRadius, float startAngle, float endAngle, const PainterStyle& style);

		// An arc: a ring sector this wide, centred on the radius. Angles as in AddCircleSector().
		void AddArc(glm::vec2 center, float radius, float width, float startAngle, float endAngle, const PainterStyle& style);

		void AddLine(glm::vec2 from, glm::vec2 to, float width, EPainterLineCap cap, const PainterStyle& style);
		void AddTriangle(glm::vec2 a, glm::vec2 b, glm::vec2 c, const PainterStyle& style);

		// Text in one of ImGui's fonts, UTF-8, with lines broken at '\n'. `position` is the top
		// left of the first line, and each line is `size` high. The glyphs come from ImGui's font
		// atlas, baked at exactly the size they are drawn at (the transform's scale included), so
		// the text looks like ImGui's own and stays crisp when a canvas zooms. Text casts no
		// shadow or glow. Needs an ImGui context whose font atlas is built (after the first
		// ImGui::NewFrame()).
		void AddText(ImFont* font, float size, glm::vec2 position, const glm::vec4& color, std::string_view text);

		// A texture, or the part of it given by `uvs` (texture coordinates from 0 to 1), stretched
		// over `rect`. `texture` is what ImGui::Image() takes. The style's colour tints the image
		// (white leaves it as it is) and its effects give it a shadow or glow around the rect;
		// its outline width is ignored. A frame uses at most PainterMaxTextures textures, the
		// font atlas included; an image with one more is dropped and reported.
		void AddImage(ImTextureRef texture, const PainterRect& rect, const PainterStyle& style, const PainterRect& uvs = PainterRect{ { 0.0f, 0.0f }, { 1.0f, 1.0f } });

		// Clip rects. A pushed rect is in the current transform's units and is cut by the clip
		// rect already pushed, so nested clips only ever shrink. The first clip rect is the
		// whole target.
		void PushClipRect(const PainterRect& rect);
		void PopClipRect();

		// A clip rect already in target pixels, whatever transform is pushed: for clip rects from
		// outside the painter's own units, like ImGui's (Nyx::UI::WindowPainter). Cut by the clip
		// rect already pushed, and popped with PopClipRect().
		void PushTargetClipRect(const PainterRect& rect);

		// Transforms, e.g. for a canvas that zooms and pans. Everything added while it is pushed
		// is scaled and moved: positions, sizes, widths, outlines, shadows, glows and clip rects.
		// A pushed transform applies inside the one already pushed.
		void PushTransform(float scale, glm::vec2 offset);
		void PopTransform();

		// Primitive groups: the shapes added between these share one shadow and one glow, as if
		// they were one shape. Groups don't nest.
		void BeginGroup(const PainterEffects& effects);
		void EndGroup();

		// The recorded frame, as the GPU reads it
		const std::vector<PainterCommand>& GetCommands() const { return Commands; }
		const std::vector<PainterGroup>& GetGroups() const { return Groups; }

		// The textures glyphs and images use; PainterCommand::Texture indexes into this. They are
		// ImGui texture references, resolved to the GPU texture only when the frame is drawn,
		// since ImGui may still update its font atlas until then.
		const std::vector<ImTextureRef>& GetTextures() const { return Textures; }

		// Misuse found while recording this frame, one line per kind, e.g. "PopClipRect() without
		// a PushClipRect()"
		const std::vector<std::string>& GetProblems() const { return Problems; }

		uint32_t GetTargetWidth() const { return TargetWidth; }
		uint32_t GetTargetHeight() const { return TargetHeight; }

		// The clip rect commands get now, in pixels of the target
		const PainterRect& GetClipRect() const { return ClipStack.back(); }
		const PainterTransform& GetTransform() const { return TransformStack.back(); }

	private:
		// Adds a command: the header from the style and the current state, then the payload,
		// which the Add...() functions have already moved into target pixels. `texture` is an
		// index into Textures, for glyphs and images.
		template<typename TPayload>
		void AddShape(EPainterCommandType type, EPainterCommandFlags flags, const PainterStyle& style, const TPayload& payload, uint32_t texture = 0);

		// A new group from effects in the current transform's units; returns its index
		uint32_t AddGroup(const PainterEffects& effects);

		// The texture's index in Textures, adding it if it is new; false if the frame has no
		// room left for another texture
		bool FindOrAddTexture(const ImTextureRef& texture, uint32_t& outIndex);

		void ReportProblem(const std::string& problem);

		uint32_t TargetWidth = 0;
		uint32_t TargetHeight = 0;

		std::vector<PainterCommand> Commands;
		std::vector<PainterGroup> Groups;
		std::vector<ImTextureRef> Textures;
		std::vector<std::string> Problems;

		// Never empty: the bottom entries are the whole target and no transform.
		std::vector<PainterRect> ClipStack{ PainterRect{} };
		std::vector<PainterTransform> TransformStack{ PainterTransform{} };

		// The open group's index, or PainterNoGroup
		uint32_t OpenGroup = PainterNoGroup;
		// How deep BeginGroup() calls are nested. Only the outermost opens a group; the others
		// are reported and their EndGroup() calls are matched without closing it.
		uint32_t GroupDepth = 0;
	};

	// A run of a painter's commands that is binned and drawn on its own, in order with whatever
	// else draws into the same target. Inside an ImGui window, the shapes painted between two
	// pieces of ImGui's own drawing are one segment (Nyx::UI::WindowPainter). Its commands still
	// index the painter's groups and textures.
	struct PainterSegment
	{
		uint32_t FirstCommand = 0;
		uint32_t CommandCount = 0;

		bool operator==(const PainterSegment&) const = default;
	};
}
