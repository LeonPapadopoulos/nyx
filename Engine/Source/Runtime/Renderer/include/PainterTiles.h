#pragma once

#include "PainterCommands.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

// Tile binning: which of a frame's draw commands can touch each 8x8 pixel tile of the target.
// The painter shader then only evaluates a pixel's own tile's commands, instead of all of them.
// The CPU measures each command once (MeasurePainterSegment()); the GPU bins every tile
// (PainterBin.comp).

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

	// Limits per frame, so that binning never allocates without bound, whatever is recorded. Past
	// MaxCommands, a segment's remaining commands are dropped; tiles whose entries don't fit in
	// MaxTileEntries are drawn empty. Either is reported.
	struct PainterBudget
	{
		uint32_t MaxCommands = 64 * 1024;
		// One entry is one command in one tile, 4 bytes each: the default is 16 MB.
		uint32_t MaxTileEntries = 4 * 1024 * 1024;
	};

	struct PainterBinningStats
	{
		// Measured on the CPU (MeasurePainterSegment()):
		// commands recorded this frame
		uint32_t Commands = 0;
		// Commands that can't touch any pixel: off the target, or clipped away. Not a problem.
		uint32_t CulledCommands = 0;
		// Commands dropped because the budget ran out
		uint32_t DroppedCommands = 0;
		// How long measuring took
		float BinningMilliseconds = 0.0f;

		// Binned on the GPU, read back once it is done (VulkanPainterSegments::ReadBack()):
		// tiles with at least one command, their entries, and the most in one tile
		uint32_t TilesInUse = 0;
		uint32_t TileEntries = 0;
		uint32_t MaxCommandsPerTile = 0;
		// The entries before the per-tile reduction dropped what lies under opaque shapes
		uint32_t EntriesBeforeReduction = 0;

		// For the statistics in the Painter Test window: 0 when no tile is in use
		float GetAverageCommandsPerTileInUse() const
		{
			return TilesInUse > 0 ? static_cast<float>(TileEntries) / static_cast<float>(TilesInUse) : 0.0f;
		}
	};

	// How much painting lies on each tile of an OS window, summed over all its draws: the
	// commands the tile's pixels walk on the GPU, and the draws they run in. Draws overlap, so one
	// draw's own counts don't show it.
	struct PainterHeatGrid
	{
		// The grid covers the OS window's target from its top left, TilesX by TilesY tiles
		uint32_t TilesX = 0;
		uint32_t TilesY = 0;
		std::vector<uint32_t> Commands;
		std::vector<uint32_t> Draws;

		// Empties the grid and sizes it for a target of `width` by `height` pixels
		void Reset(uint32_t width, uint32_t height)
		{
			TilesX = (width + PainterTileSize - 1) / PainterTileSize;
			TilesY = (height + PainterTileSize - 1) / PainterTileSize;
			Commands.assign(static_cast<size_t>(TilesX) * TilesY, 0);
			Draws.assign(static_cast<size_t>(TilesX) * TilesY, 0);
		}

		// One draw's tile at grid tile `tileX`, `tileY`, with `count` commands; nothing outside the grid
		void Add(uint32_t tileX, uint32_t tileY, uint32_t count)
		{
			if (count == 0 || tileX >= TilesX || tileY >= TilesY)
			{
				return;
			}
			const size_t index = static_cast<size_t>(tileY) * TilesX + tileX;
			Commands[index] += count;
			++Draws[index];
		}
	};

	// What the per-tile reduction does: in each tile, every entry before the last command that
	// covers the whole tile opaquely goes, since nothing drawn before it shows there
	struct PainterReduction
	{
		// The tiles' entries before the reduction, and those it keeps
		uint32_t EntriesBefore = 0;
		uint32_t Kept = 0;
	};

	// One frame of painting inside ImGui windows (Nyx::UI::WindowPainter), summed over all
	// windows and their segments, and what the frame's UI cost
	struct PainterWindowStats
	{
		// One OS window's painting: its segments, and the draws they are drawn in. Segments that
		// follow each other with nothing ImGui draws between them are one draw.
		struct OsWindow
		{
			std::string Name;
			uint32_t Segments = 0;
			uint32_t Draws = 0;
			uint32_t Commands = 0;
			uint32_t TilesInUse = 0;
			uint32_t TileEntries = 0;
			float BinningMilliseconds = 0.0f;
			// The painter's draws in this OS window, on the GPU; 0 where the GPU can't time them
			float GpuMilliseconds = 0.0f;
			// What ImGui still draws itself here: its draw commands with triangles, and their vertices
			uint32_t ImGuiDrawCommands = 0;
			uint32_t ImGuiVertices = 0;

			// The OS window's ImGui viewport and its target pixels per ImGui unit; while the heatmap
			// is on, the heat of its tiles
			uint32_t ViewportId = 0;
			float PixelsPerPoint = 1.0f;
			PainterHeatGrid Heat;
		};

		// Where the frame's UI time went, in milliseconds
		struct Times
		{
			// The editor's whole frame: from the end of the frame before to this one's, so that
			// the steps below add up to it
			float Frame = 0.0f;

			// CPU: building the UI (the editor's panels, ImGui's layout and the painting recorded
			// in them), painting the window chrome, gathering the window painters,
			// ImGui::Render(), and the segments (finding runs, binning and uploading them)
			float BuildUI = 0.0f;
			float Chrome = 0.0f;
			float Gather = 0.0f;
			float ImGuiRender = 0.0f;
			float Segments = 0.0f;

			// CPU, the rest of the frame: outside the renderer (events, the layers' updates, the
			// frame rate cap), waiting for the last frame's GPU work, acquiring the next swapchain
			// image, the world and the scene views, the painter canvases with the marks and the
			// heatmap, recording and submitting the UI after the segments, and presenting
			float Outside = 0.0f;
			float WaitForGpu = 0.0f;
			float Acquire = 0.0f;
			float Scene = 0.0f;
			float Canvases = 0.0f;
			float RecordUI = 0.0f;
			float Present = 0.0f;

			// GPU, from timestamps: binning the segments, the painter's draws in all OS windows,
			// the main window's whole UI pass (ImGui's drawing and the painter's), and the glass
			// backdrop; the scene views' editor passes (picking and the selection masks), the scene
			// views themselves, and the frame's whole command buffer (all but the OS windows').
			// Only while bGpu.
			bool bGpu = false;
			float GpuBinning = 0.0f;
			float GpuPainter = 0.0f;
			float GpuMainWindowUI = 0.0f;
			float GpuGlassBackdrop = 0.0f;
			float GpuEditorPasses = 0.0f;
			float GpuSceneViews = 0.0f;
			float GpuFrame = 0.0f;

			// What the painter adds on the CPU after the UI is built
			float GetPainterCpu() const { return Chrome + Gather + Segments; }
		};

		// Windows that painted
		uint32_t Windows = 0;
		uint32_t Segments = 0;
		// Draws of all OS windows, and each one's
		uint32_t Draws = 0;
		std::vector<OsWindow> OsWindows;
		uint32_t Commands = 0;
		// Tiles with at least one command, and command entries in them, as the GPU binned them
		uint32_t TilesInUse = 0;
		uint32_t TileEntries = 0;
		// Measuring all segments' commands for binning, on the CPU
		float BinningMilliseconds = 0.0f;
		// What goes to the GPU each frame for the segments: commands, groups, and the measured
		// commands and segments the GPU bins from
		size_t UploadBytes = 0;
		Times FrameTimes;
		// The per-tile reduction, over all segments
		PainterReduction Reduction;

		// What went wrong: the window painters' problems, each after its window's name, and
		// binning's
		std::vector<std::string> Problems;
	};

	// A segment's commands sorted into tiles, as the GPU binned them and the shader reads them,
	// read back (VulkanPainterSegments::GetBinned()), e.g. for the tests
	struct PainterTiles
	{
		// The grid covers the whole target (a canvas), or only the tiles a segment's commands can
		// touch. Tiles at its right and bottom edges may stick out of the target.
		uint32_t TilesX = 0;
		uint32_t TilesY = 0;

		// The target pixel where the grid's top left tile starts, a multiple of PainterTileSize.
		// 0, 0 for a grid over the whole target.
		uint32_t OriginX = 0;
		uint32_t OriginY = 0;

		// TilesX * TilesY tiles, row by row from the top left, each with its entries from FirstEntry
		std::vector<PainterTile> Tiles;
		// Command indices, grouped by tile, counting from the segment's first command
		std::vector<uint32_t> Entries;

		PainterBinningStats Stats;

		// What went wrong while binning this frame, e.g. the budget running out
		std::vector<std::string> Problems;

		const PainterTile& GetTile(uint32_t tileX, uint32_t tileY) const { return Tiles[tileY * TilesX + tileX]; }
	};

	// One command as the GPU binner reads it (PainterBin.comp): 64 bytes, four vec4s (std430).
	// Rects are in target pixels; an empty rect (Min not below Max) is none.
	struct PainterBinCommand
	{
		// What the command can touch: its shape (GetShapeBounds()), or for a command in a group the
		// group's whole reach (all its shapes in the segment, with their shadow and glow: the shader
		// needs every shape of a group for its shadow and glow, even in a tile only one shadow
		// reaches), grown by the anti-aliasing reach, cut by its clip rect and the target
		PainterRect Reach;
		// Tiles wholly inside it are covered opaquely by the command, which hides what comes before
		// it there: a solid, opaque rect without outline, less its largest rounding, cut by its clip
		// rect. Empty for every other command.
		PainterRect Hides;
		// A line on its own touches only the tiles of its band, row by row, not its whole Reach:
		// its ends, and how far its pixels reach from it (half its width and the anti-aliasing).
		// LineReach 0: not a line on its own.
		glm::vec2 LineFrom{ 0.0f };
		glm::vec2 LineTo{ 0.0f };
		float LineReach = 0.0f;
		float Unused[3] = {};
	};

	static_assert(sizeof(PainterBinCommand) == 64, "PainterBin.comp reads commands of exactly 64 bytes");

	// The GPU binner's workgroup: a segment's tiles start at a multiple of it, so that one workgroup
	// bins only one segment's tiles (PainterBin.comp's local_size_x)
	constexpr uint32_t PainterBinWorkgroupSize = 64;

	// One segment as the GPU binner reads it: 32 bytes, std430
	struct PainterBinSegment
	{
		// Its measured commands, and its tiles in the tile buffer
		uint32_t FirstCommand = 0;
		uint32_t CommandCount = 0;
		uint32_t FirstTile = 0;
		uint32_t TilesX = 0;
		uint32_t TilesY = 0;
		uint32_t OriginX = 0;
		uint32_t OriginY = 0;
		uint32_t Unused = 0;
	};

	static_assert(sizeof(PainterBinSegment) == 32, "PainterBin.comp reads segments of exactly 32 bytes");

	// What the GPU binner sums for one segment: 32 bytes, std430, written by the GPU and read back
	struct PainterBinSums
	{
		uint32_t EntriesBefore = 0;
		uint32_t Entries = 0;
		uint32_t TilesInUse = 0;
		uint32_t MaxPerTile = 0;
		// Not 0 if some of its tiles' entries didn't fit, and were drawn empty
		uint32_t Overflowed = 0;
		uint32_t Unused[3] = {};
	};

	static_assert(sizeof(PainterBinSums) == 32, "PainterBin.comp writes sums of exactly 32 bytes");

	// One segment measured for binning on the GPU
	struct PainterMeasuredSegment
	{
		// The grid, as in PainterTiles
		uint32_t OriginX = 0;
		uint32_t OriginY = 0;
		uint32_t TilesX = 0;
		uint32_t TilesY = 0;
		// One per command binned, in drawing order; commands past the budget aren't
		std::vector<PainterBinCommand> Commands;
		// The most entries its tiles can have: each command's tiles, before the reduction. Lines
		// count their whole Reach, so it may be more.
		uint64_t MaxEntries = 0;
		PainterBinningStats Stats;
		std::vector<std::string> Problems;
	};

	// Measures one segment's commands for binning on the GPU, once per command: what each can
	// touch and what it hides (PainterBinCommand), the grid, and the most entries. The grid covers
	// the whole target with bWholeTarget (a canvas), or else only the tiles the commands can
	// touch: a small painted button costs a few tiles, not the whole target's. A group's reach
	// counts only its shapes in this segment.
	//
	// Reuses the memory of `inOutSegment` from the previous frame.
	void MeasurePainterSegment(const Painter& painter, const PainterSegment& segment, bool bWholeTarget, const PainterBudget& budget,
		PainterMeasuredSegment& inOutSegment);
}
