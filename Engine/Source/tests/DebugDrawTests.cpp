// Debug drawing (DebugDraw): the shapes as lines, how long they stay, and the cap on lines.
#include "DebugDraw.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using Nyx::DebugDraw;
	using Nyx::Tests::Require;

	const glm::vec3 Red{ 1.0f, 0.0f, 0.0f };

	void TestShapes()
	{
		DebugDraw line;
		line.Line({ 0, 0, 0 }, { 1, 0, 0 }, Red);
		Require(line.GetLines().size() == 1, "A line is one line");

		DebugDraw arrow;
		arrow.Arrow({ 0, 0, 0 }, { 0, 2, 0 }, Red);
		Require(arrow.GetLines().size() == 5, "An arrow is a line and four for its head");

		DebugDraw stubby;
		stubby.Arrow({ 1, 1, 1 }, { 1, 1, 1 }, Red);
		Require(stubby.GetLines().size() == 1, "An arrow of no length has no head");

		DebugDraw box;
		box.Box({ -1, -1, -1 }, { 1, 1, 1 }, Red);
		Require(box.GetLines().size() == 12, "A box has twelve edges");

		DebugDraw sphere;
		sphere.Sphere({ 0, 0, 0 }, 1.0f, Red);
		Require(sphere.GetLines().size() == 72, "A sphere is three circles of 24 segments");

		DebugDraw axes;
		axes.Axes(glm::mat4(1.0f), 1.0f);
		Require(axes.GetLines().size() == 3, "Axes are three lines");

		DebugDraw cross;
		cross.Cross({ 0, 0, 0 }, 0.2f, Red);
		Require(cross.GetLines().size() == 3, "A cross is three lines");

		// An oriented box: every corner moved by the transform
		DebugDraw drawing;
		glm::mat4 transform(1.0f);
		transform[3] = glm::vec4(10.0f, 0.0f, 0.0f, 1.0f);
		drawing.Box(transform, { -1, -1, -1 }, { 1, 1, 1 }, Red);
		for (const Nyx::DebugLine& line : drawing.GetLines())
		{
			Require(line.From.x >= 9.0f && line.To.x <= 11.0f, "An oriented box's corners should be where its transform puts them");
		}

		// The arrow's head ends at its tip
		DebugDraw pointing;
		pointing.Arrow({ 0, 0, 0 }, { 0, 0, 4 }, Red);
		for (size_t i = 1; i < pointing.GetLines().size(); ++i)
		{
			Require(pointing.GetLines()[i].From == glm::vec3(0, 0, 4), "The arrow's head should start at its tip");
		}
	}

	void TestLifetimes()
	{
		DebugDraw drawing;
		drawing.Line({ 0, 0, 0 }, { 1, 0, 0 }, Red);
		drawing.Line({ 0, 0, 0 }, { 0, 1, 0 }, Red, 0.05f);
		Require(drawing.GetLines().size() == 2, "Both lines should be there until the owner moves on");

		drawing.Advance(0.02f);
		Require(drawing.GetLines().size() == 1, "A line without time should go at the next Advance()");
		Require(drawing.GetLines().front().To == glm::vec3(0, 1, 0), "The timed line should stay");

		drawing.Advance(0.02f);
		Require(drawing.GetLines().size() == 1, "A timed line should stay while it has time left");

		drawing.Advance(0.02f);
		Require(drawing.GetLines().empty(), "A timed line should go once its time ran out");

		drawing.Line({ 0, 0, 0 }, { 1, 0, 0 }, Red, 10.0f);
		drawing.Clear();
		Require(drawing.GetLines().empty(), "Clear() should drop every shape");
	}

	void TestLineCap()
	{
		DebugDraw drawing;
		for (size_t i = 0; i < DebugDraw::MaxLines + 10; ++i)
		{
			drawing.Line({ 0, 0, 0 }, { 1, 0, 0 }, Red);
		}
		Require(drawing.GetLines().size() == DebugDraw::MaxLines, "No more than MaxLines lines should be kept");
		Require(drawing.GetDroppedLineCount() == 10, "The lines beyond the cap should be counted");
	}
}

int main()
{
	try
	{
		if constexpr (!DebugDraw::Enabled)
		{
			std::cout << "Debug drawing is compiled out of this build; nothing to test.\n";
			return 0;
		}
		TestShapes();
		TestLifetimes();
		TestLineCap();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Debug draw test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All debug draw tests passed.\n";
	return 0;
}
