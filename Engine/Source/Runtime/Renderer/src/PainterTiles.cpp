#include "PainterTiles.h"

#include "Painter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

namespace Nyx::Engine
{
	namespace
	{
		constexpr float TileSize = static_cast<float>(PainterTileSize);

		// The pixels one command can touch, worked out once and then walked tile by tile
		struct CommandCoverage
		{
			// In target pixels, already cut by the clip rect and the target. Empty: the command
			// can't touch any pixel.
			PainterRect Rect;

			// Lines on their own are walked row by row instead of filling Rect.
			bool bWalkLine = false;
			glm::vec2 LineFrom{ 0.0f };
			glm::vec2 LineTo{ 0.0f };
			// How far the line's pixels can be from the line: half its width plus anti-aliasing
			float LineReach = 0.0f;
		};

		// Tiles from Min to Max, both included
		struct TileRange
		{
			uint32_t MinX = 0;
			uint32_t MinY = 0;
			uint32_t MaxX = 0;
			uint32_t MaxY = 0;
		};

		// The tiles a rect touches. False if it touches none.
		bool GetTileRange(const PainterRect& rect, uint32_t tilesX, uint32_t tilesY, TileRange& outRange)
		{
			if (rect.IsEmpty() || tilesX == 0 || tilesY == 0)
			{
				return false;
			}

			// A tile x covers pixels from x * 8 up to, but not including, (x + 1) * 8.
			const float minX = std::floor(rect.Min.x / TileSize);
			const float minY = std::floor(rect.Min.y / TileSize);
			const float maxX = std::ceil(rect.Max.x / TileSize) - 1.0f;
			const float maxY = std::ceil(rect.Max.y / TileSize) - 1.0f;
			if (maxX < 0.0f || maxY < 0.0f || minX >= static_cast<float>(tilesX) || minY >= static_cast<float>(tilesY))
			{
				return false;
			}

			outRange.MinX = static_cast<uint32_t>(std::max(minX, 0.0f));
			outRange.MinY = static_cast<uint32_t>(std::max(minY, 0.0f));
			outRange.MaxX = std::min(static_cast<uint32_t>(maxX), tilesX - 1);
			outRange.MaxY = std::min(static_cast<uint32_t>(maxY), tilesY - 1);
			return outRange.MinX <= outRange.MaxX && outRange.MinY <= outRange.MaxY;
		}

		// Narrows one row of tiles to those a line can touch. The line's pixels lie within
		// LineReach of the line, so in this row they lie within LineReach of the part of the line
		// that runs between the row's top and bottom, each moved out by LineReach. False if the
		// line doesn't touch the row.
		bool NarrowRowToLine(const CommandCoverage& coverage, uint32_t tileY, uint32_t& inOutMinX, uint32_t& inOutMaxX)
		{
			const glm::vec2 from = coverage.LineFrom;
			const glm::vec2 to = coverage.LineTo;
			const float reach = coverage.LineReach;
			const float rowTop = static_cast<float>(tileY) * TileSize - reach;
			const float rowBottom = static_cast<float>(tileY + 1) * TileSize + reach;

			float lineMinX = 0.0f;
			float lineMaxX = 0.0f;
			if (from.y == to.y)
			{
				// A horizontal line touches the whole row or none of it.
				if (from.y < rowTop || from.y > rowBottom)
				{
					return false;
				}
				lineMinX = std::min(from.x, to.x);
				lineMaxX = std::max(from.x, to.x);
			}
			else
			{
				// Where along the line (0 at `from`, 1 at `to`) it crosses the row's top and bottom
				const float atTop = (rowTop - from.y) / (to.y - from.y);
				const float atBottom = (rowBottom - from.y) / (to.y - from.y);
				const float enter = std::max(std::min(atTop, atBottom), 0.0f);
				const float leave = std::min(std::max(atTop, atBottom), 1.0f);
				if (enter > leave)
				{
					return false;
				}
				const float enterX = from.x + (to.x - from.x) * enter;
				const float leaveX = from.x + (to.x - from.x) * leave;
				lineMinX = std::min(enterX, leaveX);
				lineMaxX = std::max(enterX, leaveX);
			}

			const float firstTile = std::floor((lineMinX - reach) / TileSize);
			const float lastTile = std::ceil((lineMaxX + reach) / TileSize) - 1.0f;
			if (lastTile < static_cast<float>(inOutMinX) || firstTile > static_cast<float>(inOutMaxX))
			{
				return false;
			}
			inOutMinX = std::max(inOutMinX, static_cast<uint32_t>(std::max(firstTile, 0.0f)));
			inOutMaxX = std::min(inOutMaxX, static_cast<uint32_t>(lastTile));
			return inOutMinX <= inOutMaxX;
		}

		// Calls visit(tileIndex) for every tile the command can touch, row by row
		template<typename TVisit>
		void ForEachTile(const CommandCoverage& coverage, uint32_t tilesX, uint32_t tilesY, TVisit&& visit)
		{
			TileRange range;
			if (!GetTileRange(coverage.Rect, tilesX, tilesY, range))
			{
				return;
			}

			for (uint32_t tileY = range.MinY; tileY <= range.MaxY; ++tileY)
			{
				uint32_t minX = range.MinX;
				uint32_t maxX = range.MaxX;
				if (coverage.bWalkLine && !NarrowRowToLine(coverage, tileY, minX, maxX))
				{
					continue;
				}
				for (uint32_t tileX = minX; tileX <= maxX; ++tileX)
				{
					visit(tileY * tilesX + tileX);
				}
			}
		}

		uint32_t CountTiles(const CommandCoverage& coverage, uint32_t tilesX, uint32_t tilesY)
		{
			uint32_t count = 0;
			ForEachTile(coverage, tilesX, tilesY, [&](uint32_t)
				{
					++count;
				});
			return count;
		}

		// How far each group can reach: all of its shapes, plus its shadow and glow. Empty for a
		// group without shapes.
		std::vector<PainterRect> MeasureGroupReach(const std::vector<PainterCommand>& commands, const std::vector<PainterGroup>& groups)
		{
			std::vector<PainterRect> shapes(groups.size());
			std::vector<bool> bHasShapes(groups.size(), false);
			for (const PainterCommand& command : commands)
			{
				if (command.Group >= groups.size())
				{
					continue;
				}
				const PainterRect bounds = GetShapeBounds(command);
				shapes[command.Group] = bHasShapes[command.Group] ? Union(shapes[command.Group], bounds) : bounds;
				bHasShapes[command.Group] = true;
			}

			std::vector<PainterRect> reach(groups.size());
			for (size_t groupIndex = 0; groupIndex < groups.size(); ++groupIndex)
			{
				if (!bHasShapes[groupIndex])
				{
					continue;
				}
				const PainterGroup& group = groups[groupIndex];
				reach[groupIndex] = shapes[groupIndex];
				if (group.ShadowColor != 0)
				{
					const PainterRect shadow = Grow(Move(shapes[groupIndex], group.ShadowOffset), group.ShadowBlur);
					reach[groupIndex] = Union(reach[groupIndex], shadow);
				}
				if (group.GlowColor != 0)
				{
					reach[groupIndex] = Union(reach[groupIndex], Grow(shapes[groupIndex], group.GlowDistance));
				}
			}
			return reach;
		}

		CommandCoverage MeasureCoverage(const PainterCommand& command, const std::vector<PainterRect>& groupReach, const PainterRect& target)
		{
			const bool bInGroup = command.Group < groupReach.size();
			const PainterRect reach = bInGroup ? groupReach[command.Group] : GetShapeBounds(command);

			CommandCoverage coverage;
			coverage.Rect = Intersect(Intersect(Grow(reach, PainterAntiAliasingReach), command.ClipRect), target);

			// A line in a group covers the group's reach like any other shape: its shadow and
			// glow spread around the whole group.
			if (!bInGroup && static_cast<EPainterCommandType>(command.Type) == EPainterCommandType::Line)
			{
				const auto line = GetPayload<PainterLinePayload>(command);
				coverage.bWalkLine = true;
				coverage.LineFrom = line.From;
				coverage.LineTo = line.To;
				coverage.LineReach = line.Width * 0.5f + PainterAntiAliasingReach;
			}
			return coverage;
		}
	}

	void BinPainterCommands(const Painter& painter, const PainterBudget& budget, PainterTiles& inOutTiles)
	{
		const auto startTime = std::chrono::steady_clock::now();

		const std::vector<PainterCommand>& commands = painter.GetCommands();
		const uint32_t targetWidth = painter.GetTargetWidth();
		const uint32_t targetHeight = painter.GetTargetHeight();

		PainterTiles& tiles = inOutTiles;
		tiles.TilesX = (targetWidth + PainterTileSize - 1) / PainterTileSize;
		tiles.TilesY = (targetHeight + PainterTileSize - 1) / PainterTileSize;
		tiles.Tiles.assign(static_cast<size_t>(tiles.TilesX) * tiles.TilesY, PainterTile{});
		tiles.Entries.clear();
		tiles.Stats = PainterBinningStats{};
		tiles.Stats.Commands = static_cast<uint32_t>(commands.size());
		tiles.Problems.clear();

		const PainterRect target{ glm::vec2(0.0f), glm::vec2(static_cast<float>(targetWidth), static_cast<float>(targetHeight)) };
		const std::vector<PainterRect> groupReach = MeasureGroupReach(commands, painter.GetGroups());

		// Pass 1: work out each command's coverage and count the commands of each tile. The
		// budget is checked before a command is counted, so a command is either binned
		// completely or, with all commands after it, not at all.
		std::vector<CommandCoverage> coverages;
		coverages.reserve(std::min<size_t>(commands.size(), budget.MaxCommands));
		uint64_t entryCount = 0;
		for (const PainterCommand& command : commands)
		{
			if (coverages.size() >= budget.MaxCommands)
			{
				break;
			}

			const CommandCoverage coverage = MeasureCoverage(command, groupReach, target);
			const uint32_t tileCount = CountTiles(coverage, tiles.TilesX, tiles.TilesY);
			if (entryCount + tileCount > budget.MaxTileEntries)
			{
				break;
			}

			if (tileCount == 0)
			{
				++tiles.Stats.CulledCommands;
			}
			ForEachTile(coverage, tiles.TilesX, tiles.TilesY, [&](uint32_t tileIndex)
				{
					++tiles.Tiles[tileIndex].Count;
				});
			entryCount += tileCount;
			coverages.push_back(coverage);
		}

		tiles.Stats.DroppedCommands = static_cast<uint32_t>(commands.size() - coverages.size());
		if (tiles.Stats.DroppedCommands > 0)
		{
			tiles.Problems.push_back("The painter's budget ran out: " + std::to_string(tiles.Stats.DroppedCommands) + " of " +
				std::to_string(commands.size()) + " commands were dropped (at most " + std::to_string(budget.MaxCommands) +
				" commands and " + std::to_string(budget.MaxTileEntries) + " tile entries per frame)");
		}

		// Each tile's commands start where the previous tile's end.
		uint32_t nextEntry = 0;
		for (PainterTile& tile : tiles.Tiles)
		{
			tile.FirstEntry = nextEntry;
			nextEntry += tile.Count;
			if (tile.Count > 0)
			{
				++tiles.Stats.TilesInUse;
				tiles.Stats.MaxCommandsPerTile = std::max(tiles.Stats.MaxCommandsPerTile, tile.Count);
			}
		}
		tiles.Stats.TileEntries = nextEntry;

		// Pass 2: write each command's index into its tiles. Commands are walked in order, so each
		// tile lists its commands in drawing order.
		tiles.Entries.resize(nextEntry);
		std::vector<uint32_t> written(tiles.Tiles.size(), 0);
		for (uint32_t commandIndex = 0; commandIndex < coverages.size(); ++commandIndex)
		{
			ForEachTile(coverages[commandIndex], tiles.TilesX, tiles.TilesY, [&](uint32_t tileIndex)
				{
					const PainterTile& tile = tiles.Tiles[tileIndex];
					tiles.Entries[tile.FirstEntry + written[tileIndex]] = commandIndex;
					++written[tileIndex];
				});
		}

		const std::chrono::duration<float, std::milli> binningTime = std::chrono::steady_clock::now() - startTime;
		tiles.Stats.BinningMilliseconds = binningTime.count();
	}
}
