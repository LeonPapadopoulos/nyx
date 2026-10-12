#pragma once

#include "PainterCommands.h"

#include <cstdint>
#include <string>
#include <vector>

// Tile binning: which of a frame's draw commands can touch each 8x8 pixel tile of the target.
// The painter shader then only evaluates a pixel's own tile's commands, instead of all of them.
// The plan is in Design/ui-painter.md: stage 1, change 2, and for segments stage 2, change 2.

namespace Nyx::Engine
{
	class Painter;
	struct PainterSegment;

	// Tiles are 8x8 pixels: a GPU runs pixel shaders in waves of 32 or 64 threads, scheduled in
	// 8x8 blocks, so all pixels of a wave walk the same command list.
	constexpr uint32_t PainterTileSize = 8;

	// How far a shape's anti-aliasing reaches past its edge, in pixels. The shader fades an edge
	// out over one pixel centred on it, so half a pixel would do; one leaves room for rounding.
	constexpr float PainterAntiAliasingReach = 1.0f;

	// One tile as the shader reads it: 8 bytes, a uvec2 in its storage buffer (std430). Its
	// commands are Entries[FirstEntry] to Entries[FirstEntry + Count - 1], in drawing order.
	struct PainterTile
	{
		uint32_t FirstEntry = 0;
		uint32_t Count = 0;
	};

	static_assert(sizeof(PainterTile) == 8, "The shader reads tiles of exactly 8 bytes");

	// Limits per frame. Past them, the frame's remaining commands are dropped and a problem is
	// reported; binning never allocates without bound, whatever is recorded.
	struct PainterBudget
	{
		uint32_t MaxCommands = 64 * 1024;
		// One entry is one command in one tile, 4 bytes each: the default is 16 MB.
		uint32_t MaxTileEntries = 4 * 1024 * 1024;
	};

	struct PainterBinningStats
	{
		// Commands recorded this frame
		uint32_t Commands = 0;
		// Commands that can't touch any pixel: off the target, or clipped away. Not a problem.
		uint32_t CulledCommands = 0;
		// Commands dropped because the budget ran out
		uint32_t DroppedCommands = 0;

		// Tiles with at least one command
		uint32_t TilesInUse = 0;
		uint32_t TileEntries = 0;
		uint32_t MaxCommandsPerTile = 0;

		// How long BinPainterCommands() took, on the CPU
		float BinningMilliseconds = 0.0f;

		// For the statistics in the Painter Test window: 0 when no tile is in use
		float GetAverageCommandsPerTileInUse() const
		{
			return TilesInUse > 0 ? static_cast<float>(TileEntries) / static_cast<float>(TilesInUse) : 0.0f;
		}
	};

	// One frame of painting inside ImGui windows (Nyx::UI::WindowPainter), summed over all
	// windows and their segments, e.g. for the Painter Test window
	struct PainterWindowStats
	{
		// One OS window's painting: its segments, and the draws they are drawn in. Segments that
		// follow each other with nothing ImGui draws between them are one draw.
		struct OsWindow
		{
			std::string Name;
			uint32_t Segments = 0;
			uint32_t Draws = 0;
		};

		// Windows that painted
		uint32_t Windows = 0;
		uint32_t Segments = 0;
		// Draws of all OS windows, and each one's
		uint32_t Draws = 0;
		std::vector<OsWindow> OsWindows;
		uint32_t Commands = 0;
		// Tiles with at least one command, and command entries in them
		uint32_t TilesInUse = 0;
		uint32_t TileEntries = 0;
		// Binning all segments, on the CPU
		float BinningMilliseconds = 0.0f;

		// What went wrong: the window painters' problems, each after its window's name, and
		// binning's
		std::vector<std::string> Problems;
	};

	// A frame's commands, or a segment's, sorted into tiles, as the GPU reads them
	struct PainterTiles
	{
		// The grid covers the whole target (BinPainterCommands()), or only the tiles a segment's
		// commands can touch (BinPainterSegment()). Tiles at its right and bottom edges may stick
		// out of the target.
		uint32_t TilesX = 0;
		uint32_t TilesY = 0;

		// The target pixel where the grid's top left tile starts, a multiple of PainterTileSize.
		// 0, 0 for a grid over the whole target.
		uint32_t OriginX = 0;
		uint32_t OriginY = 0;

		// TilesX * TilesY tiles, row by row from the top left
		std::vector<PainterTile> Tiles;
		// Command indices, grouped by tile. For a segment they count from its first command.
		std::vector<uint32_t> Entries;

		PainterBinningStats Stats;

		// What went wrong while binning this frame, e.g. the budget running out
		std::vector<std::string> Problems;

		const PainterTile& GetTile(uint32_t tileX, uint32_t tileY) const { return Tiles[tileY * TilesX + tileX]; }
	};

	// Sorts the painter's commands into tiles. A command goes into every tile its coverage can
	// touch:
	//  - its shape (GetShapeBounds()), grown by the anti-aliasing reach;
	//  - for a command in a group, the whole group's reach instead: all of the group's shapes,
	//    with their shadow and glow. The shader needs every shape of a group to draw its shadow
	//    and glow, even in a tile that only the shadow of one of them reaches.
	//  - lines on their own walk their tiles row by row, so a diagonal line touches a thin band
	//    of tiles, not its whole bounding box;
	//  - cut by the command's clip rect and the target.
	// Commands keep their order within each tile, since the shader blends in that order.
	//
	// Reuses the memory of `inOutTiles` from the previous frame.
	void BinPainterCommands(const Painter& painter, const PainterBudget& budget, PainterTiles& inOutTiles);

	// Sorts one segment's commands into tiles, like BinPainterCommands(), over a grid that only
	// covers the tiles those commands can touch: a small painted button costs a few tiles, not
	// the whole target's. A group's reach counts only its shapes in this segment. The budget
	// applies to this segment alone.
	void BinPainterSegment(const Painter& painter, const PainterSegment& segment, const PainterBudget& budget, PainterTiles& inOutTiles);
}
