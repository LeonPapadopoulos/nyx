#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <cstring>

// The painter's draw commands, laid out exactly as the painter shader reads them. The painter
// (Painter.h) records them; the plan is in Design/ui-painter.md.
//
// How to add a primitive:
//  1. Add its type to EPainterCommandType, and a payload struct below that fits in 32 bytes.
//  2. Add an Add...() function to Painter that fills the payload (Painter.h and Painter.cpp).
//  3. Give it bounds in GetShapeBounds() (PainterCommands.cpp), so tile binning finds it.
//  4. Give it a distance function in the painter shader (Painter.frag, from change 3 of the plan).
//  5. Test it in NyxPainterTests, and draw it on the test sheet (from change 5).

namespace Nyx::Engine
{
	// The values are part of the layout the shader reads, so they are written out.
	enum class EPainterCommandType : uint32_t
	{
		Rect = 0,         // PainterRectPayload; sharp or rounded corners
		Circle = 1,       // PainterCirclePayload
		CircleSector = 2, // PainterCircleSectorPayload; also arcs and full rings
		Line = 3,         // PainterLinePayload
		Triangle = 4,     // PainterTrianglePayload

		// Reserved for change 4 of the plan (text and images); nothing records them yet.
		Glyph = 5,
		Image = 6,
	};

	// Bits of PainterCommand::Flags
	enum class EPainterCommandFlags : uint32_t
	{
		None = 0,
		// Lines: round ends that reach half the width past each end point. Without it, a line ends
		// flat at its end points.
		RoundCaps = 1u << 0,
	};

	// How a shape's inside is coloured. Only Solid exists yet; gradients are planned
	// (Design/editor-ui-architecture.md, 3.10.1).
	enum class EPainterFillMode : uint32_t
	{
		Solid = 0,
	};

	// A command that belongs to no primitive group: it has no shadow and no glow.
	constexpr uint32_t PainterNoGroup = 0xFFFFFFFFu;

	// An axis-aligned rectangle in pixels of the target, origin top left, y pointing down
	struct PainterRect
	{
		glm::vec2 Min{ 0.0f };
		glm::vec2 Max{ 0.0f };

		float GetWidth() const { return Max.x - Min.x; }
		float GetHeight() const { return Max.y - Min.y; }

		// True when nothing is inside, e.g. the intersection of two rects that don't overlap
		bool IsEmpty() const { return Max.x <= Min.x || Max.y <= Min.y; }

		bool operator==(const PainterRect&) const = default;
	};

	// The part both rects cover; empty if they don't overlap
	PainterRect Intersect(const PainterRect& a, const PainterRect& b);

	// The smallest rect that holds both
	PainterRect Union(const PainterRect& a, const PainterRect& b);

	// The rect with every side moved outwards by `amount` pixels
	PainterRect Grow(const PainterRect& rect, float amount);

	// The rect moved by `offset`, keeping its size
	PainterRect Move(const PainterRect& rect, glm::vec2 offset);

	// The radius of each corner of a rect, in pixels; 0 is a sharp corner
	struct PainterCornerRadii
	{
		float TopLeft = 0.0f;
		float TopRight = 0.0f;
		float BottomRight = 0.0f;
		float BottomLeft = 0.0f;

		bool operator==(const PainterCornerRadii&) const = default;
	};

	// One draw command: 80 bytes, five vec4s in the shader's storage buffer (std430).
	//
	//   bytes  0-15  Type, Flags, Color, Group                  (uints)
	//   bytes 16-31  ClipRect: min x, min y, max x, max y        (floats)
	//   bytes 32-47  OutlineWidth (float), FillMode, Texture, Unused (uints)
	//   bytes 48-79  Payload: 32 bytes, laid out by the payload struct of Type
	//
	// Every command has the same size so that the shader can find command i at i * 80 bytes, and
	// so that this table is the whole format. The static_asserts below keep it true.
	struct PainterCommand
	{
		uint32_t Type = 0;  // EPainterCommandType
		uint32_t Flags = 0; // EPainterCommandFlags bits
		// Premultiplied RGBA, 8 bits each, red in the lowest byte, as GLSL's unpackUnorm4x8() reads it
		uint32_t Color = 0;
		// Index into the painter's groups, or PainterNoGroup
		uint32_t Group = PainterNoGroup;

		// Pixels outside are never touched by this command, shadow and glow included
		PainterRect ClipRect;

		// 0: filled. Otherwise only an outline this wide is drawn, centred on the shape's edge.
		float OutlineWidth = 0.0f;
		uint32_t FillMode = 0; // EPainterFillMode
		uint32_t Texture = 0;  // Glyphs and images (change 4); unused by shapes
		uint32_t Unused = 0;

		// The shape itself; read and written with GetPayload() and SetPayload()
		std::byte Payload[32]{};
	};

	static_assert(sizeof(glm::vec2) == 8, "Payloads assume a tightly packed glm::vec2");
	static_assert(sizeof(PainterRect) == 16);
	static_assert(sizeof(PainterCommand) == 80, "The shader reads commands of exactly 80 bytes");
	static_assert(offsetof(PainterCommand, ClipRect) == 16);
	static_assert(offsetof(PainterCommand, OutlineWidth) == 32);
	static_assert(offsetof(PainterCommand, Payload) == 48);

	// Payloads, one per command type. Each is copied into PainterCommand::Payload as it is, so
	// its members are also the order in which the shader reads the floats.

	struct PainterRectPayload
	{
		PainterRect Rect;
		PainterCornerRadii Radii;
	};

	struct PainterCirclePayload
	{
		glm::vec2 Center{ 0.0f };
		float Radius = 0.0f;
	};

	// The part of a ring between two angles. Angles are in radians: 0 points right, and angles
	// grow clockwise on the screen, since y points down. StartAngle <= EndAngle; a difference of
	// 2 pi or more is a full ring. An inner radius of 0 makes a pie slice.
	struct PainterCircleSectorPayload
	{
		glm::vec2 Center{ 0.0f };
		float InnerRadius = 0.0f;
		float OuterRadius = 0.0f;
		float StartAngle = 0.0f;
		float EndAngle = 0.0f;
	};

	struct PainterLinePayload
	{
		glm::vec2 From{ 0.0f };
		glm::vec2 To{ 0.0f };
		float Width = 0.0f;
	};

	struct PainterTrianglePayload
	{
		glm::vec2 A{ 0.0f };
		glm::vec2 B{ 0.0f };
		glm::vec2 C{ 0.0f };
	};

	template<typename TPayload>
	void SetPayload(PainterCommand& command, const TPayload& payload)
	{
		static_assert(sizeof(TPayload) <= sizeof(PainterCommand::Payload), "A payload must fit in 32 bytes");
		std::memcpy(command.Payload, &payload, sizeof(TPayload));
	}

	template<typename TPayload>
	TPayload GetPayload(const PainterCommand& command)
	{
		static_assert(sizeof(TPayload) <= sizeof(PainterCommand::Payload), "A payload must fit in 32 bytes");
		TPayload payload;
		std::memcpy(&payload, command.Payload, sizeof(TPayload));
		return payload;
	}

	// Shadow and glow of a primitive group: 32 bytes, two vec4s in the shader's storage buffer.
	// All shapes of a group share them, so the group casts one shadow and glows as one shape.
	//
	//   bytes  0-15  ShadowColor, GlowColor (uints), ShadowOffset (2 floats)
	//   bytes 16-31  ShadowBlur, GlowDistance, GlowStrength, Unused (floats)
	struct PainterGroup
	{
		// Premultiplied RGBA8 like PainterCommand::Color; fully transparent means no shadow or glow
		uint32_t ShadowColor = 0;
		uint32_t GlowColor = 0;
		glm::vec2 ShadowOffset{ 0.0f };

		// How far past the edge of the moved shapes the shadow fades out, in pixels; 0 is a hard
		// edge. Tile binning counts on the shadow reaching no further.
		float ShadowBlur = 0.0f;
		// How far the glow reaches outside the shapes, in pixels
		float GlowDistance = 0.0f;
		// The glow's opacity at the shapes' edge, from 0 to 1
		float GlowStrength = 0.0f;
		float Unused = 0.0f;
	};

	static_assert(sizeof(PainterGroup) == 32, "The shader reads groups of exactly 32 bytes");

	// Packs a straight (not premultiplied) RGBA colour, each channel from 0 to 1, the way
	// PainterCommand::Color stores it. Channels outside 0 to 1 are clamped.
	uint32_t PackPremultipliedColor(const glm::vec4& straightColor);

	// The premultiplied colour, each channel from 0 to 1, as the shader sees it
	glm::vec4 UnpackPremultipliedColor(uint32_t packedColor);

	// The pixels the shape itself can cover, outline included; shadow, glow, anti-aliasing and
	// the clip rect are not included. Tile binning starts from this.
	PainterRect GetShapeBounds(const PainterCommand& command);
}
