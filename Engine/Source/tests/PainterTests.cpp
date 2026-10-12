// The painter's CPU side: the commands and groups it records, its clip and transform stacks, the
// problems it reports, tile binning, and segments in ImGui windows. No GPU is involved.
#include "ImageFile.h"
#include "Painter.h"
#include "PainterTestSheet.h"
#include "PainterTiles.h"
#include "UIDiagnostics.h"
#include "WindowPainter.h"

#include <imgui_internal.h> // ImRect

#include <algorithm>
#include <utility>
#include <vector>

#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Engine;

	const glm::vec4 Orange{ 1.0f, 0.5f, 0.0f, 1.0f };
	const glm::vec4 Black{ 0.0f, 0.0f, 0.0f, 1.0f };

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) < 1e-4f;
	}

	bool Near(glm::vec2 a, glm::vec2 b)
	{
		return Near(a.x, b.x) && Near(a.y, b.y);
	}

	bool Near(const PainterRect& a, const PainterRect& b)
	{
		return Near(a.Min, b.Min) && Near(a.Max, b.Max);
	}

	EPainterCommandType TypeOf(const PainterCommand& command)
	{
		return static_cast<EPainterCommandType>(command.Type);
	}

	bool HasProblemContaining(const Painter& painter, const std::string& text)
	{
		for (const std::string& problem : painter.GetProblems())
		{
			if (problem.find(text) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	PainterEffects MakeShadow()
	{
		PainterEffects effects;
		effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		effects.Shadow.Offset = glm::vec2(0.0f, 4.0f);
		effects.Shadow.Blur = 6.0f;
		return effects;
	}

	PainterStyle WithShadow(const glm::vec4& color)
	{
		PainterStyle style = PainterStyle::Filled(color);
		style.Effects = MakeShadow();
		return style;
	}

	void TestColorsArePremultiplied()
	{
		// Red in the lowest byte, as GLSL's unpackUnorm4x8() reads it
		Require(PackPremultipliedColor(glm::vec4(1.0f, 0.0f, 0.0f, 1.0f)) == 0xFF0000FFu, "Opaque red packs wrong");

		// Half transparent: each colour channel is multiplied by the alpha.
		const uint32_t packed = PackPremultipliedColor(glm::vec4(1.0f, 0.5f, 0.0f, 0.5f));
		Require((packed & 0xFFu) == 128 && ((packed >> 8) & 0xFFu) == 64 && ((packed >> 16) & 0xFFu) == 0 && (packed >> 24) == 128,
			"A half transparent colour isn't premultiplied");

		Require(PackPremultipliedColor(glm::vec4(2.0f, -1.0f, 0.0f, 1.0f)) == 0xFF0000FFu, "Channels outside 0 to 1 should be clamped");

		const glm::vec4 unpacked = UnpackPremultipliedColor(PackPremultipliedColor(glm::vec4(1.0f, 1.0f, 1.0f, 0.5f)));
		Require(std::abs(unpacked.r - 0.5f) < 0.01f && std::abs(unpacked.a - 0.5f) < 0.01f, "Unpacking doesn't give the premultiplied colour back");
	}

	void TestShapesAreRecordedInOrder()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRect(PainterRect{ { 10, 10 }, { 50, 30 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 100, 50 }, 20.0f, PainterStyle::Outlined(Black, 2.0f));
		painter.AddLine({ 0, 0 }, { 30, 40 }, 3.0f, EPainterLineCap::Round, PainterStyle::Filled(Black));
		painter.AddTriangle({ 0, 0 }, { 10, 0 }, { 0, 10 }, PainterStyle::Filled(Orange));
		painter.AddCircleSector({ 50, 50 }, 10.0f, 20.0f, 0.0f, 1.0f, PainterStyle::Filled(Orange));
		painter.EndFrame();

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 5, "Expected 5 commands, got " + std::to_string(commands.size()));
		Require(TypeOf(commands[0]) == EPainterCommandType::Rect && TypeOf(commands[1]) == EPainterCommandType::Circle &&
				TypeOf(commands[2]) == EPainterCommandType::Line && TypeOf(commands[3]) == EPainterCommandType::Triangle &&
				TypeOf(commands[4]) == EPainterCommandType::CircleSector,
			"Commands aren't in the order they were added");

		const auto rect = GetPayload<PainterRectPayload>(commands[0]);
		Require(Near(rect.Rect, PainterRect{ { 10, 10 }, { 50, 30 } }) && rect.Radii == PainterCornerRadii{}, "AddRect() should record a rect with sharp corners");
		Require(commands[0].Color == PackPremultipliedColor(Orange), "The rect has the wrong colour");
		Require(commands[0].OutlineWidth == 0.0f, "A filled shape should have no outline width");
		Require(commands[0].Group == PainterNoGroup, "A shape without effects should belong to no group");
		Require(Near(commands[0].ClipRect, PainterRect{ { 0, 0 }, { 200, 100 } }), "Without a pushed clip rect, a command should be clipped to the target");

		const auto circle = GetPayload<PainterCirclePayload>(commands[1]);
		Require(Near(circle.Center, { 100, 50 }) && Near(circle.Radius, 20.0f), "The circle's payload is wrong");
		Require(commands[1].OutlineWidth == 2.0f, "An outlined shape should keep its outline width");

		const auto line = GetPayload<PainterLinePayload>(commands[2]);
		Require(Near(line.From, { 0, 0 }) && Near(line.To, { 30, 40 }) && Near(line.Width, 3.0f), "The line's payload is wrong");
		Require(commands[2].Flags == static_cast<uint32_t>(EPainterCommandFlags::RoundCaps), "A round-capped line should have the RoundCaps flag");

		Require(painter.GetProblems().empty(), "Correct use shouldn't report problems");
	}

	void TestRoundedRectRadiiFitTheRect()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRoundedRect(PainterRect{ { 0, 0 }, { 100, 20 } }, PainterCornerRadii{ 4, 50, -3, 10 }, PainterStyle::Filled(Orange));
		// Given the other way round, a rect means the same area.
		painter.AddRect(PainterRect{ { 50, 30 }, { 10, 10 } }, PainterStyle::Filled(Orange));

		const auto radii = GetPayload<PainterRectPayload>(painter.GetCommands()[0]).Radii;
		Require(Near(radii.TopLeft, 4.0f), "A radius that fits should stay");
		Require(Near(radii.TopRight, 10.0f), "A radius larger than half the shorter side should shrink to it");
		Require(Near(radii.BottomRight, 0.0f), "A negative radius should become 0");
		Require(Near(radii.BottomLeft, 10.0f), "A radius of exactly half the shorter side should stay");

		const auto flipped = GetPayload<PainterRectPayload>(painter.GetCommands()[1]).Rect;
		Require(Near(flipped, PainterRect{ { 10, 10 }, { 50, 30 } }), "A rect given the other way round should be normalized");
	}

	void TestArcsAndSectors()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddArc({ 50, 50 }, 20.0f, 4.0f, 0.0f, 3.0f, PainterStyle::Filled(Orange));
		painter.AddCircleSector({ 50, 50 }, 30.0f, 10.0f, 2.0f, 1.0f, PainterStyle::Filled(Orange));

		const auto arc = GetPayload<PainterCircleSectorPayload>(painter.GetCommands()[0]);
		Require(Near(arc.InnerRadius, 18.0f) && Near(arc.OuterRadius, 22.0f), "An arc should be a sector as wide as the arc, centred on its radius");
		Require(Near(arc.StartAngle, 0.0f) && Near(arc.EndAngle, 3.0f), "An arc should keep its angles");

		const auto swapped = GetPayload<PainterCircleSectorPayload>(painter.GetCommands()[1]);
		Require(Near(swapped.InnerRadius, 10.0f) && Near(swapped.OuterRadius, 30.0f), "Radii given the other way round should be swapped");
		Require(Near(swapped.StartAngle, 1.0f) && Near(swapped.EndAngle, 2.0f), "Angles given the other way round should be swapped");
	}

	void TestLinesIgnoreOutlineWidth()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddLine({ 0, 0 }, { 10, 0 }, 2.0f, EPainterLineCap::Flat, PainterStyle::Outlined(Black, 5.0f));

		const PainterCommand& line = painter.GetCommands()[0];
		Require(line.OutlineWidth == 0.0f, "A line's outline width should be dropped");
		Require(line.Flags == static_cast<uint32_t>(EPainterCommandFlags::None), "A flat-capped line should have no flags");
	}

	void TestClipRectsNest()
	{
		Painter painter;
		painter.BeginFrame(200, 100);

		painter.PushClipRect(PainterRect{ { 10, 10 }, { 150, 90 } });
		painter.PushClipRect(PainterRect{ { 100, 0 }, { 300, 50 } });
		painter.AddCircle({ 0, 0 }, 1.0f, PainterStyle::Filled(Orange));
		Require(Near(painter.GetCommands()[0].ClipRect, PainterRect{ { 100, 10 }, { 150, 50 } }), "A nested clip rect should be cut by the outer one");

		painter.PopClipRect();
		painter.AddCircle({ 0, 0 }, 1.0f, PainterStyle::Filled(Orange));
		Require(Near(painter.GetCommands()[1].ClipRect, PainterRect{ { 10, 10 }, { 150, 90 } }), "PopClipRect() should bring back the outer clip rect");

		// Clip rects that don't overlap leave nothing to draw.
		painter.PushClipRect(PainterRect{ { 160, 0 }, { 200, 10 } });
		Require(painter.GetClipRect().IsEmpty(), "Clip rects that don't overlap should give an empty clip rect");
		painter.PopClipRect();

		painter.PopClipRect();
		Require(painter.GetProblems().empty(), "Balanced clip rects shouldn't report problems");

		painter.PopClipRect();
		painter.PopClipRect();
		Require(painter.GetProblems().size() == 1 && HasProblemContaining(painter, "PopClipRect() without"),
			"An extra PopClipRect() should be reported once, however often it happens");
		Require(Near(painter.GetClipRect(), PainterRect{ { 0, 0 }, { 200, 100 } }), "An extra PopClipRect() should keep the whole target");
	}

	void TestTransformsScaleAndMoveEverything()
	{
		Painter painter;
		painter.BeginFrame(400, 400);

		painter.PushTransform(2.0f, { 10, 20 });
		painter.AddRoundedRect(PainterRect{ { 1, 1 }, { 11, 6 } }, 2.0f, PainterStyle::Outlined(Orange, 1.5f));
		painter.AddLine({ 0, 0 }, { 5, 0 }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(Black));
		painter.AddCircle({ 0, 0 }, 3.0f, WithShadow(Black));

		const auto rect = GetPayload<PainterRectPayload>(painter.GetCommands()[0]);
		Require(Near(rect.Rect, PainterRect{ { 12, 22 }, { 32, 32 } }), "A rect should be scaled, then moved");
		Require(Near(rect.Radii.TopLeft, 4.0f), "Corner radii should scale");
		Require(Near(painter.GetCommands()[0].OutlineWidth, 3.0f), "Outline widths should scale");

		const auto line = GetPayload<PainterLinePayload>(painter.GetCommands()[1]);
		Require(Near(line.From, { 10, 20 }) && Near(line.To, { 20, 20 }) && Near(line.Width, 2.0f), "Lines should be scaled and moved");

		const PainterGroup& group = painter.GetGroups()[painter.GetCommands()[2].Group];
		Require(Near(group.ShadowOffset, { 0, 8 }) && Near(group.ShadowBlur, 12.0f), "Shadow offsets and blur should scale, but not move");

		// A nested transform applies inside the outer one.
		painter.PushTransform(0.5f, { 4, 0 });
		painter.AddCircle({ 2, 2 }, 4.0f, PainterStyle::Filled(Orange));
		const auto circle = GetPayload<PainterCirclePayload>(painter.GetCommands()[3]);
		Require(Near(circle.Center, { 20, 22 }) && Near(circle.Radius, 4.0f), "Nested transforms should combine, the inner one first");

		// Clip rects are pushed in the current transform's units too.
		painter.PushClipRect(PainterRect{ { 0, 0 }, { 10, 10 } });
		Require(Near(painter.GetClipRect(), PainterRect{ { 18, 20 }, { 28, 30 } }), "A clip rect should be transformed when it is pushed");
		painter.PopClipRect();

		painter.PopTransform();
		painter.PopTransform();
		painter.AddCircle({ 2, 2 }, 4.0f, PainterStyle::Filled(Orange));
		const auto untransformed = GetPayload<PainterCirclePayload>(painter.GetCommands()[4]);
		Require(Near(untransformed.Center, { 2, 2 }) && Near(untransformed.Radius, 4.0f), "Popping every transform should leave target pixels");
		Require(painter.GetProblems().empty(), "Balanced transforms shouldn't report problems");

		painter.PushTransform(0.0f, { 5, 5 });
		Require(HasProblemContaining(painter, "scale of 0 or less"), "A scale of 0 should be reported");
		Require(painter.GetTransform().Scale == 1.0f && painter.GetTransform().Offset == glm::vec2(0.0f), "A scale of 0 should be ignored");
		painter.PopTransform();
		Require(!HasProblemContaining(painter, "PopTransform() without"), "The PopTransform() after an ignored PushTransform() should still match it");
	}

	void TestGroupsShareEffects()
	{
		Painter painter;
		painter.BeginFrame(200, 100);

		painter.BeginGroup(MakeShadow());
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 20, 5 }, 5.0f, PainterStyle::Filled(Orange));
		painter.EndGroup();
		painter.AddCircle({ 50, 5 }, 5.0f, PainterStyle::Filled(Orange));

		const auto& commands = painter.GetCommands();
		Require(painter.GetGroups().size() == 1, "One BeginGroup() should make one group");
		Require(commands[0].Group == 0 && commands[1].Group == 0, "Shapes inside a group should belong to it");
		Require(commands[2].Group == PainterNoGroup, "A shape after EndGroup() should belong to no group");

		const PainterGroup& group = painter.GetGroups()[0];
		Require(group.ShadowColor == PackPremultipliedColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.5f)), "The group's shadow colour is wrong");
		Require(group.GlowColor == 0 && group.GlowDistance == 0.0f, "A group without a glow should have none");
		Require(painter.GetProblems().empty(), "Correct groups shouldn't report problems");
	}

	void TestShapeWithEffectsIsAGroupOfOne()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, WithShadow(Orange));
		painter.AddRect(PainterRect{ { 20, 0 }, { 30, 10 } }, WithShadow(Orange));

		Require(painter.GetGroups().size() == 2, "Each shape with its own shadow should get its own group");
		Require(painter.GetCommands()[0].Group == 0 && painter.GetCommands()[1].Group == 1, "The shapes should point at their own groups");

		// A glow that reaches nowhere is no glow.
		PainterStyle noGlow = PainterStyle::Filled(Orange);
		noGlow.Effects.Glow.Color = Orange;
		noGlow.Effects.Glow.Distance = 0.0f;
		painter.AddRect(PainterRect{ { 40, 0 }, { 50, 10 } }, noGlow);
		Require(painter.GetCommands()[2].Group == PainterNoGroup, "A glow with no distance shouldn't make a group");
	}

	void TestGroupMisuseIsReported()
	{
		Painter painter;
		painter.BeginFrame(200, 100);

		painter.BeginGroup(MakeShadow());
		painter.BeginGroup(PainterEffects{});
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, WithShadow(Orange));
		painter.EndGroup();
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));
		painter.EndGroup();
		painter.EndGroup();

		Require(painter.GetGroups().size() == 1, "A nested BeginGroup() shouldn't make a group");
		Require(painter.GetCommands()[0].Group == 0 && painter.GetCommands()[1].Group == 0,
			"Shapes up to the outer EndGroup() should stay in the outer group");
		Require(HasProblemContaining(painter, "groups don't nest"), "A nested BeginGroup() should be reported");
		Require(HasProblemContaining(painter, "has its own shadow or glow"), "Effects on a shape inside a group should be reported");
		Require(HasProblemContaining(painter, "EndGroup() without"), "An EndGroup() too many should be reported");
		Require(painter.GetProblems().size() == 3, "Each problem should be reported once");
	}

	void TestEndFrameClosesWhatIsLeftOpen()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.BeginGroup(MakeShadow());
		painter.PushClipRect(PainterRect{ { 0, 0 }, { 10, 10 } });
		painter.PushTransform(2.0f, { 0, 0 });
		painter.EndFrame();

		Require(HasProblemContaining(painter, "BeginGroup() without an EndGroup()"), "A group left open should be reported");
		Require(HasProblemContaining(painter, "PushClipRect() without a PopClipRect()"), "A clip rect left pushed should be reported");
		Require(HasProblemContaining(painter, "PushTransform() without a PopTransform()"), "A transform left pushed should be reported");
		Require(Near(painter.GetClipRect(), PainterRect{ { 0, 0 }, { 200, 100 } }) && painter.GetTransform().Scale == 1.0f,
			"EndFrame() should close the stacks");

		// The next frame starts clean.
		painter.BeginFrame(50, 50);
		painter.AddCircle({ 0, 0 }, 1.0f, PainterStyle::Filled(Orange));
		Require(painter.GetCommands().size() == 1 && painter.GetGroups().empty() && painter.GetProblems().empty(),
			"BeginFrame() should drop the previous frame");
		Require(painter.GetCommands()[0].Group == PainterNoGroup, "No group should stay open into the next frame");
		Require(Near(painter.GetCommands()[0].ClipRect, PainterRect{ { 0, 0 }, { 50, 50 } }), "The clip rect should be the new target");
	}

	void TestShapeBounds()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRect(PainterRect{ { 10, 10 }, { 50, 30 } }, PainterStyle::Outlined(Orange, 4.0f));
		painter.AddCircle({ 100, 50 }, 20.0f, PainterStyle::Filled(Orange));
		painter.AddCircleSector({ 100, 50 }, 5.0f, 10.0f, 0.0f, 0.5f, PainterStyle::Filled(Orange));
		painter.AddLine({ 10, 40 }, { 0, 20 }, 6.0f, EPainterLineCap::Round, PainterStyle::Filled(Black));
		painter.AddTriangle({ 5, 0 }, { 0, 10 }, { 20, 8 }, PainterStyle::Filled(Orange));

		const auto& commands = painter.GetCommands();
		Require(Near(GetShapeBounds(commands[0]), PainterRect{ { 8, 8 }, { 52, 32 } }), "A rect's bounds should include half its outline");
		Require(Near(GetShapeBounds(commands[1]), PainterRect{ { 80, 30 }, { 120, 70 } }), "A circle's bounds are wrong");
		Require(Near(GetShapeBounds(commands[2]), PainterRect{ { 90, 40 }, { 110, 60 } }), "A sector's bounds should be its whole outer circle");
		Require(Near(GetShapeBounds(commands[3]), PainterRect{ { -3, 17 }, { 13, 43 } }), "A line's bounds should reach half its width past its ends");
		Require(Near(GetShapeBounds(commands[4]), PainterRect{ { 0, 0 }, { 20, 10 } }), "A triangle's bounds are wrong");
	}

	// Tile binning

	using TileCoordinates = std::pair<uint32_t, uint32_t>;

	PainterTiles Bin(const Painter& painter, const PainterBudget& budget = PainterBudget{})
	{
		PainterTiles tiles;
		BinPainterCommands(painter, budget, tiles);
		return tiles;
	}

	std::vector<uint32_t> CommandsInTile(const PainterTiles& tiles, uint32_t tileX, uint32_t tileY)
	{
		const PainterTile& tile = tiles.GetTile(tileX, tileY);
		return std::vector<uint32_t>(tiles.Entries.begin() + tile.FirstEntry, tiles.Entries.begin() + tile.FirstEntry + tile.Count);
	}

	bool TileHasCommand(const PainterTiles& tiles, uint32_t tileX, uint32_t tileY, uint32_t commandIndex)
	{
		const std::vector<uint32_t> commands = CommandsInTile(tiles, tileX, tileY);
		return std::find(commands.begin(), commands.end(), commandIndex) != commands.end();
	}

	// Every tile that holds the command, row by row
	std::vector<TileCoordinates> TilesOfCommand(const PainterTiles& tiles, uint32_t commandIndex)
	{
		std::vector<TileCoordinates> result;
		for (uint32_t tileY = 0; tileY < tiles.TilesY; ++tileY)
		{
			for (uint32_t tileX = 0; tileX < tiles.TilesX; ++tileX)
			{
				if (TileHasCommand(tiles, tileX, tileY, commandIndex))
				{
					result.emplace_back(tileX, tileY);
				}
			}
		}
		return result;
	}

	// The tile a pixel position lies in
	TileCoordinates TileAt(glm::vec2 position)
	{
		return TileCoordinates{ static_cast<uint32_t>(position.x) / PainterTileSize, static_cast<uint32_t>(position.y) / PainterTileSize };
	}

	void TestGridCoversTheTarget()
	{
		Painter painter;
		painter.BeginFrame(100, 50);
		const PainterTiles tiles = Bin(painter);
		Require(tiles.TilesX == 13 && tiles.TilesY == 7, "A 100x50 target should have 13x7 tiles, the last ones sticking out");
		Require(tiles.Tiles.size() == 13 * 7, "There should be one tile per grid cell");
		Require(tiles.Entries.empty() && tiles.Stats.TilesInUse == 0, "An empty frame should have no entries");
	}

	void TestShapesLandInTheirTiles()
	{
		Painter painter;
		painter.BeginFrame(100, 100);
		// With one pixel of anti-aliasing, this rect covers pixels 8 to 24 across and 8 to 16 down.
		painter.AddRect(PainterRect{ { 9, 9 }, { 23, 15 } }, PainterStyle::Filled(Orange));
		// Covers pixels 35 to 45 both ways: tiles 4 and 5.
		painter.AddCircle({ 40, 40 }, 4.0f, PainterStyle::Filled(Orange));
		const PainterTiles tiles = Bin(painter);

		Require(TilesOfCommand(tiles, 0) == std::vector<TileCoordinates>{ { 1, 1 }, { 2, 1 } }, "The rect should land in exactly tiles (1,1) and (2,1)");
		Require(TilesOfCommand(tiles, 1) == std::vector<TileCoordinates>{ { 4, 4 }, { 5, 4 }, { 4, 5 }, { 5, 5 } },
			"The circle should land in exactly tiles (4..5, 4..5)");
		Require(tiles.Stats.CulledCommands == 0 && tiles.Stats.DroppedCommands == 0 && tiles.Problems.empty(), "Nothing should be culled or dropped");
	}

	void TestCommandsThatTouchNothingAreCulled()
	{
		Painter painter;
		painter.BeginFrame(100, 100);
		painter.AddRect(PainterRect{ { -50, -50 }, { -20, -20 } }, PainterStyle::Filled(Orange));
		painter.PushClipRect(PainterRect{ { 0, 0 }, { 10, 10 } });
		painter.AddCircle({ 80, 80 }, 5.0f, PainterStyle::Filled(Orange));
		painter.PopClipRect();
		painter.AddCircle({ 50, 50 }, 5.0f, PainterStyle::Filled(Orange));
		const PainterTiles tiles = Bin(painter);

		Require(tiles.Stats.CulledCommands == 2, "Off-target and clipped-away commands should be culled, got " + std::to_string(tiles.Stats.CulledCommands));
		Require(TilesOfCommand(tiles, 0).empty() && TilesOfCommand(tiles, 1).empty(), "Culled commands should be in no tile");
		Require(!TilesOfCommand(tiles, 2).empty(), "A visible command after culled ones should still be binned");
		Require(tiles.Problems.empty(), "Culling is not a problem");

		// A target without pixels has no tiles at all.
		painter.BeginFrame(0, 0);
		painter.AddCircle({ 0, 0 }, 5.0f, PainterStyle::Filled(Orange));
		const PainterTiles empty = Bin(painter);
		Require(empty.Tiles.empty() && empty.Entries.empty() && empty.Stats.CulledCommands == 1, "A 0x0 target should cull everything");
	}

	void TestClipRectCutsCoverage()
	{
		Painter painter;
		painter.BeginFrame(100, 100);
		painter.PushClipRect(PainterRect{ { 16, 16 }, { 32, 32 } });
		painter.AddRect(PainterRect{ { 0, 0 }, { 100, 100 } }, WithShadow(Orange));
		painter.PopClipRect();
		const PainterTiles tiles = Bin(painter);

		Require(TilesOfCommand(tiles, 0) == std::vector<TileCoordinates>{ { 2, 2 }, { 3, 2 }, { 2, 3 }, { 3, 3 } },
			"A command should only cover its clip rect's tiles, shadow included");
	}

	void TestTilesKeepDrawingOrder()
	{
		Painter painter;
		painter.BeginFrame(64, 64);
		painter.AddRect(PainterRect{ { 0, 0 }, { 16, 16 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 4, 4 }, 2.0f, PainterStyle::Filled(Black));
		painter.AddRect(PainterRect{ { 1, 1 }, { 6, 6 } }, PainterStyle::Filled(Orange));
		const PainterTiles tiles = Bin(painter);

		Require(CommandsInTile(tiles, 0, 0) == std::vector<uint32_t>{ 0, 1, 2 }, "A tile should list its commands in drawing order");

		// The first rect covers pixels -1 to 17 each way: tiles 0 to 2, nine of them. The two
		// small shapes only add to tile (0,0).
		Require(tiles.Stats.TilesInUse == 9 && tiles.Stats.TileEntries == 11 && tiles.Stats.MaxCommandsPerTile == 3,
			"The statistics are wrong");
		Require(Near(tiles.Stats.GetAverageCommandsPerTileInUse(), 11.0f / 9.0f), "The average per tile in use is wrong");
	}

	// Checks that every tile the line's pixels can be in holds the line, by walking along it
	// and across it in small steps.
	void RequireLineCovered(const PainterTiles& tiles, uint32_t commandIndex, glm::vec2 from, glm::vec2 to, float width, const std::string& name)
	{
		const glm::vec2 along = to - from;
		const glm::vec2 across = glm::normalize(glm::vec2(-along.y, along.x));
		for (float t = 0.0f; t <= 1.0f; t += 0.0005f)
		{
			for (float side = -0.5f; side <= 0.5f; side += 0.25f)
			{
				const glm::vec2 point = from + along * t + across * (side * width);
				if (point.x < 0.0f || point.y < 0.0f || point.x >= tiles.TilesX * 8.0f || point.y >= tiles.TilesY * 8.0f)
				{
					continue;
				}
				const TileCoordinates tile = TileAt(point);
				Require(TileHasCommand(tiles, tile.first, tile.second, commandIndex),
					name + " is missing from tile (" + std::to_string(tile.first) + "," + std::to_string(tile.second) + ")");
			}
		}
	}

	void TestLinesWalkTheirTiles()
	{
		Painter painter;
		painter.BeginFrame(800, 800);
		painter.AddLine({ 0, 0 }, { 800, 800 }, 2.0f, EPainterLineCap::Flat, PainterStyle::Filled(Black));
		painter.AddLine({ 13, 700 }, { 790, 610 }, 5.0f, EPainterLineCap::Round, PainterStyle::Filled(Black));
		painter.AddLine({ 400, 3 }, { 430, 797 }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(Black));
		const PainterTiles tiles = Bin(painter);

		// Its bounding box would be all 100x100 tiles; along the diagonal it needs about three per row.
		const size_t diagonalTiles = TilesOfCommand(tiles, 0).size();
		Require(diagonalTiles < 400, "A diagonal line should touch a thin band of tiles, got " + std::to_string(diagonalTiles));

		RequireLineCovered(tiles, 0, { 0, 0 }, { 800, 800 }, 2.0f, "The diagonal line");
		RequireLineCovered(tiles, 1, { 13, 700 }, { 790, 610 }, 5.0f, "The shallow line");
		RequireLineCovered(tiles, 2, { 400, 3 }, { 430, 797 }, 1.0f, "The steep line");

		// A horizontal line, 2 wide at y = 4: pixels 3 to 5, plus anti-aliasing 2 to 6. One row of tiles.
		painter.BeginFrame(100, 100);
		painter.AddLine({ 10, 4 }, { 70, 4 }, 2.0f, EPainterLineCap::Flat, PainterStyle::Filled(Black));
		const PainterTiles horizontal = Bin(painter);
		const std::vector<TileCoordinates> horizontalTiles = TilesOfCommand(horizontal, 0);
		Require(horizontalTiles.size() == 8 && horizontalTiles.front() == TileCoordinates{ 1, 0 } && horizontalTiles.back() == TileCoordinates{ 8, 0 },
			"A horizontal line should cover tiles 1 to 8 of row 0");
	}

	void TestGroupsCoverTheirWholeReach()
	{
		Painter painter;
		painter.BeginFrame(400, 400);

		// Two shapes far apart with one shadow, 40 pixels below them
		PainterEffects shadow;
		shadow.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		shadow.Shadow.Offset = glm::vec2(0.0f, 40.0f);
		shadow.Shadow.Blur = 4.0f;
		painter.BeginGroup(shadow);
		painter.AddRect(PainterRect{ { 0, 0 }, { 16, 16 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 100, 8 }, 4.0f, PainterStyle::Filled(Orange));
		painter.EndGroup();

		// A circle with a glow of its own, reaching 10 pixels past its edge
		PainterStyle glowing = PainterStyle::Filled(Orange);
		glowing.Effects.Glow.Color = Orange;
		glowing.Effects.Glow.Distance = 10.0f;
		painter.AddCircle({ 200, 200 }, 4.0f, glowing);

		// A line in a group without effects covers the group's bounds, not just its own band.
		painter.BeginGroup(PainterEffects{});
		painter.AddLine({ 0, 300 }, { 80, 380 }, 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(Black));
		painter.EndGroup();

		const PainterTiles tiles = Bin(painter);

		// The tile of pixel (12, 50) only lies in the rect's shadow, but the shader needs both
		// shapes of the group there.
		const TileCoordinates inShadow = TileAt({ 12, 50 });
		Require(TileHasCommand(tiles, inShadow.first, inShadow.second, 0) && TileHasCommand(tiles, inShadow.first, inShadow.second, 1),
			"A tile in a group's shadow should hold every shape of the group");
		const TileCoordinates belowShadow = TileAt({ 12, 90 });
		Require(CommandsInTile(tiles, belowShadow.first, belowShadow.second).empty(), "A tile past the shadow should hold nothing");

		const TileCoordinates inGlow = TileAt({ 212, 200 });
		Require(TileHasCommand(tiles, inGlow.first, inGlow.second, 2), "A tile in a shape's glow should hold the shape");

		const TileCoordinates cornerOfLine = TileAt({ 76, 302 });
		Require(TileHasCommand(tiles, cornerOfLine.first, cornerOfLine.second, 3), "A line in a group should cover the group's whole bounds");
	}

	void TestBudgetDropsTheRestOfTheFrame()
	{
		Painter painter;
		painter.BeginFrame(64, 64);
		painter.AddCircle({ 4, 4 }, 2.0f, PainterStyle::Filled(Orange));
		painter.AddCircle({ 20, 20 }, 2.0f, PainterStyle::Filled(Orange));
		painter.AddCircle({ 40, 40 }, 2.0f, PainterStyle::Filled(Orange));

		PainterBudget fewCommands;
		fewCommands.MaxCommands = 2;
		const PainterTiles tiles = Bin(painter, fewCommands);
		Require(tiles.Stats.DroppedCommands == 1, "One command over the command budget should be dropped");
		Require(!TilesOfCommand(tiles, 1).empty() && TilesOfCommand(tiles, 2).empty(), "The commands within the budget should be binned, the rest not");
		Require(tiles.Problems.size() == 1 && tiles.Problems[0].find("budget ran out") != std::string::npos, "Running out of budget should be reported");

		// Each full-target rect needs all 64 tiles. Only the first fits in 100 entries; the small
		// circle after the second would fit, but the rest of the frame is dropped together.
		painter.BeginFrame(64, 64);
		painter.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(Orange));
		painter.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 4, 4 }, 2.0f, PainterStyle::Filled(Orange));

		PainterBudget fewEntries;
		fewEntries.MaxTileEntries = 100;
		const PainterTiles full = Bin(painter, fewEntries);
		Require(full.Stats.DroppedCommands == 2 && full.Stats.TileEntries == 64, "Commands past the entry budget should be dropped together");
	}

	void TestBinningReusesItsResult()
	{
		Painter painter;
		PainterTiles tiles;

		painter.BeginFrame(64, 64);
		painter.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(Orange));
		PainterBudget tight;
		tight.MaxCommands = 0;
		BinPainterCommands(painter, tight, tiles);
		Require(!tiles.Problems.empty(), "The first frame should report its dropped command");

		painter.BeginFrame(16, 8);
		painter.AddCircle({ 4, 4 }, 2.0f, PainterStyle::Filled(Orange));
		BinPainterCommands(painter, PainterBudget{}, tiles);
		Require(tiles.TilesX == 2 && tiles.TilesY == 1 && tiles.Tiles.size() == 2, "The grid should follow the new target");
		Require(tiles.Problems.empty() && tiles.Stats.DroppedCommands == 0 && tiles.Stats.Commands == 1, "Problems and statistics shouldn't carry over");
		Require(CommandsInTile(tiles, 0, 0) == std::vector<uint32_t>{ 0 } && CommandsInTile(tiles, 1, 0).empty(),
			"The previous frame's entries shouldn't carry over");
	}

	// Text and images

	// An ImGui context whose font atlas can bake glyphs at any size, as the editor's can once its
	// first frame has started
	class ImGuiFonts
	{
	public:
		ImGuiFonts()
		{
			Context = ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures;
			io.DisplaySize = ImVec2(64.0f, 64.0f);
			io.DeltaTime = 1.0f / 60.0f;
			// No imgui.ini next to wherever the tests run
			io.IniFilename = nullptr;
			Font = io.Fonts->AddFontDefault();
			ImGui::NewFrame();
		}

		~ImGuiFonts()
		{
			ImGui::EndFrame();
			ImGui::DestroyContext(Context);
		}

		ImGuiFonts(const ImGuiFonts&) = delete;
		ImGuiFonts& operator=(const ImGuiFonts&) = delete;

		ImFont* Font = nullptr;

	private:
		ImGuiContext* Context = nullptr;
	};

	PainterTexturedRectPayload TexturedRectOf(const PainterCommand& command)
	{
		return GetPayload<PainterTexturedRectPayload>(command);
	}

	void TestTextMakesOneGlyphPerVisibleCharacter(const ImGuiFonts& fonts)
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddText(fonts.Font, 13.0f, { 10.6f, 20.3f }, Black, "Hi yo");

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 4, "\"Hi yo\" should make 4 glyphs, the space none; got " + std::to_string(commands.size()));
		for (const PainterCommand& command : commands)
		{
			Require(TypeOf(command) == EPainterCommandType::Glyph && command.Texture == 0, "Text should make glyphs of the first texture");
			Require(command.Color == PackPremultipliedColor(Black), "Glyphs should have the text's colour");
		}
		Require(painter.GetTextures().size() == 1 && painter.GetTextures()[0]._TexData == fonts.Font->OwnerAtlas->TexRef._TexData,
			"The text's texture should be the font atlas");

		// The text starts on a whole pixel, as ImGui's does, so glyph texels land on pixels.
		const ImFontGlyph* h = fonts.Font->GetFontBaked(13.0f)->FindGlyph('H');
		const PainterTexturedRectPayload first = TexturedRectOf(commands[0]);
		Require(Near(first.Rect.Min, glm::vec2(10.0f + h->X0, 20.0f + h->Y0)) && Near(first.Rect.Max, glm::vec2(10.0f + h->X1, 20.0f + h->Y1)),
			"The first glyph should sit at the whole pixel the text starts on");
		Require(Near(first.UVs.Min, glm::vec2(h->U0, h->V0)) && Near(first.UVs.Max, glm::vec2(h->U1, h->V1)), "The glyph should use the atlas' texture coordinates");

		for (size_t index = 1; index < commands.size(); ++index)
		{
			Require(TexturedRectOf(commands[index]).Rect.Min.x > TexturedRectOf(commands[index - 1]).Rect.Min.x, "Glyphs should run left to right");
		}
		Require(painter.GetProblems().empty(), "Text shouldn't report problems");
	}

	void TestTextBreaksLines(const ImGuiFonts& fonts)
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddText(fonts.Font, 13.0f, { 10.0f, 10.0f }, Black, "H\r\nH");

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 2, "Line breaks shouldn't make glyphs");
		const PainterRect firstLine = TexturedRectOf(commands[0]).Rect;
		const PainterRect secondLine = TexturedRectOf(commands[1]).Rect;
		Require(Near(secondLine.Min, firstLine.Min + glm::vec2(0.0f, 13.0f)), "The second line should start one line height below the first, at the same x");
	}

	void TestZoomedTextIsBakedAtItsSize(const ImGuiFonts& fonts)
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.PushTransform(2.0f, { 0, 0 });
		painter.AddText(fonts.Font, 13.0f, { 5.0f, 5.0f }, Black, "H");
		painter.PopTransform();

		// Drawn twice as large, so baked at 26 pixels: its own glyph, not a stretched 13 pixel one
		const ImFontGlyph* h = fonts.Font->GetFontBaked(26.0f)->FindGlyph('H');
		const PainterTexturedRectPayload glyph = TexturedRectOf(painter.GetCommands()[0]);
		Require(Near(glyph.Rect.Min, glm::vec2(10.0f + h->X0, 10.0f + h->Y0)) && Near(glyph.Rect.Max, glm::vec2(10.0f + h->X1, 10.0f + h->Y1)),
			"Zoomed text should use the glyph baked at the zoomed size");
		Require(Near(glyph.UVs.Min, glm::vec2(h->U0, h->V0)), "Zoomed text should sample the glyph baked at the zoomed size");
	}

	void TestTextWithoutAFontIsReported()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddText(nullptr, 13.0f, { 0, 0 }, Black, "Hi");
		Require(painter.GetCommands().empty() && HasProblemContaining(painter, "without a font"), "Text without a font should be reported and dropped");
	}

	ImTextureRef FakeTexture(uint64_t id)
	{
		return ImTextureRef(static_cast<ImTextureID>(id));
	}

	void TestImages()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.PushTransform(2.0f, { 10, 0 });
		painter.AddImage(FakeTexture(100), PainterRect{ { 0, 0 }, { 16, 8 } }, PainterStyle::Outlined(Orange, 3.0f));
		painter.PopTransform();
		painter.AddImage(FakeTexture(200), PainterRect{ { 0, 0 }, { 4, 4 } }, WithShadow(glm::vec4(1.0f)), PainterRect{ { 0.5f, 0.0f }, { 1.0f, 0.5f } });
		painter.AddImage(FakeTexture(100), PainterRect{ { 0, 0 }, { 4, 4 } }, PainterStyle::Filled(glm::vec4(1.0f)));

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 3 && TypeOf(commands[0]) == EPainterCommandType::Image, "Each image should make one image command");

		const PainterTexturedRectPayload first = TexturedRectOf(commands[0]);
		Require(Near(first.Rect, PainterRect{ { 10, 0 }, { 42, 16 } }), "An image's rect should be transformed");
		Require(Near(first.UVs, PainterRect{ { 0, 0 }, { 1, 1 } }), "By default an image should show its whole texture");
		Require(commands[0].Color == PackPremultipliedColor(Orange) && commands[0].OutlineWidth == 0.0f, "The style's colour should tint the image, without an outline");

		Require(commands[0].Texture == 0 && commands[1].Texture == 1 && commands[2].Texture == 0, "The same texture should get the same index");
		Require(painter.GetTextures().size() == 2, "Two different textures were used");
		Require(Near(TexturedRectOf(commands[1]).UVs, PainterRect{ { 0.5f, 0.0f }, { 1.0f, 0.5f } }), "An image should keep the part of the texture it was given");
		Require(commands[1].Group != PainterNoGroup, "An image with a shadow should get a group");
		Require(Near(GetShapeBounds(commands[0]), first.Rect), "An image's bounds should be its rect");
	}

	void TestImageGlows()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.PushTransform(2.0f, { 10, 0 });
		painter.AddImageGlow(FakeTexture(100), PainterRect{ { 20, 10 }, { 4, 2 } }, 6.0f, 0.5f, PainterRect{ { 0.5f, 0.0f }, { 1.0f, 0.5f } });
		painter.PopTransform();
		painter.AddImageGlow(FakeTexture(100), PainterRect{ { 0, 0 }, { 4, 4 } }, -3.0f, 2.0f);
		painter.BeginGroup(MakeShadow());
		painter.AddImageGlow(FakeTexture(200), PainterRect{ { 0, 0 }, { 4, 4 } }, 8.0f, 1.0f);
		painter.EndGroup();

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 3 && TypeOf(commands[0]) == EPainterCommandType::ImageGlow, "Each image glow should make one image glow command");

		const PainterTexturedRectPayload first = TexturedRectOf(commands[0]);
		Require(Near(first.Rect, PainterRect{ { 18, 4 }, { 50, 20 } }), "An image glow's rect should be put the right way round and transformed");
		Require(Near(first.UVs, PainterRect{ { 0.5f, 0.0f }, { 1.0f, 0.5f } }), "An image glow should keep the part of the texture it was given");
		Require(Near(commands[0].Reach, 12.0f), "The reach should be scaled by the transform");
		Require(commands[0].Color == PackPremultipliedColor(glm::vec4(1.0f, 1.0f, 1.0f, 0.5f)), "The strength should be the alpha of a white tint");
		Require(Near(GetShapeBounds(commands[0]), PainterRect{ { 6, -8 }, { 62, 32 } }), "An image glow's bounds should be its rect grown by the reach");
		Require(commands[0].Group == PainterNoGroup, "An image glow on its own belongs to no group");

		Require(commands[1].Reach == 0.0f && commands[1].Color == PackPremultipliedColor(glm::vec4(1.0f)), "A negative reach should become 0, and the strength at most 1");
		Require(commands[0].Texture == 0 && commands[1].Texture == 0 && commands[2].Texture == 1, "Image glows should share textures as images do");
		Require(commands[2].Group != PainterNoGroup && painter.GetProblems().empty(), "An image glow inside a group should belong to it");

		// Binning finds the glow out to its reach: a rect from 28 to 36 reaching 12 pixels covers
		// pixels 15 to 49 with anti-aliasing, tiles 1 to 6.
		painter.BeginFrame(64, 64);
		painter.AddImageGlow(FakeTexture(100), PainterRect{ { 28, 28 }, { 36, 36 } }, 12.0f, 1.0f);
		const PainterTiles tiles = Bin(painter);
		Require(TileHasCommand(tiles, 1, 4, 0) && TileHasCommand(tiles, 6, 4, 0) && !TileHasCommand(tiles, 0, 4, 0) && !TileHasCommand(tiles, 7, 4, 0),
			"An image glow should be binned out to its reach");
	}

	void TestGlass()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		PainterGlass glass;
		glass.Refraction = 4.0f;
		glass.EdgeWidth = 8.0f;
		glass.RimStrength = 2.0f;
		PainterStyle tint = PainterStyle::Outlined(glm::vec4(0.1f, 0.1f, 0.1f, 0.5f), 3.0f);
		tint.Effects = MakeShadow();
		painter.PushTransform(2.0f, { 10, 0 });
		painter.AddGlass(FakeTexture(100), PainterRect{ { 40, 30 }, { 0, 0 } }, 50.0f, tint, glass);
		painter.PopTransform();

		const auto& commands = painter.GetCommands();
		Require(commands.size() == 1 && TypeOf(commands[0]) == EPainterCommandType::Glass, "Glass should make one glass command");
		const auto payload = GetPayload<PainterGlassPayload>(commands[0]);
		Require(Near(payload.Rect, PainterRect{ { 10, 0 }, { 90, 60 } }), "Glass' rect should be put the right way round and transformed");
		Require(Near(payload.Radius, 30.0f), "Glass' radius should be scaled, and no larger than half the shorter side");
		Require(Near(payload.Refraction, 8.0f) && Near(payload.EdgeWidth, 16.0f), "The refraction and edge width should be scaled by the transform");
		Require(Near(payload.RimStrength, 1.0f), "The rim strength should be at most 1");
		Require(commands[0].Color == PackPremultipliedColor(tint.Color) && commands[0].OutlineWidth == 0.0f, "The style's colour should be the tint, without an outline");
		Require(commands[0].Texture == 0 && painter.GetTextures().size() == 1, "The backdrop should be the frame's texture");
		Require(commands[0].Group != PainterNoGroup, "Glass with a shadow should get a group");
		Require(Near(GetShapeBounds(commands[0]), payload.Rect), "Glass' bounds should be its rect");
	}

	void TestTestSheetDrawsCleanly(const ImGuiFonts& fonts)
	{
		Painter painter;
		painter.BeginFrame(static_cast<uint32_t>(PainterTestSheetSize.x), static_cast<uint32_t>(PainterTestSheetSize.y));
		DrawPainterTestSheet(painter, fonts.Font, fonts.Font->OwnerAtlas->TexRef, FakeTexture(100));
		painter.EndFrame();

		Require(painter.GetProblems().empty(), "The test sheet shouldn't report problems: " + (painter.GetProblems().empty() ? std::string() : painter.GetProblems()[0]));
		Require(painter.GetCommands().size() > 100, "The test sheet should draw plenty of commands");
		Require(painter.GetTextures().size() == 2, "The test sheet's text and images use the font atlas and the gradient only");

		// Every type up to the last one, Glass
		constexpr int TypeCount = static_cast<int>(EPainterCommandType::Glass) + 1;
		bool bHasType[TypeCount] = {};
		const PainterRect sheet{ { 0.0f, 0.0f }, PainterTestSheetSize };
		for (const PainterCommand& command : painter.GetCommands())
		{
			bHasType[command.Type] = true;
			const PainterRect bounds = GetShapeBounds(command);
			Require(bounds.Min.x >= sheet.Min.x && bounds.Min.y >= sheet.Min.y && bounds.Max.x <= sheet.Max.x && bounds.Max.y <= sheet.Max.y,
				"Every shape of the test sheet should lie inside PainterTestSheetSize");
		}
		for (int type = 0; type < TypeCount; ++type)
		{
			Require(bHasType[type], "The test sheet should show every command type, missing type " + std::to_string(type));
		}

		PainterTiles tiles;
		BinPainterCommands(painter, PainterBudget{}, tiles);
		// Only the clipped text sample has glyphs that are culled: those wholly outside its clip rect.
		Require(tiles.Problems.empty() && tiles.Stats.DroppedCommands == 0, "Every shape of the test sheet should be binned");
		Require(tiles.Stats.CulledCommands > 0 && tiles.Stats.CulledCommands < 30, "Only the glyphs past the clip rect should be culled, got " + std::to_string(tiles.Stats.CulledCommands));
		Require(tiles.Stats.BinningMilliseconds >= 0.0f, "Binning should time itself");
	}

	void TestTooManyTexturesAreDropped()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		for (uint64_t id = 1; id <= PainterMaxTextures + 2; ++id)
		{
			painter.AddImage(FakeTexture(id), PainterRect{ { 0, 0 }, { 4, 4 } }, PainterStyle::Filled(glm::vec4(1.0f)));
		}
		// A texture the frame already has still fits.
		painter.AddImage(FakeTexture(1), PainterRect{ { 0, 0 }, { 4, 4 } }, PainterStyle::Filled(glm::vec4(1.0f)));

		Require(painter.GetTextures().size() == PainterMaxTextures, "A frame should hold at most PainterMaxTextures textures");
		Require(painter.GetCommands().size() == PainterMaxTextures + 1, "Images with textures past the limit should be dropped, the others kept");
		Require(painter.GetProblems().size() == 1 && HasProblemContaining(painter, "textures in one frame"), "Running out of textures should be reported once");
	}

	// Segments

	void TestSegmentGridCoversOnlyItsCommands()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRect(PainterRect{ { 0, 0 }, { 200, 100 } }, PainterStyle::Filled(Black));
		// With anti-aliasing, the rect covers pixels 19 to 31 both ways, the circle 55 to 65
		// across and 19 to 29 down.
		painter.AddRect(PainterRect{ { 20, 20 }, { 30, 30 } }, PainterStyle::Filled(Orange));
		painter.AddCircle({ 60, 24 }, 4.0f, PainterStyle::Filled(Orange));

		PainterTiles tiles;
		BinPainterSegment(painter, PainterSegment{ 1, 2 }, PainterBudget{}, tiles);

		// From pixel (16, 16), 7 tiles reach past pixel 65 across and 2 past pixel 31 down.
		Require(tiles.OriginX == 16 && tiles.OriginY == 16 && tiles.TilesX == 7 && tiles.TilesY == 2,
			"The grid should cover only the segment's commands, in whole tiles");
		Require(tiles.Stats.Commands == 2 && tiles.Problems.empty(), "Only the segment's commands should be binned");

		// Entries count from the segment's first command, and tiles from the grid's origin.
		Require(TilesOfCommand(tiles, 0) == std::vector<TileCoordinates>{ { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } },
			"The segment's first command should be entry 0, in grid tiles 0 to 1");
		Require(TilesOfCommand(tiles, 1) == std::vector<TileCoordinates>{ { 4, 0 }, { 5, 0 }, { 6, 0 }, { 4, 1 }, { 5, 1 }, { 6, 1 } },
			"The circle should land in grid tiles 4 to 6");
	}

	void TestSegmentGroupReachIsItsOwn()
	{
		Painter painter;
		painter.BeginFrame(200, 100);

		// One group's shapes, split over two segments
		PainterEffects shadow;
		shadow.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
		shadow.Shadow.Offset = glm::vec2(0.0f, 40.0f);
		shadow.Shadow.Blur = 4.0f;
		painter.BeginGroup(shadow);
		painter.AddRect(PainterRect{ { 0, 0 }, { 16, 16 } }, PainterStyle::Filled(Orange));
		painter.AddRect(PainterRect{ { 100, 0 }, { 116, 16 } }, PainterStyle::Filled(Orange));
		painter.EndGroup();

		PainterTiles tiles;
		BinPainterSegment(painter, PainterSegment{ 1, 1 }, PainterBudget{}, tiles);

		// The second rect with its shadow and anti-aliasing covers pixels 95 to 121 across, so the
		// grid starts at 88: the first rect, in the other segment, doesn't widen it.
		Require(tiles.OriginX == 88 && tiles.OriginY == 0 && tiles.TilesX == 5, "A group's reach in a segment should only count the segment's shapes");
	}

	void TestSegmentClippedAwayHasNoTiles()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.PushClipRect(PainterRect{ { 150, 0 }, { 160, 10 } });
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));
		painter.PopClipRect();

		PainterTiles tiles;
		BinPainterSegment(painter, PainterSegment{ 0, 1 }, PainterBudget{}, tiles);
		Require(tiles.TilesX == 0 && tiles.TilesY == 0 && tiles.Tiles.empty() && tiles.Entries.empty(), "A segment wholly clipped away should have no tiles");
		Require(tiles.Stats.CulledCommands == 1 && tiles.Problems.empty(), "Its command should be culled, without a problem");
	}

	void TestSegmentPastTheCommandsIsCut()
	{
		Painter painter;
		painter.BeginFrame(200, 100);
		painter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));
		painter.AddRect(PainterRect{ { 20, 0 }, { 30, 10 } }, PainterStyle::Filled(Orange));

		PainterTiles tiles;
		BinPainterSegment(painter, PainterSegment{ 1, 5 }, PainterBudget{}, tiles);
		// The second rect covers pixels 19 to 31 across and 0 to 11 down: 2 by 2 tiles.
		Require(tiles.Stats.Commands == 1 && TilesOfCommand(tiles, 0).size() == 4, "A segment past the painter's commands should keep those it has");
		Require(tiles.Problems.size() == 1 && tiles.Problems[0].find("reaches past") != std::string::npos, "A segment past the painter's commands should be reported");
	}

	// The segment callbacks a window painter put into a draw list, in order
	struct SegmentCallback
	{
		const Nyx::UI::WindowPainter* Painter = nullptr;
		uint32_t Segment = 0;
		// The indices ImGui had drawn into the draw list before the callback
		uint32_t IndicesBefore = 0;
		// Whether ImGui's render state is reset right after it
		bool bResetsRenderState = false;
	};

	std::vector<SegmentCallback> FindSegmentCallbacks(const ImDrawList& drawList)
	{
		std::vector<SegmentCallback> callbacks;
		uint32_t indices = 0;
		for (int index = 0; index < drawList.CmdBuffer.Size; ++index)
		{
			const ImDrawCmd& command = drawList.CmdBuffer[index];
			if (command.UserCallback == &Nyx::UI::WindowPainter::DrawSegmentCallback)
			{
				// Until ImGui renders the draw list, the data sits in its buffer at this offset.
				Nyx::UI::WindowPainter::SegmentCallbackData data;
				std::memcpy(&data, drawList._CallbacksDataBuf.Data + command.UserCallbackDataOffset, sizeof(data));
				const bool bResets = index + 1 < drawList.CmdBuffer.Size && drawList.CmdBuffer[index + 1].UserCallback == ImDrawCallback_ResetRenderState;
				callbacks.push_back(SegmentCallback{ data.Painter, data.Segment, indices, bResets });
			}
			indices += command.ElemCount;
		}
		return callbacks;
	}

	// An ImGui window at the top left of the main viewport, to paint into
	void BeginTestWindow(const char* name)
	{
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(64.0f, 64.0f));
		ImGui::Begin(name);
	}

	PainterRect ToPainterRect(ImVec2 min, ImVec2 max)
	{
		return PainterRect{ { min.x, min.y }, { max.x, max.y } };
	}

	void TestWindowPainterSplitsWhereImGuiDraws(const ImGuiFonts& fonts)
	{
		BeginTestWindow("Segments");
		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		const PainterRect windowClip = ToPainterRect(drawList.GetClipRectMin(), drawList.GetClipRectMax());

		Nyx::UI::WindowPainter windowPainter;
		windowPainter.Begin(drawList, *ImGui::GetMainViewport());

		// Segment 0: two shapes
		windowPainter.AddRect(PainterRect{ { 10, 30 }, { 50, 40 } }, PainterStyle::Filled(Orange));
		windowPainter.AddCircle({ 20, 35 }, 3.0f, PainterStyle::Filled(Black));
		ImGui::TextUnformatted("H");
		// Segment 1: a shape and two glyphs, after ImGui's text
		windowPainter.AddRect(PainterRect{ { 10, 40 }, { 50, 50 } }, PainterStyle::Filled(Orange));
		windowPainter.AddText(fonts.Font, 13.0f, { 10, 40 }, Black, "Hi");
		// Segment 2: a shape under another clip rect, with nothing drawn in between
		ImGui::PushClipRect(ImVec2(10.0f, 30.0f), ImVec2(30.0f, 50.0f), true);
		const PainterRect pushedClip = ToPainterRect(drawList.GetClipRectMin(), drawList.GetClipRectMax());
		windowPainter.AddCircle({ 20, 40 }, 3.0f, PainterStyle::Filled(Black));
		ImGui::PopClipRect();

		windowPainter.End();
		ImGui::End();

		const std::vector<PainterSegment> expected{ { 0, 2 }, { 2, 3 }, { 5, 1 } };
		Require(windowPainter.GetSegments() == expected, "The shapes should be cut into segments where ImGui drew and where the clip rect changed");
		Require(windowPainter.GetProblems().empty(), "Painting between ImGui's drawing shouldn't report problems");

		const std::vector<SegmentCallback> callbacks = FindSegmentCallbacks(drawList);
		Require(callbacks.size() == 3, "Each segment should have one callback in the draw list, got " + std::to_string(callbacks.size()));
		for (uint32_t index = 0; index < callbacks.size(); ++index)
		{
			Require(callbacks[index].Painter == &windowPainter && callbacks[index].Segment == index, "The callbacks should name their window painter and segment, in order");
			Require(callbacks[index].bResetsRenderState, "ImGui's render state should be reset after each segment");
		}
		Require(callbacks[1].IndicesBefore > callbacks[0].IndicesBefore, "ImGui's text should be drawn between the first two segments");
		Require(callbacks[2].IndicesBefore == callbacks[1].IndicesBefore, "Nothing should be drawn between the last two segments");

		const auto& commands = windowPainter.GetPainter().GetCommands();
		Require(Near(commands[0].ClipRect, windowClip) && Near(commands[4].ClipRect, windowClip), "Shapes should get the window's clip rect");
		Require(Near(commands[5].ClipRect, pushedClip), "A shape should get the clip rect pushed with ImGui");
	}

	void TestWindowPainterPaintsInViewportPixels()
	{
		BeginTestWindow("Viewport pixels");

		// An OS window at (100, 50) on the desktop, at twice the density
		ImGuiViewport viewport;
		viewport.ID = 7;
		viewport.Pos = ImVec2(100.0f, 50.0f);
		viewport.Size = ImVec2(200.0f, 100.0f);
		viewport.FramebufferScale = ImVec2(2.0f, 2.0f);

		ImGui::PushClipRect(ImVec2(100.0f, 50.0f), ImVec2(300.0f, 150.0f), false);
		Nyx::UI::WindowPainter windowPainter;
		windowPainter.Begin(*ImGui::GetWindowDrawList(), viewport);
		windowPainter.AddRect(PainterRect{ { 110, 60 }, { 120, 70 } }, PainterStyle::Filled(Orange));
		windowPainter.AddCircle({ 150, 100 }, 5.0f, PainterStyle::Filled(Orange));
		windowPainter.End();
		ImGui::PopClipRect();
		ImGui::End();

		const Painter& painter = windowPainter.GetPainter();
		Require(painter.GetTargetWidth() == 400 && painter.GetTargetHeight() == 200, "The target should be the viewport's framebuffer");
		Require(windowPainter.GetViewportId() == 7, "The window painter should know its viewport");

		const auto& commands = painter.GetCommands();
		Require(Near(GetPayload<PainterRectPayload>(commands[0]).Rect, PainterRect{ { 20, 20 }, { 40, 40 } }), "Positions should become framebuffer pixels");
		const auto circle = GetPayload<PainterCirclePayload>(commands[1]);
		Require(Near(circle.Center, glm::vec2(100.0f, 100.0f)) && Near(circle.Radius, 10.0f), "Sizes should be scaled to framebuffer pixels");
		Require(Near(commands[0].ClipRect, PainterRect{ { 0, 0 }, { 400, 200 } }), "ImGui's clip rect should become framebuffer pixels too");
		Require(windowPainter.GetProblems().empty(), "Painting into another viewport shouldn't report problems");
	}

	void TestImGuiDrawingInsideAGroupIsReported()
	{
		BeginTestWindow("Split group");
		Nyx::UI::WindowPainter windowPainter;
		windowPainter.Begin(*ImGui::GetWindowDrawList(), *ImGui::GetMainViewport());

		// ImGui drawing before a group's first shape doesn't split it.
		windowPainter.BeginGroup(MakeShadow());
		ImGui::TextUnformatted("H");
		windowPainter.AddRect(PainterRect{ { 10, 30 }, { 50, 40 } }, PainterStyle::Filled(Orange));
		windowPainter.EndGroup();
		Require(windowPainter.GetProblems().empty(), "ImGui drawing before a group's first shape is fine");

		windowPainter.BeginGroup(MakeShadow());
		windowPainter.AddRect(PainterRect{ { 10, 30 }, { 50, 40 } }, PainterStyle::Filled(Orange));
		ImGui::TextUnformatted("H");
		windowPainter.AddRect(PainterRect{ { 10, 40 }, { 50, 50 } }, PainterStyle::Filled(Orange));
		windowPainter.EndGroup();

		windowPainter.End();
		ImGui::End();

		Require(windowPainter.GetProblems().size() == 1 && windowPainter.GetProblems()[0].find("between the shapes of one group") != std::string::npos,
			"ImGui drawing between a group's shapes should be reported");
	}

	void TestWindowPainterClipRectsAndTransforms()
	{
		BeginTestWindow("Clip rects and transforms");
		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		const PainterRect windowClip = ToPainterRect(drawList.GetClipRectMin(), drawList.GetClipRectMax());

		Nyx::UI::WindowPainter windowPainter;
		windowPainter.Begin(drawList, *ImGui::GetMainViewport(), "Clip rects and transforms");

		// Twice the size, 10 pixels in
		windowPainter.PushTransform(2.0f, { 10.0f, 10.0f });
		windowPainter.AddRect(PainterRect{ { 0, 0 }, { 5, 5 } }, PainterStyle::Filled(Orange));

		// A clip rect goes onto ImGui's stack, through the transform: (10, 10) to (30, 30) on the screen
		windowPainter.PushClipRect(PainterRect{ { 0, 0 }, { 10, 10 } });
		const PainterRect pushedClip = ToPainterRect(drawList.GetClipRectMin(), drawList.GetClipRectMax());
		windowPainter.AddRect(PainterRect{ { 0, 0 }, { 5, 5 } }, PainterStyle::Filled(Orange));
		windowPainter.PopClipRect();
		const PainterRect clipAfterPop = ToPainterRect(drawList.GetClipRectMin(), drawList.GetClipRectMax());
		windowPainter.PopTransform();

		// The viewport's transform stays until End().
		windowPainter.PopTransform();
		windowPainter.End();
		ImGui::End();

		Require(Near(pushedClip, Intersect(PainterRect{ { 10, 10 }, { 30, 30 } }, windowClip)), "PushClipRect() should push the transformed rect onto ImGui's stack, cut by the current one");
		Require(Near(clipAfterPop, windowClip), "PopClipRect() should pop ImGui's stack");

		const auto& commands = windowPainter.GetPainter().GetCommands();
		Require(commands.size() == 2 && Near(GetPayload<PainterRectPayload>(commands[0]).Rect, PainterRect{ { 10, 10 }, { 20, 20 } }), "A pushed transform should apply to shapes");
		Require(Near(commands[1].ClipRect, pushedClip), "A shape should get the clip rect pushed with PushClipRect(), not transformed again");
		const std::vector<PainterSegment> expected{ { 0, 1 }, { 1, 1 } };
		Require(windowPainter.GetSegments() == expected, "A clip rect pushed with PushClipRect() should start a segment");
		Require(windowPainter.GetProblems().size() == 1 && windowPainter.GetProblems()[0].find("PopTransform() without") != std::string::npos,
			"Popping the viewport's transform should be refused and reported");
	}

	void TestGetWindowPainterIsOnePerWindow()
	{
		BeginTestWindow("One per window");
		Nyx::UI::WindowPainter& window = Nyx::UI::GetWindowPainter();
		Require(&Nyx::UI::GetWindowPainter() == &window, "A window should get the same window painter each time in a frame");
		Require(window.IsRecording() && window.GetDrawList() == ImGui::GetWindowDrawList() && window.GetWindowName() == "One per window",
			"A window painter should be started for its window");
		window.AddRect(PainterRect{ { 10, 30 }, { 20, 40 } }, PainterStyle::Filled(Orange));

		ImGui::BeginChild("Child", ImVec2(30.0f, 20.0f));
		Nyx::UI::WindowPainter& child = Nyx::UI::GetWindowPainter();
		Require(&child != &window && child.GetDrawList() == ImGui::GetWindowDrawList(), "A child window should get one of its own");
		child.AddRect(PainterRect{ { 10, 30 }, { 20, 40 } }, PainterStyle::Filled(Orange));
		ImGui::EndChild();

		Require(&Nyx::UI::GetWindowPainter() == &window, "Back in the parent, the parent's window painter");
		ImGui::End();

		const std::span<Nyx::UI::WindowPainter* const> painters = Nyx::UI::EndWindowPainters();
		Require(painters.size() == 2 && painters[0] == &window && painters[1] == &child, "EndWindowPainters() should return this frame's, in the order they started");
		Require(!window.IsRecording() && !child.IsRecording() && window.GetSegments().size() == 1, "EndWindowPainters() should end them");

		// A frame without painting has none.
		ImGui::EndFrame();
		ImGui::NewFrame();
		Require(Nyx::UI::EndWindowPainters().empty(), "A new frame should start without window painters");
	}

	void TestForegroundPainterPaintsOverEverything()
	{
		Nyx::UI::WindowPainter& foreground = Nyx::UI::GetForegroundPainter();
		Require(&Nyx::UI::GetForegroundPainter() == &foreground, "The foreground painter should be the same each time in a frame");
		Require(foreground.GetDrawList() == ImGui::GetForegroundDrawList(ImGui::GetMainViewport()) && foreground.GetViewportId() == ImGui::GetMainViewport()->ID,
			"The foreground painter should paint into the main viewport's foreground draw list");
		foreground.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));

		const std::span<Nyx::UI::WindowPainter* const> painters = Nyx::UI::EndWindowPainters();
		Require(std::find(painters.begin(), painters.end(), &foreground) != painters.end(), "The foreground painter should be gathered with the windows' ones");
	}

	void TestPainterBehindGoesInFrontOfTheDrawList()
	{
		BeginTestWindow("Behind");
		ImDrawList& drawList = *ImGui::GetWindowDrawList();
		ImGui::TextUnformatted("H");
		Nyx::UI::WindowPainter& inOrder = Nyx::UI::GetPainterFor(drawList, *ImGui::GetMainViewport(), "Behind");
		inOrder.AddRect(PainterRect{ { 10, 30 }, { 20, 40 } }, PainterStyle::Filled(Orange));

		// Painted after ImGui's text and the shape above, yet in front of the draw list: drawn first
		Nyx::UI::WindowPainter& behind = Nyx::UI::GetPainterFor(drawList, *ImGui::GetMainViewport(), "Behind", Nyx::UI::EWindowPainterPlacement::Behind);
		Require(&behind != &inOrder && behind.GetPlacement() == Nyx::UI::EWindowPainterPlacement::Behind, "A draw list should have a painter of its own for each placement");
		behind.AddRect(PainterRect{ { 0, 0 }, { 64, 64 } }, PainterStyle::Filled(Black));
		// Another clip rect: a second segment, after the first one and still in front of ImGui's text
		ImGui::PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(32.0f, 32.0f), true);
		behind.AddRect(PainterRect{ { 0, 0 }, { 32, 32 } }, PainterStyle::Filled(Orange));
		ImGui::PopClipRect();
		ImGui::End();
		Nyx::UI::EndWindowPainters();

		const std::vector<SegmentCallback> callbacks = FindSegmentCallbacks(drawList);
		Require(callbacks.size() == 3, "Two segments behind and one in order should make three callbacks, got " + std::to_string(callbacks.size()));
		Require(callbacks[0].Painter == &behind && callbacks[0].Segment == 0 && callbacks[1].Painter == &behind && callbacks[1].Segment == 1,
			"The segments behind should come first, in the order they were painted");
		Require(callbacks[0].IndicesBefore == 0 && callbacks[1].IndicesBefore == 0, "Nothing ImGui drew should come before the segments behind");
		Require(callbacks[0].bResetsRenderState && callbacks[1].bResetsRenderState, "ImGui's render state should be reset after each segment behind");
		Require(drawList.CmdBuffer[0].UserCallback == &Nyx::UI::WindowPainter::DrawSegmentCallback, "The first segment behind should be the draw list's first command");
		Require(callbacks[2].Painter == &inOrder && callbacks[2].IndicesBefore > 0, "The painter in order should stay after ImGui's text");
		Require(behind.GetSegments().size() == 2 && behind.GetProblems().empty(), "Painting behind should keep its segments without problems");
	}

	// Runs of segments (stage 3, change 8): a run's segments are copied into one painter, with
	// their groups and textures renumbered for it
	void TestAppendSegmentRenumbersGroupsAndTextures()
	{
		const PainterRect rect{ { 10, 10 }, { 20, 20 } };
		Painter first;
		first.BeginFrame(200, 100);
		first.AddImage(FakeTexture(1), rect, PainterStyle::Filled(glm::vec4(1.0f)));
		first.AddRect(rect, WithShadow(Orange));
		Painter second;
		second.BeginFrame(200, 100);
		second.AddRect(rect, WithShadow(Black));
		second.AddImage(FakeTexture(2), rect, PainterStyle::Filled(glm::vec4(1.0f)));
		second.AddImage(FakeTexture(1), rect, PainterStyle::Filled(glm::vec4(1.0f)));

		Painter run;
		run.BeginFrame(200, 100);
		Require(run.AppendSegment(first, PainterSegment{ 0, 2 }) && run.AppendSegment(second, PainterSegment{ 0, 3 }), "Two painters' segments should fit into one");
		const auto& commands = run.GetCommands();
		Require(commands.size() == 5 && run.GetGroups().size() == 2, "The run should have both segments' commands and groups");
		Require(commands[1].Group == 0 && commands[2].Group == 1, "Each segment's group should get its own index in the run");
		Require(run.GetTextures().size() == 2 && run.GetTextures()[0].GetTexID() == 1 && run.GetTextures()[1].GetTexID() == 2,
			"A texture both segments use should be in the run once");
		Require(commands[0].Texture == 0 && commands[3].Texture == 1 && commands[4].Texture == 0, "Textured commands should point at their texture in the run");
		Require(Near(TexturedRectOf(commands[4]).Rect, rect) && commands[2].Color == second.GetCommands()[0].Color, "Commands should be copied as they are");

		// Part of a segment brings only what it uses.
		Painter part;
		part.BeginFrame(200, 100);
		part.AppendSegment(second, PainterSegment{ 1, 1 });
		Require(part.GetCommands().size() == 1 && part.GetGroups().empty() && part.GetTextures().size() == 1 && part.GetCommands()[0].Texture == 0,
			"A part of a segment should bring only its own textures and groups");

		// A run without room for another texture takes nothing of the segment.
		Painter full;
		full.BeginFrame(200, 100);
		for (uint64_t texture = 10; texture < 10 + PainterMaxTextures; ++texture)
		{
			full.AddImage(FakeTexture(texture), rect, PainterStyle::Filled(glm::vec4(1.0f)));
		}
		Painter crowded;
		crowded.BeginFrame(200, 100);
		Require(crowded.AppendSegment(full, PainterSegment{ 0, PainterMaxTextures }), "A segment with the most textures should fit an empty run");
		Require(!crowded.AppendSegment(second, PainterSegment{ 0, 3 }), "A segment with further textures shouldn't fit a full run");
		Require(crowded.GetCommands().size() == PainterMaxTextures && crowded.GetTextures().size() == PainterMaxTextures && crowded.GetGroups().empty(),
			"A segment that doesn't fit should leave the run as it was");
	}

	// A frame of its own, with bare windows: no title bar, background or border, so that ImGui
	// draws nothing in them but what a test asks for
	void BeginBareWindow(const char* name)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
		ImGui::SetNextWindowSize(ImVec2(64.0f, 64.0f));
		ImGui::Begin(name, nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar |
			ImGuiWindowFlags_NoSavedSettings);
		ImGui::PopStyleVar();
	}

	// Segments that follow each other in render order, with nothing ImGui draws between them, are
	// one run, also across windows; a run ends where ImGui draws, and before a draw list asked for.
	void TestSegmentRunsFollowRenderOrder()
	{
		ImGui::EndFrame();
		ImGui::NewFrame();
		const PainterRect rect{ { 10, 10 }, { 20, 20 } };

		BeginBareWindow("Runs A");
		Nyx::UI::WindowPainter& first = Nyx::UI::GetWindowPainter();
		first.AddRect(rect, PainterStyle::Filled(Orange));
		first.AddRect(rect, PainterStyle::Filled(Black));
		ImGui::TextUnformatted("H");
		first.AddRect(rect, PainterStyle::Filled(Orange));
		// Another clip rect: another segment, with nothing drawn in between
		ImGui::PushClipRect(ImVec2(0.0f, 0.0f), ImVec2(30.0f, 30.0f), true);
		first.AddRect(rect, PainterStyle::Filled(Black));
		ImGui::PopClipRect();
		ImGui::End();

		BeginBareWindow("Runs B");
		Nyx::UI::WindowPainter& second = Nyx::UI::GetWindowPainter();
		second.AddRect(rect, PainterStyle::Filled(Orange));
		const ImDrawList* secondList = ImGui::GetWindowDrawList();
		ImGui::End();

		Nyx::UI::EndWindowPainters();
		ImGui::Render();
		const ImDrawData& drawData = *ImGui::GetDrawData();
		using Nyx::UI::WindowSegmentRef;
		const auto segmentsOf = [](const std::vector<Nyx::UI::WindowSegmentRun>& runs)
		{
			std::vector<std::vector<WindowSegmentRef>> result;
			for (const Nyx::UI::WindowSegmentRun& run : runs)
			{
				result.push_back(run.Segments);
			}
			return result;
		};

		const std::vector<std::vector<WindowSegmentRef>> expected{ { { &first, 0 } }, { { &first, 1 }, { &first, 2 }, { &second, 0 } } };
		Require(first.GetSegments().size() == 3 && second.GetSegments().size() == 1, "The windows should have three segments and one");
		Require(segmentsOf(Nyx::UI::FindWindowSegmentRuns(drawData)) == expected,
			"Segments should run on across a clip rect and into the next window, and stop where ImGui's text is");

		const ImDrawList* const breakBefore[] = { secondList };
		const std::vector<std::vector<WindowSegmentRef>> broken{ { { &first, 0 } }, { { &first, 1 }, { &first, 2 } }, { { &second, 0 } } };
		Require(segmentsOf(Nyx::UI::FindWindowSegmentRuns(drawData, breakBefore)) == broken, "A run should end before a draw list asked to break before");

		ImGui::NewFrame();
	}

	// Debug > Mark ImGui Shapes: what ImGui draws with its white pixel is outlined, its text not,
	// nor what it draws fully transparent. A viewport's foreground draw list is looked at too.
	void TestImGuiShapesAreMarked()
	{
		ImGui::EndFrame();
		ImGui::NewFrame();
		Nyx::UI::SetMarkImGuiShapes(true);

		BeginBareWindow("Marks");
		ImGui::Button("Apply");
		const ImRect button(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
		ImGui::TextUnformatted("Text isn't marked");
		ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(2.0f, 40.0f), ImVec2(30.0f, 60.0f), IM_COL32(255, 255, 255, 0));
		ImGui::End();
		// As the renderer does: once the frame has ended, when ImGui has hidden its implicit
		// "Debug" window, which nothing used
		ImGui::EndFrame();
		Nyx::UI::MarkImGuiShapes();

		Require(Nyx::UI::GetImGuiShapeCount() == 1 && Nyx::UI::GetImGuiShapeWindowCount() == 1, "The button's frame should be the one shape ImGui drew, got " +
			std::to_string(Nyx::UI::GetImGuiShapeCount()));
		const auto& marks = Nyx::UI::GetForegroundPainter().GetPainter().GetCommands();
		Require(!marks.empty(), "A shape should be marked over everything");
		const PainterRect mark = GetPayload<PainterRectPayload>(marks[0]).Rect;
		Require(mark.Min.x <= button.Min.x && mark.Min.x > button.Min.x - 1.0f && mark.Max.y >= button.Max.y && mark.Max.y < button.Max.y + 1.0f,
			"The mark should be the button's frame, with ImGui's anti-aliased edge");
		Nyx::UI::EndWindowPainters();

		// A shape over all windows, e.g. the editor's focus border
		ImGui::NewFrame();
		ImGui::GetForegroundDrawList()->AddRect(ImVec2(1.0f, 1.0f), ImVec2(50.0f, 50.0f), IM_COL32(255, 200, 100, 255), 4.0f, 0, 3.0f);
		ImGui::EndFrame();
		Nyx::UI::MarkImGuiShapes();
		Require(Nyx::UI::GetImGuiShapeCount() == 1 && Nyx::UI::GetImGuiShapeWindowCount() == 1,
			"A shape in the foreground draw list should be marked, got " + std::to_string(Nyx::UI::GetImGuiShapeCount()));

		Nyx::UI::SetMarkImGuiShapes(false);
		Nyx::UI::MarkImGuiShapes();
		Require(Nyx::UI::GetImGuiShapeCount() == 0, "Nothing should be marked while marking is off");
		Nyx::UI::EndWindowPainters();
		ImGui::NewFrame();
	}

	void TestPngsSaveAndLoad()
	{
		// 2x2: red, green / blue, half transparent white
		RgbaImage image;
		image.Width = 2;
		image.Height = 2;
		image.Pixels = { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128 };

		const std::filesystem::path folder = std::filesystem::temp_directory_path() / "NyxPainterTests";
		const std::filesystem::path path = folder / "Sub folder" / "Image.png";
		std::filesystem::remove_all(folder);
		std::string error;
		Require(SavePng(path, image, error), "Saving a PNG should work, and make its folder: " + error);

		RgbaImage loaded;
		Require(LoadRgbaImage(path, loaded, error), "Loading the PNG back should work: " + error);
		Require(loaded.Width == 2 && loaded.Height == 2 && loaded.Pixels == image.Pixels, "A PNG should load back as it was saved");

		Require(!LoadRgbaImage(folder / "Missing.png", loaded, error) && !error.empty(), "Loading a missing file should fail with a reason");
		Require(!SavePng(folder / "Empty.png", RgbaImage{}, error) && !error.empty(), "Saving an image without pixels should fail with a reason");
		std::filesystem::remove_all(folder);
	}

	void TestWindowPainterMisuseIsReported()
	{
		Nyx::UI::WindowPainter windowPainter;
		windowPainter.AddRect(PainterRect{ { 0, 0 }, { 10, 10 } }, PainterStyle::Filled(Orange));
		windowPainter.End();
		Require(windowPainter.GetPainter().GetCommands().empty(), "A shape outside Begin()/End() should be dropped");
		Require(windowPainter.GetProblems().size() == 2, "A shape outside Begin()/End() and an End() without a Begin() should be reported");

		BeginTestWindow("Misuse");
		windowPainter.Begin(*ImGui::GetWindowDrawList(), *ImGui::GetMainViewport());
		windowPainter.BeginGroup(PainterEffects{});
		windowPainter.AddRect(PainterRect{ { 10, 30 }, { 50, 40 } }, PainterStyle::Filled(Orange));
		windowPainter.End();
		ImGui::End();

		Require(windowPainter.GetProblems().size() == 1 && windowPainter.GetProblems()[0].find("BeginGroup() without an EndGroup()") != std::string::npos,
			"Begin() should drop the previous problems, and End() should add the painter's");
	}
}

int main()
{
	try
	{
		TestColorsArePremultiplied();
		TestShapesAreRecordedInOrder();
		TestRoundedRectRadiiFitTheRect();
		TestArcsAndSectors();
		TestLinesIgnoreOutlineWidth();
		TestClipRectsNest();
		TestTransformsScaleAndMoveEverything();
		TestGroupsShareEffects();
		TestShapeWithEffectsIsAGroupOfOne();
		TestGroupMisuseIsReported();
		TestEndFrameClosesWhatIsLeftOpen();
		TestShapeBounds();

		TestGridCoversTheTarget();
		TestShapesLandInTheirTiles();
		TestCommandsThatTouchNothingAreCulled();
		TestClipRectCutsCoverage();
		TestTilesKeepDrawingOrder();
		TestLinesWalkTheirTiles();
		TestGroupsCoverTheirWholeReach();
		TestBudgetDropsTheRestOfTheFrame();
		TestBinningReusesItsResult();

		TestTextWithoutAFontIsReported();
		TestImages();
		TestImageGlows();
		TestGlass();
		TestTooManyTexturesAreDropped();
		{
			const ImGuiFonts fonts;
			TestTextMakesOneGlyphPerVisibleCharacter(fonts);
			TestTextBreaksLines(fonts);
			TestZoomedTextIsBakedAtItsSize(fonts);
			TestTestSheetDrawsCleanly(fonts);

			TestWindowPainterSplitsWhereImGuiDraws(fonts);
			TestWindowPainterPaintsInViewportPixels();
			TestImGuiDrawingInsideAGroupIsReported();
			TestWindowPainterMisuseIsReported();
			TestWindowPainterClipRectsAndTransforms();
			// It counts the frame's window painters, then starts a new frame.
			TestGetWindowPainterIsOnePerWindow();
			TestForegroundPainterPaintsOverEverything();
			TestPainterBehindGoesInFrontOfTheDrawList();
			TestSegmentRunsFollowRenderOrder();
			TestImGuiShapesAreMarked();
		}
		TestAppendSegmentRenumbersGroupsAndTextures();

		TestSegmentGridCoversOnlyItsCommands();
		TestSegmentGroupReachIsItsOwn();
		TestSegmentClippedAwayHasNoTiles();
		TestSegmentPastTheCommandsIsCut();
		TestPngsSaveAndLoad();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All painter tests passed\n";
	return 0;
}
