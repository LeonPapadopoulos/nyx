#version 450

// The painter's pixel shader (Design/ui-painter.md, change 3).
//
// Every pixel finds its 8x8 tile, walks the tile's draw commands in order, and blends each one
// in, in registers: a command's shape is a signed distance field (negative inside, positive
// outside, in pixels), and one pixel of fade around its edge is the anti-aliasing. The result is
// written once, premultiplied, and blended over the target with (One, OneMinusSrcAlpha).
//
// How to add a primitive: the checklist is in PainterCommands.h. Here, it needs a constant
// below, a ...Distance() function, and a case in ShapeDistance().

// ---------------------------------------------------------------------------------------------
// Data, laid out exactly as PainterCommands.h and PainterTiles.h describe it
// ---------------------------------------------------------------------------------------------

// EPainterCommandType
const uint CommandRect = 0u;
const uint CommandCircle = 1u;
const uint CommandCircleSector = 2u;
const uint CommandLine = 3u;
const uint CommandTriangle = 4u;

// EPainterCommandFlags
const uint FlagRoundCaps = 1u;

const uint NoGroup = 0xFFFFFFFFu;
const uint TileSize = 8u;
const float Pi = 3.14159265358979;
const float Far = 1.0e9;

// 80 bytes; the payload is two vec4s, laid out by the payload struct of the type
struct PainterCommand
{
	uint Type;
	uint Flags;
	uint Color; // premultiplied RGBA8
	uint Group;
	vec4 ClipRect; // min x, min y, max x, max y
	float OutlineWidth;
	uint FillMode;
	uint Texture;
	uint Unused;
	vec4 Payload0;
	vec4 Payload1;
};

// 32 bytes
struct PainterGroup
{
	uint ShadowColor; // premultiplied RGBA8; 0: no shadow
	uint GlowColor;	  // premultiplied RGBA8; 0: no glow
	vec2 ShadowOffset;
	float ShadowBlur;
	float GlowDistance;
	float GlowStrength;
	float Unused;
};

layout(std430, set = 0, binding = 0) readonly buffer CommandBuffer
{
	PainterCommand Commands[];
};

layout(std430, set = 0, binding = 1) readonly buffer GroupBuffer
{
	PainterGroup Groups[];
};

// PainterTile: x is the first entry, y the number of entries
layout(std430, set = 0, binding = 2) readonly buffer TileBuffer
{
	uvec2 Tiles[];
};

layout(std430, set = 0, binding = 3) readonly buffer EntryBuffer
{
	uint Entries[];
};

// VulkanPainter.cpp, PainterPushConstants
layout(push_constant) uniform PainterPushConstants
{
	uint TilesX;
	uint TilesY;
	uint DebugView; // EPainterDebugView
	uint PushUnused;
};

const uint DebugViewNone = 0u;
const uint DebugViewHeatmap = 1u;

layout(location = 0) out vec4 outColor;

// ---------------------------------------------------------------------------------------------
// Distance functions: signed distance in pixels from `p` to the shape's edge, negative inside
// ---------------------------------------------------------------------------------------------

// Unsigned distance from p to the segment from a to b
float SegmentDistance(vec2 p, vec2 a, vec2 b)
{
	vec2 pa = p - a;
	vec2 ba = b - a;
	float along = clamp(dot(pa, ba) / max(dot(ba, ba), 1.0e-8), 0.0, 1.0);
	return length(pa - ba * along);
}

// A box centred on the origin with these half sizes, sharp corners
float BoxDistance(vec2 p, vec2 halfSize)
{
	vec2 d = abs(p) - halfSize;
	return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

// PainterRectPayload: Payload0 = min x, min y, max x, max y;
// Payload1 = corner radii top left, top right, bottom right, bottom left
float RectDistance(vec2 p, PainterCommand command)
{
	vec2 center = (command.Payload0.xy + command.Payload0.zw) * 0.5;
	vec2 halfSize = (command.Payload0.zw - command.Payload0.xy) * 0.5;
	vec2 q = p - center;

	// The radius of the corner p is nearest to. y points down, so q.y < 0 is the top half.
	vec4 radii = command.Payload1;
	float radius = q.x < 0.0 ? (q.y < 0.0 ? radii.x : radii.w) : (q.y < 0.0 ? radii.y : radii.z);

	// A box shrunk by the radius, then grown by it again with round corners
	return BoxDistance(q, halfSize - radius) - radius;
}

// PainterCirclePayload: Payload0 = centre x, centre y, radius
float CircleDistance(vec2 p, PainterCommand command)
{
	return length(p - command.Payload0.xy) - command.Payload0.z;
}

// PainterCircleSectorPayload: Payload0 = centre x, centre y, inner radius, outer radius;
// Payload1 = start angle, end angle. Angles grow clockwise on the screen, since y points down.
float CircleSectorDistance(vec2 p, PainterCommand command)
{
	vec2 q = p - command.Payload0.xy;
	float inner = command.Payload0.z;
	float outer = command.Payload0.w;
	float startAngle = command.Payload1.x;
	float endAngle = command.Payload1.y;

	// The full ring: the distance to the circle halfway between the radii, minus half the
	// ring's width
	float ring = abs(length(q) - (inner + outer) * 0.5) - (outer - inner) * 0.5;

	float halfAngle = (endAngle - startAngle) * 0.5;
	if (halfAngle >= Pi)
	{
		return ring;
	}

	// Turn q so that the sector lies symmetric around +x, then mirror the lower half onto the
	// upper one: only one straight end is left to measure against.
	float middle = startAngle + halfAngle;
	q = vec2(cos(middle) * q.x + sin(middle) * q.y, -sin(middle) * q.x + cos(middle) * q.y);
	q.y = abs(q.y);

	// The sector's straight end runs along `edge`, from the inner to the outer radius.
	vec2 edge = vec2(cos(halfAngle), sin(halfAngle));
	float endDistance = SegmentDistance(q, edge * inner, edge * outer);

	// Positive past the end's angle, i.e. outside the sector's angles
	bool bPastEnd = dot(q, vec2(-edge.y, edge.x)) > 0.0;
	return bPastEnd ? endDistance : max(ring, -endDistance);
}

// PainterLinePayload: Payload0 = from x, from y, to x, to y; Payload1.x = width
float LineDistance(vec2 p, PainterCommand command)
{
	vec2 from = command.Payload0.xy;
	vec2 to = command.Payload0.zw;
	float halfWidth = command.Payload1.x * 0.5;

	if ((command.Flags & FlagRoundCaps) != 0u)
	{
		return SegmentDistance(p, from, to) - halfWidth;
	}

	// Flat ends: a box along the line, measured in the line's own axes
	vec2 along = to - from;
	float lineLength = length(along);
	if (lineLength < 1.0e-4)
	{
		return Far;
	}
	vec2 direction = along / lineLength;
	vec2 local = p - (from + to) * 0.5;
	vec2 inLineAxes = vec2(dot(local, direction), dot(local, vec2(-direction.y, direction.x)));
	return BoxDistance(inLineAxes, vec2(lineLength * 0.5, halfWidth));
}

// PainterTrianglePayload: Payload0 = a x, a y, b x, b y; Payload1.xy = c.
// After Inigo Quilez's exact triangle distance; works for either winding.
float TriangleDistance(vec2 p, PainterCommand command)
{
	vec2 p0 = command.Payload0.xy;
	vec2 p1 = command.Payload0.zw;
	vec2 p2 = command.Payload1.xy;

	vec2 e0 = p1 - p0;
	vec2 e1 = p2 - p1;
	vec2 e2 = p0 - p2;
	vec2 v0 = p - p0;
	vec2 v1 = p - p1;
	vec2 v2 = p - p2;

	// The nearest point on each edge
	vec2 pq0 = v0 - e0 * clamp(dot(v0, e0) / max(dot(e0, e0), 1.0e-8), 0.0, 1.0);
	vec2 pq1 = v1 - e1 * clamp(dot(v1, e1) / max(dot(e1, e1), 1.0e-8), 0.0, 1.0);
	vec2 pq2 = v2 - e2 * clamp(dot(v2, e2) / max(dot(e2, e2), 1.0e-8), 0.0, 1.0);

	// x: the squared distance to the nearest edge; y: negative once p is outside any edge
	float winding = sign(e0.x * e2.y - e0.y * e2.x);
	vec2 d = min(min(vec2(dot(pq0, pq0), winding * (v0.x * e0.y - v0.y * e0.x)),
					 vec2(dot(pq1, pq1), winding * (v1.x * e1.y - v1.y * e1.x))),
		vec2(dot(pq2, pq2), winding * (v2.x * e2.y - v2.y * e2.x)));
	return -sqrt(d.x) * sign(d.y);
}

// The command's distance, its outline included
float ShapeDistance(vec2 p, PainterCommand command)
{
	float result = Far;
	switch (command.Type)
	{
	case CommandRect:
		result = RectDistance(p, command);
		break;
	case CommandCircle:
		result = CircleDistance(p, command);
		break;
	case CommandCircleSector:
		result = CircleSectorDistance(p, command);
		break;
	case CommandLine:
		result = LineDistance(p, command);
		break;
	case CommandTriangle:
		result = TriangleDistance(p, command);
		break;
	default:
		// Glyphs and images arrive in change 4.
		return Far;
	}

	// An outline is a band centred on the edge.
	if (command.OutlineWidth > 0.0)
	{
		result = abs(result) - command.OutlineWidth * 0.5;
	}
	return result;
}

// ---------------------------------------------------------------------------------------------
// Blending
// ---------------------------------------------------------------------------------------------

// How much of a pixel a shape covers: 1 inside, 0 outside, fading over one pixel centred on the
// edge. This is the anti-aliasing.
float Coverage(float signedDistance)
{
	return clamp(0.5 - signedDistance, 0.0, 1.0);
}

bool IsInsideClipRect(vec2 p, vec4 clipRect)
{
	return p.x >= clipRect.x && p.y >= clipRect.y && p.x < clipRect.z && p.y < clipRect.w;
}

// Premultiplied `source` over `destination`
vec4 Over(vec4 destination, vec4 source)
{
	return source + destination * (1.0 - source.a);
}

// A group's shadow and glow, drawn under its shapes. The group's shapes follow each other in the
// tile's list from `firstEntry` on: the painter records a group in one go, and binning puts every
// shape of a group into every tile the group reaches.
vec4 DrawGroupEffects(vec4 color, vec2 pixel, uint groupIndex, uint firstEntry, uint endEntry)
{
	PainterGroup group = Groups[groupIndex];

	// The group as one shape: the union of its shapes is the nearest of their distances.
	float shapes = Far;
	float movedShapes = Far;
	for (uint entry = firstEntry; entry < endEntry; ++entry)
	{
		PainterCommand command = Commands[Entries[entry]];
		if (command.Group != groupIndex)
		{
			break;
		}
		if (!IsInsideClipRect(pixel, command.ClipRect))
		{
			continue;
		}
		shapes = min(shapes, ShapeDistance(pixel, command));
		movedShapes = min(movedShapes, ShapeDistance(pixel - group.ShadowOffset, command));
	}

	if (group.ShadowColor != 0u)
	{
		// Full under the moved shapes, faded out ShadowBlur pixels past their edge. Tile
		// binning counts on the shadow reaching no further.
		float amount = 1.0 - smoothstep(-0.5, group.ShadowBlur + 0.5, movedShapes);
		color = Over(color, unpackUnorm4x8(group.ShadowColor) * amount);
	}

	if (group.GlowColor != 0u && group.GlowDistance > 0.0)
	{
		// Strongest at the shapes' edge, faded out GlowDistance pixels past it
		float amount = group.GlowStrength * (1.0 - clamp(shapes / group.GlowDistance, 0.0, 1.0));
		color = Over(color, unpackUnorm4x8(group.GlowColor) * amount);
	}

	return color;
}

// The tile heatmap: blue for a few commands per tile, through green, to red for 16 or more
vec4 Heatmap(uint commandCount)
{
	float t = clamp(float(commandCount) / 16.0, 0.0, 1.0);
	vec3 few = vec3(0.1, 0.3, 1.0);
	vec3 some = vec3(0.1, 0.9, 0.2);
	vec3 many = vec3(1.0, 0.15, 0.1);
	vec3 rgb = t < 0.5 ? mix(few, some, t * 2.0) : mix(some, many, t * 2.0 - 1.0);
	return vec4(rgb, 1.0) * 0.6;
}

void main()
{
	// The pixel's centre, e.g. (0.5, 0.5) for the top-left pixel
	vec2 pixel = gl_FragCoord.xy;

	uvec2 tileCoordinates = uvec2(pixel) / TileSize;
	if (tileCoordinates.x >= TilesX || tileCoordinates.y >= TilesY)
	{
		discard;
	}
	uvec2 tile = Tiles[tileCoordinates.y * TilesX + tileCoordinates.x];
	uint firstEntry = tile.x;
	uint endEntry = tile.x + tile.y;

	// Most of a window is not painted; those pixels keep what's beneath.
	if (tile.y == 0u)
	{
		discard;
	}

	vec4 color = vec4(0.0);
	uint groupWithEffectsDrawn = NoGroup;
	for (uint entry = firstEntry; entry < endEntry; ++entry)
	{
		PainterCommand command = Commands[Entries[entry]];

		if (command.Group != NoGroup && command.Group != groupWithEffectsDrawn)
		{
			color = DrawGroupEffects(color, pixel, command.Group, entry, endEntry);
			groupWithEffectsDrawn = command.Group;
		}

		if (!IsInsideClipRect(pixel, command.ClipRect))
		{
			continue;
		}
		float coverage = Coverage(ShapeDistance(pixel, command));
		color = Over(color, unpackUnorm4x8(command.Color) * coverage);
	}

	if (DebugView == DebugViewHeatmap)
	{
		color = Over(color, Heatmap(tile.y));
	}

	outColor = color;
}
