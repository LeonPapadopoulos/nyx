#version 450

// The painter draws one quad over its tile grid; Painter.frag does the rest per pixel. For a
// canvas the grid covers the whole target. For a segment inside an ImGui window it covers only
// the tiles the segment's commands touch, so the rest of the window is left alone. Tiles at the
// right and bottom edges may stick out of the target; the GPU clips them away.

const float TileSize = 8.0;

// VulkanPainter.cpp, PainterPushConstants; the same block as in Painter.frag
layout(push_constant) uniform PainterPushConstants
{
	uint CommandBase;
	uint GroupBase;
	uint TileBase;
	uint EntryBase;
	uint OriginX;
	uint OriginY;
	uint TilesX;
	uint TilesY;
	float TargetWidth;
	float TargetHeight;
	uint DebugView;
	uint PushUnused;
};

// The quad's two triangles, as corners of the grid from (0, 0) at its top left to (1, 1)
const vec2 Corners[6] = vec2[](
	vec2(0.0, 0.0),
	vec2(1.0, 0.0),
	vec2(0.0, 1.0),
	vec2(1.0, 0.0),
	vec2(1.0, 1.0),
	vec2(0.0, 1.0)
);

void main()
{
	vec2 gridMin = vec2(OriginX, OriginY);
	vec2 gridMax = gridMin + vec2(TilesX, TilesY) * TileSize;
	vec2 pixel = mix(gridMin, gridMax, Corners[gl_VertexIndex]);

	// Target pixels to clip space, where (-1, -1) is the top left in Vulkan
	gl_Position = vec4(pixel / vec2(TargetWidth, TargetHeight) * 2.0 - 1.0, 0.0, 1.0);
}
