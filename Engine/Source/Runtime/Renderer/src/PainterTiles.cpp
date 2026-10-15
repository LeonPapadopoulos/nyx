#include "PainterTiles.h"

#include "Painter.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <span>
#include <string>

namespace Nyx::Engine
{
	namespace
	{
		constexpr float TileSize = static_cast<float>(PainterTileSize);

		// How many tiles a rect touches, in a grid of tilesX by tilesY from the target's top left.
		// A tile x covers pixels from x * 8 up to, but not including, (x + 1) * 8.
		uint64_t CountTiles(const PainterRect& rect, uint32_t tilesX, uint32_t tilesY)
		{
			if (rect.IsEmpty() || tilesX == 0 || tilesY == 0)
			{
				return 0;
			}
			const float minX = std::max(std::floor(rect.Min.x / TileSize), 0.0f);
			const float minY = std::max(std::floor(rect.Min.y / TileSize), 0.0f);
			const float maxX = std::min(std::ceil(rect.Max.x / TileSize) - 1.0f, static_cast<float>(tilesX - 1));
			const float maxY = std::min(std::ceil(rect.Max.y / TileSize) - 1.0f, static_cast<float>(tilesY - 1));
			if (maxX < minX || maxY < minY)
			{
				return 0;
			}
			return static_cast<uint64_t>(maxX - minX + 1.0f) * static_cast<uint64_t>(maxY - minY + 1.0f);
		}

		// How far each group can reach: all of its shapes, plus its shadow and glow. Empty for a
		// group without shapes.
		std::vector<PainterRect> MeasureGroupReach(std::span<const PainterCommand> commands, const std::vector<PainterGroup>& groups)
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

		// The rect a command covers opaquely: an opaque, solid filled rect without outline, less its
		// largest rounding (a tile inside that misses its rounded corners), cut by its clip rect.
		// Empty for anything else.
		PainterRect MeasureHiding(const PainterCommand& command)
		{
			if (static_cast<EPainterCommandType>(command.Type) != EPainterCommandType::Rect || command.OutlineWidth > 0.0f ||
				static_cast<EPainterFillMode>(command.FillMode) != EPainterFillMode::Solid)
			{
				return PainterRect{};
			}
			const bool bOpaque = (command.Color >> 24) == 0xFFu;
			if (!bOpaque)
			{
				return PainterRect{};
			}
			const PainterRectPayload payload = GetPayload<PainterRectPayload>(command);
			const float rounding = std::max({ payload.Radii.TopLeft, payload.Radii.TopRight, payload.Radii.BottomRight, payload.Radii.BottomLeft });
			const PainterRect inside{ payload.Rect.Min + glm::vec2(rounding), payload.Rect.Max - glm::vec2(rounding) };
			return inside.IsEmpty() ? PainterRect{} : Intersect(inside, command.ClipRect);
		}

		PainterBinCommand MeasureCommand(const PainterCommand& command, const std::vector<PainterRect>& groupReach, const PainterRect& target)
		{
			const bool bInGroup = command.Group < groupReach.size();
			const PainterRect reach = bInGroup ? groupReach[command.Group] : GetShapeBounds(command);

			PainterBinCommand measured;
			measured.Reach = Intersect(Intersect(Grow(reach, PainterAntiAliasingReach), command.ClipRect), target);
			if (measured.Reach.IsEmpty())
			{
				// It touches nothing, so it hides nothing either.
				measured.Reach = PainterRect{};
				return measured;
			}
			measured.Hides = MeasureHiding(command);

			// A line in a group covers the group's reach like any other shape: its shadow and
			// glow spread around the whole group.
			if (!bInGroup && static_cast<EPainterCommandType>(command.Type) == EPainterCommandType::Line)
			{
				const auto line = GetPayload<PainterLinePayload>(command);
				measured.LineFrom = line.From;
				measured.LineTo = line.To;
				measured.LineReach = line.Width * 0.5f + PainterAntiAliasingReach;
			}
			return measured;
		}
	}

	void MeasurePainterSegment(const Painter& painter, const PainterSegment& segment, bool bWholeTarget, const PainterBudget& budget,
		PainterMeasuredSegment& inOutSegment)
	{
		const auto startTime = std::chrono::steady_clock::now();
		PainterMeasuredSegment& measured = inOutSegment;
		measured.Commands.clear();
		measured.Problems.clear();
		measured.Stats = PainterBinningStats{};
		measured.MaxEntries = 0;

		// A segment reaching past the painter's commands is cut to those it has.
		const std::span<const PainterCommand> allCommands = painter.GetCommands();
		const size_t first = std::min<size_t>(segment.FirstCommand, allCommands.size());
		const size_t count = std::min<size_t>(segment.CommandCount, allCommands.size() - first);
		const std::span<const PainterCommand> commands = allCommands.subspan(first, count);
		if (count != segment.CommandCount)
		{
			measured.Problems.push_back("A segment of " + std::to_string(segment.CommandCount) + " commands from command " + std::to_string(segment.FirstCommand) +
				" reaches past the painter's " + std::to_string(allCommands.size()) + " commands; only those it has were binned");
		}
		measured.Stats.Commands = static_cast<uint32_t>(commands.size());

		// Each command, as far as the budget's number of commands goes
		const PainterRect target{ glm::vec2(0.0f), glm::vec2(static_cast<float>(painter.GetTargetWidth()), static_cast<float>(painter.GetTargetHeight())) };
		const std::vector<PainterRect> groupReach = MeasureGroupReach(commands, painter.GetGroups());
		const size_t binned = std::min<size_t>(commands.size(), budget.MaxCommands);
		measured.Commands.reserve(binned);
		for (size_t index = 0; index < binned; ++index)
		{
			measured.Commands.push_back(MeasureCommand(commands[index], groupReach, target));
			measured.Stats.CulledCommands += measured.Commands.back().Reach.IsEmpty() ? 1 : 0;
		}
		measured.Stats.DroppedCommands = static_cast<uint32_t>(commands.size() - binned);
		if (measured.Stats.DroppedCommands > 0)
		{
			measured.Problems.push_back("The painter's budget ran out: " + std::to_string(measured.Stats.DroppedCommands) + " of " + std::to_string(commands.size()) +
				" commands were dropped (at most " + std::to_string(budget.MaxCommands) + " commands per frame or segment)");
		}

		// The grid: whole tiles over the target, or over every pixel the commands can touch
		PainterRect area = target;
		if (!bWholeTarget)
		{
			area = PainterRect{};
			for (const PainterBinCommand& command : measured.Commands)
			{
				if (!command.Reach.IsEmpty())
				{
					area = area.IsEmpty() ? command.Reach : Union(area, command.Reach);
				}
			}
		}
		const glm::vec2 origin = area.IsEmpty() ? glm::vec2(0.0f) : glm::floor(area.Min / TileSize) * TileSize;
		measured.OriginX = static_cast<uint32_t>(origin.x);
		measured.OriginY = static_cast<uint32_t>(origin.y);
		measured.TilesX = area.IsEmpty() ? 0 : static_cast<uint32_t>(std::ceil((area.Max.x - origin.x) / TileSize));
		measured.TilesY = area.IsEmpty() ? 0 : static_cast<uint32_t>(std::ceil((area.Max.y - origin.y) / TileSize));

		// The most entries: each command in every tile of its reach, before the reduction
		for (const PainterBinCommand& command : measured.Commands)
		{
			measured.MaxEntries += CountTiles(Move(command.Reach, -origin), measured.TilesX, measured.TilesY);
		}

		const std::chrono::duration<float, std::milli> measuringTime = std::chrono::steady_clock::now() - startTime;
		measured.Stats.BinningMilliseconds = measuringTime.count();
	}
}
