// The painter's recording side: the commands and groups it makes, its clip and transform stacks,
// and the problems it reports. No GPU is involved.
#include "Painter.h"

#include <cmath>
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
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All painter tests passed\n";
	return 0;
}
