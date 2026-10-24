// The point-cloud recogniser (PointCloudRecognizer): known shapes drawn again with a shaky hand, in
// another stroke order and direction, larger, elsewhere and turned, recognised; a scribble below
// the threshold a game would set; turns told apart when rotation sensitive; the same result every
// time; the template text format; and the time 50 templates take.
#include "PointCloudRecognizer.h"
#include "TestSupport.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::PointCloudOptions;
	using Nyx::PointCloudRecognizer;
	using Nyx::PointCloudResult;
	using Nyx::Stroke;
	using Nyx::Tests::Require;

	constexpr float Pi = 3.14159265358979f;

	// What a game would take as recognised
	constexpr float MinScore = 0.75f;

	// A small generator of its own, so the shakes are the same on every machine
	class Shaky
	{
	public:
		explicit Shaky(uint32_t seed)
			: State(seed * 2654435761u + 1u)
		{
		}

		// In [-1, 1]
		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) / static_cast<float>(1u << 23) - 1.0f;
		}

		// Every point moved by up to amount in x and y
		std::vector<Stroke> Shake(std::vector<Stroke> strokes, float amount)
		{
			for (Stroke& stroke : strokes)
			{
				for (glm::vec2& point : stroke)
				{
					point += amount * glm::vec2(Next(), Next());
				}
			}
			return strokes;
		}

	private:
		uint32_t State;
	};

	// Points along a line, the last one at `to`
	void AddLine(Stroke& stroke, glm::vec2 from, glm::vec2 to, int steps)
	{
		for (int step = stroke.empty() ? 0 : 1; step <= steps; ++step)
		{
			stroke.push_back(from + (to - from) * (static_cast<float>(step) / static_cast<float>(steps)));
		}
	}

	std::vector<Stroke> MakeCircle(float startAngle = 0.0f, bool bClockwise = false)
	{
		Stroke stroke;
		for (int step = 0; step <= 48; ++step)
		{
			const float angle = startAngle + (bClockwise ? -1.0f : 1.0f) * 2.0f * Pi * static_cast<float>(step) / 48.0f;
			stroke.push_back(glm::vec2(std::cos(angle), std::sin(angle)));
		}
		return { stroke };
	}

	// Pointing up; in one stroke, or each side its own in the order and direction given
	std::vector<Stroke> MakeTriangle(bool bOneStroke = true, bool bReversed = false)
	{
		const glm::vec2 corners[3] = { { -1.0f, -0.8f }, { 0.0f, 0.9f }, { 1.0f, -0.8f } };
		std::vector<Stroke> strokes;
		Stroke stroke;
		for (int side = 0; side < 3; ++side)
		{
			const int from = bReversed ? (3 - side) % 3 : side;
			const int to = bReversed ? (5 - side) % 3 : (side + 1) % 3;
			if (!bOneStroke)
			{
				stroke.clear();
			}
			AddLine(stroke, corners[from], corners[to], 10);
			if (!bOneStroke)
			{
				strokes.push_back(stroke);
			}
		}
		if (bOneStroke)
		{
			strokes.push_back(stroke);
		}
		return strokes;
	}

	// Pointing right: the shaft, then the head as one stroke; or the head first, each drawn backwards
	std::vector<Stroke> MakeArrow(bool bHeadFirst = false)
	{
		Stroke shaft;
		AddLine(shaft, { -1.0f, 0.0f }, { 1.0f, 0.0f }, 16);
		Stroke head;
		AddLine(head, { 0.55f, 0.45f }, { 1.0f, 0.0f }, 6);
		AddLine(head, { 1.0f, 0.0f }, { 0.55f, -0.45f }, 6);
		if (!bHeadFirst)
		{
			return { shaft, head };
		}
		std::reverse(shaft.begin(), shaft.end());
		std::reverse(head.begin(), head.end());
		return { head, shaft };
	}

	std::vector<Stroke> MakeZigzag(int teeth = 4, bool bReversed = false)
	{
		Stroke stroke;
		for (int corner = 0; corner < teeth; ++corner)
		{
			const glm::vec2 from(-1.0f + 2.0f * corner / teeth, corner % 2 == 0 ? -0.5f : 0.5f);
			const glm::vec2 to(-1.0f + 2.0f * (corner + 1) / teeth, corner % 2 == 0 ? 0.5f : -0.5f);
			AddLine(stroke, from, to, 8);
		}
		if (bReversed)
		{
			std::reverse(stroke.begin(), stroke.end());
		}
		return { stroke };
	}

	std::vector<Stroke> Transform(std::vector<Stroke> strokes, float scale, glm::vec2 offset, float angle = 0.0f)
	{
		const float c = std::cos(angle);
		const float s = std::sin(angle);
		for (Stroke& stroke : strokes)
		{
			for (glm::vec2& point : stroke)
			{
				point = scale * glm::vec2(c * point.x - s * point.y, s * point.x + c * point.y) + offset;
			}
		}
		return strokes;
	}

	// Three examples of each shape, as a game ships a few drawn by different hands
	void AddShapes(PointCloudRecognizer& recognizer)
	{
		Shaky shaky(1);
		for (int example = 0; example < 3; ++example)
		{
			Require(recognizer.AddTemplate("Circle", shaky.Shake(MakeCircle(0.3f * example), 0.02f)), "A circle should make a template");
			Require(recognizer.AddTemplate("Triangle", shaky.Shake(MakeTriangle(), 0.02f)), "A triangle should make a template");
			Require(recognizer.AddTemplate("Arrow", shaky.Shake(MakeArrow(), 0.02f)), "An arrow should make a template");
			Require(recognizer.AddTemplate("Zigzag", shaky.Shake(MakeZigzag(), 0.02f)), "A zigzag should make a template");
		}
	}

	struct Drawing
	{
		std::string Expected;
		std::string What;
		std::vector<Stroke> Strokes;
	};

	// The shapes as a player might draw them: shakier, elsewhere, larger or smaller, strokes in
	// another order and direction
	std::vector<Drawing> MakeDrawings(uint32_t seed)
	{
		Shaky shaky(seed);
		return {
			{ "Circle", "a circle", shaky.Shake(Transform(MakeCircle(), 3.0f, { 10.0f, -4.0f }), 0.15f) },
			{ "Circle", "a circle drawn clockwise from the bottom", shaky.Shake(Transform(MakeCircle(-0.5f * Pi, true), 0.2f, { 0.0f, 0.0f }), 0.01f) },
			{ "Triangle", "a triangle in one stroke", shaky.Shake(Transform(MakeTriangle(), 50.0f, { 100.0f, 100.0f }), 2.5f) },
			{ "Triangle", "a triangle side by side", shaky.Shake(MakeTriangle(false), 0.05f) },
			{ "Triangle", "a triangle side by side, backwards", shaky.Shake(MakeTriangle(false, true), 0.05f) },
			{ "Arrow", "an arrow", shaky.Shake(Transform(MakeArrow(), 2.0f, { -3.0f, 1.0f }), 0.1f) },
			{ "Arrow", "an arrow, its head first and both backwards", shaky.Shake(MakeArrow(true), 0.05f) },
			{ "Zigzag", "a zigzag", shaky.Shake(Transform(MakeZigzag(), 0.5f, { 2.0f, 2.0f }), 0.025f) },
			{ "Zigzag", "a zigzag backwards", shaky.Shake(MakeZigzag(4, true), 0.05f) },
		};
	}

	std::string Describe(const PointCloudResult& result)
	{
		if (!result.Best)
		{
			return "nothing";
		}
		std::string text = result.Best->Name + " (score " + std::to_string(result.Best->Score) + ", distance " +
			std::to_string(result.Best->Distance) + ")";
		if (result.RunnerUp)
		{
			text += ", then " + result.RunnerUp->Name + " (score " + std::to_string(result.RunnerUp->Score) + ")";
		}
		return text;
	}

	void RequireRecognised(const PointCloudRecognizer& recognizer, const Drawing& drawing, glm::vec2 up = { 0.0f, 1.0f })
	{
		const PointCloudResult result = recognizer.Recognize(drawing.Strokes, up);
		std::cout << "  " << drawing.What << ": " << Describe(result) << "\n";
		Require(result.Best && result.Best->Name == drawing.Expected, drawing.What + " should be a " + drawing.Expected + ", was " + Describe(result));
		Require(result.Best->Score >= MinScore, drawing.What + " should score at least " + std::to_string(MinScore) + ": " + Describe(result));
		Require(result.RunnerUp && result.RunnerUp->Name != drawing.Expected && result.RunnerUp->Score < result.Best->Score,
			drawing.What + " should have a runner-up of another name, scoring less: " + Describe(result));
	}

	// The cloud: as many points as asked, in a box of side 1, round the origin
	void TestPointCloud()
	{
		const std::vector<glm::vec2> cloud = Nyx::MakePointCloud(Transform(MakeArrow(true), 7.0f, { 3.0f, -2.0f }), 32);
		Require(cloud.size() == 32, "The cloud should have 32 points, has " + std::to_string(cloud.size()));
		glm::vec2 lowest(1e9f);
		glm::vec2 highest(-1e9f);
		glm::vec2 sum(0.0f);
		for (const glm::vec2& point : cloud)
		{
			lowest = glm::min(lowest, point);
			highest = glm::max(highest, point);
			sum += point;
		}
		Require(std::abs(std::max(highest.x - lowest.x, highest.y - lowest.y) - 1.0f) < 1e-5f, "The cloud's box should have a longer side of 1");
		Require(glm::length(sum / 32.0f) < 1e-5f, "The cloud's centroid should be at the origin");

		// The points are spread evenly along the strokes: the shaft's 2 of the arrow's 3.27 in length
		size_t onShaft = 0;
		for (const glm::vec2& point : cloud)
		{
			onShaft += std::abs(point.y - cloud.back().y) < 1e-4f ? 1 : 0;
		}
		Require(onShaft >= 18 && onShaft <= 21, "About 20 of 32 points should be on the arrow's shaft, " + std::to_string(onShaft) + " are");

		Require(Nyx::MakePointCloud({}, 32).empty(), "No strokes should make no cloud");
		Require(Nyx::MakePointCloud({ { { 1.0f, 2.0f } }, { { 1.0f, 2.0f }, { 1.0f, 2.0f } } }, 32).empty(), "Strokes in one place should make no cloud");
		Require(Nyx::MakePointCloud(MakeCircle(), 1).empty(), "A cloud of one point should be refused");
		Require(Nyx::MakePointCloud({ { { 0.0f, 0.0f }, { 2.0f, 0.0f } } }, 5).size() == 5, "A straight line should make a cloud");

		PointCloudRecognizer recognizer;
		Require(!recognizer.AddTemplate("Dot", { { { 1.0f, 1.0f } } }), "A dot should make no template");
		Require(recognizer.GetTemplateCount() == 0, "A refused template should not be kept");
		Require(!recognizer.Recognize(MakeCircle()).Best, "Without templates nothing should be recognised");
		AddShapes(recognizer);
		Require(!recognizer.Recognize({}).Best, "No strokes should be nothing");
	}

	// The shapes drawn again, as drawn; a scribble isn't close to any of them
	void TestKnownShapesRecognised(bool bRotationSensitive)
	{
		std::cout << (bRotationSensitive ? "Rotation sensitive:\n" : "Rotation insensitive:\n");
		PointCloudRecognizer recognizer(PointCloudOptions{ .bRotationSensitive = bRotationSensitive });
		AddShapes(recognizer);
		Require(recognizer.GetTemplateCount() == 12, "Twelve templates should be kept");
		for (uint32_t seed = 10; seed < 13; ++seed)
		{
			for (const Drawing& drawing : MakeDrawings(seed))
			{
				RequireRecognised(recognizer, drawing);
			}
		}

		// Up and down, round and round: close to nothing
		Shaky shaky(7);
		for (uint32_t scribble = 0; scribble < 4; ++scribble)
		{
			Stroke stroke{ { 0.0f, 0.0f } };
			for (int step = 0; step < 60; ++step)
			{
				stroke.push_back(stroke.back() + glm::vec2(shaky.Next(), shaky.Next()));
			}
			const PointCloudResult result = recognizer.Recognize({ stroke });
			std::cout << "  a scribble: " << Describe(result) << "\n";
			Require(result.Best && result.Best->Score < MinScore, "A scribble should score under " + std::to_string(MinScore) + ": " + Describe(result));
		}
	}

	// Rotation insensitive, a shape turned is the same shape, and the match says which way its up lay
	void TestTurnedShapesRecognised()
	{
		std::cout << "Turned:\n";
		PointCloudRecognizer recognizer;
		AddShapes(recognizer);
		for (const float degrees : { 37.0f, 90.0f, 161.0f, -100.0f })
		{
			const float angle = degrees * Pi / 180.0f;
			for (Drawing drawing : MakeDrawings(20))
			{
				drawing.Strokes = Transform(drawing.Strokes, 1.0f, { 0.0f, 0.0f }, angle);
				drawing.What += " turned " + std::to_string(static_cast<int>(degrees)) + " degrees";
				RequireRecognised(recognizer, drawing);

				// Only the arrow's turn shows: a circle looks the same at every turn, a zigzag upside
				// down, a triangle nearly so a third of a turn on
				if (drawing.Expected == "Arrow")
				{
					const glm::vec2 up = recognizer.Recognize(drawing.Strokes).Best->Up;
					const glm::vec2 expected(-std::sin(angle), std::cos(angle));
					const float off = std::acos(std::clamp(glm::dot(up, expected), -1.0f, 1.0f)) * 180.0f / Pi;
					Require(std::abs(glm::length(up) - 1.0f) < 1e-4f, "The match's up should be of unit length");
					Require(off < 8.0f, drawing.What + ": the match's up should be within 8 degrees of the turn, is " + std::to_string(off) + " off");
				}
			}
		}
	}

	// Rotation sensitive, an arrow to the right and one up are two shapes, told apart by the up given
	void TestRotationSensitive()
	{
		PointCloudRecognizer recognizer(PointCloudOptions{ .bRotationSensitive = true });
		Shaky shaky(3);
		for (int example = 0; example < 2; ++example)
		{
			recognizer.AddTemplate("Right", shaky.Shake(MakeArrow(), 0.02f));
			recognizer.AddTemplate("Up", shaky.Shake(Transform(MakeArrow(), 1.0f, { 0.0f, 0.0f }, 0.5f * Pi), 0.02f));
			recognizer.AddTemplate("Left", shaky.Shake(Transform(MakeArrow(), 1.0f, { 0.0f, 0.0f }, Pi), 0.02f));
		}

		const std::vector<Stroke> drawn = shaky.Shake(MakeArrow(true), 0.04f);
		const PointCloudResult asDrawn = recognizer.Recognize(drawn);
		Require(asDrawn.Best && asDrawn.Best->Name == "Right", "An arrow to the right should be Right: " + Describe(asDrawn));
		Require(asDrawn.Best->Up == glm::vec2(0.0f, 1.0f), "The match's up should be the one given");

		// The same strokes on a plane whose up is +X (a scale of up doesn't matter): to the right of
		// it is down, here
		const PointCloudResult turnedLeft = recognizer.Recognize(drawn, { -3.0f, 0.0f });
		Require(turnedLeft.Best && turnedLeft.Best->Name == "Up", "With -X up, an arrow along +X should be Up: " + Describe(turnedLeft));
		const PointCloudResult upsideDown = recognizer.Recognize(drawn, { 0.0f, -1.0f });
		Require(upsideDown.Best && upsideDown.Best->Name == "Left", "Upside down, an arrow along +X should be Left: " + Describe(upsideDown));
		const PointCloudResult noUp = recognizer.Recognize(drawn, { 0.0f, 0.0f });
		Require(noUp.Best && noUp.Best->Name == "Right", "Without an up, +Y should be up: " + Describe(noUp));
	}

	// The same templates and strokes give the same result, to the bit
	void TestDeterministic()
	{
		for (const bool bRotationSensitive : { false, true })
		{
			PointCloudRecognizer first(PointCloudOptions{ .bRotationSensitive = bRotationSensitive });
			PointCloudRecognizer second(PointCloudOptions{ .bRotationSensitive = bRotationSensitive });
			AddShapes(first);
			AddShapes(second);
			for (const Drawing& drawing : MakeDrawings(30))
			{
				const PointCloudResult a = first.Recognize(drawing.Strokes);
				const PointCloudResult b = second.Recognize(drawing.Strokes);
				const PointCloudResult again = first.Recognize(drawing.Strokes);
				for (const PointCloudResult* other : { &b, &again })
				{
					Require(a.Best && other->Best && a.Best->Template == other->Best->Template && a.RunnerUp->Template == other->RunnerUp->Template,
						drawing.What + " should match the same templates every time");
					Require(std::memcmp(&a.Best->Distance, &other->Best->Distance, sizeof(float)) == 0 &&
							std::memcmp(&a.RunnerUp->Distance, &other->RunnerUp->Distance, sizeof(float)) == 0 &&
							std::memcmp(&a.Best->Up, &other->Best->Up, sizeof(glm::vec2)) == 0,
						drawing.What + " should give the same distances and up to the bit every time");
				}
			}
		}
	}

	// The text format: read, written and read back exactly; problems with their line
	void TestTemplateText()
	{
		const std::string text =
			"# Two shapes\n"
			"template Arrow\n"
			"stroke 0 0  1 0\r\n"
			"\n"
			"   stroke 0.8 0.2\t1 0  0.8 -0.2   \n"
			"template Two Words\n"
			"stroke -1.5e2 3.25\n"
			"template Arrow\n"
			"stroke 0 0 0 1\n";
		std::vector<Nyx::StrokeTemplate> templates;
		std::string error;
		Require(Nyx::ParseStrokeTemplates(text, templates, error), "The templates should read: " + error);
		Require(templates.size() == 3, "Three templates should be read");
		Require(templates[0].Name == "Arrow" && templates[1].Name == "Two Words" && templates[2].Name == "Arrow", "The names should be read whole");
		Require(templates[0].Strokes.size() == 2 && templates[0].Strokes[1].size() == 3, "The first template should have two strokes, of 2 and 3 points");
		Require(templates[0].Strokes[1][2] == glm::vec2(0.8f, -0.2f) && templates[1].Strokes[0][0] == glm::vec2(-150.0f, 3.25f),
			"The points should be read as written");

		// Written and read back to the bit, also numbers that need every digit
		templates[1].Strokes[0].push_back(glm::vec2(1.0f / 3.0f, -0.1f));
		std::vector<Nyx::StrokeTemplate> again;
		Require(Nyx::ParseStrokeTemplates(Nyx::WriteStrokeTemplates(templates), again, error), "Written templates should read back: " + error);
		Require(again.size() == templates.size(), "Every template should be read back");
		for (size_t index = 0; index < templates.size(); ++index)
		{
			Require(again[index].Name == templates[index].Name && again[index].Strokes == templates[index].Strokes,
				"Template " + std::to_string(index) + " should read back exactly");
		}

		// Usable at once
		PointCloudRecognizer recognizer;
		for (const Nyx::StrokeTemplate& entry : again)
		{
			Require(recognizer.AddTemplate(entry.Name, entry.Strokes), "A read template should make a template");
		}

		const auto requireProblem = [](const std::string& badText, const std::string& expected)
		{
			std::vector<Nyx::StrokeTemplate> read;
			std::string problem;
			Require(!Nyx::ParseStrokeTemplates(badText, read, problem), "Should not read: " + badText);
			Require(problem == expected, "The problem should be \"" + expected + "\", was \"" + problem + "\"");
		};
		requireProblem("stroke 0 0 1 1\n", "line 1: a stroke before any template");
		requireProblem("template A\nstroke 0 0 1\n", "line 2: a stroke needs x y pairs, 3 numbers given");
		requireProblem("template A\nstroke\n", "line 2: a stroke needs x y pairs, 0 numbers given");
		requireProblem("template A\nstroke 0 0 1 x\n", "line 2: 'x' is not a number");
		requireProblem("template A\nstroke 0 0 1 1.5.2\n", "line 2: '1.5.2' is not a number");
		requireProblem("template\n", "line 1: a template needs a name");
		requireProblem("template A\n# nothing\ntemplate B\nstroke 0 0 1 1\n", "line 2: template 'A' has no strokes");
		requireProblem("template A\nstroke 0 0 1 1\ntemplate B\n", "line 3: template 'B' has no strokes");
		requireProblem("template A\nstrokes 0 0 1 1\n", "line 2: expected 'template' or 'stroke', found 'strokes'");

		Nyx::Tests::TestDirectory directory("NyxPointCloudTests");
		const std::filesystem::path path = directory.Path / "Shapes.txt";
		{
			std::ofstream file(path, std::ios::binary);
			file << Nyx::WriteStrokeTemplates(templates);
		}
		std::vector<Nyx::StrokeTemplate> loaded;
		Require(Nyx::LoadStrokeTemplates(path, loaded, error) && loaded.size() == 3, "The file should load: " + error);
		Require(!Nyx::LoadStrokeTemplates(directory.Path / "Missing.txt", loaded, error) && error.find("Missing.txt") != std::string::npos,
			"A missing file should fail, naming it");
	}

	// 50 templates: under a millisecond a recognition in Release (timing is only checked there)
	void TestFiftyTemplatesTime()
	{
		std::vector<std::vector<Stroke>> shapes = { MakeCircle(), MakeTriangle(), MakeArrow(), MakeZigzag(), MakeZigzag(3), MakeZigzag(6) };
		for (int corners = 4; corners <= 7; ++corners)
		{
			Stroke polygon;
			for (int corner = 0; corner <= corners; ++corner)
			{
				const float angle = 2.0f * Pi * static_cast<float>(corner) / static_cast<float>(corners);
				if (!polygon.empty())
				{
					AddLine(polygon, polygon.back(), glm::vec2(std::sin(angle), std::cos(angle)), 6);
				}
				else
				{
					polygon.push_back(glm::vec2(std::sin(angle), std::cos(angle)));
				}
			}
			shapes.push_back({ polygon });
		}

		for (const bool bRotationSensitive : { true, false })
		{
			PointCloudRecognizer recognizer(PointCloudOptions{ .bRotationSensitive = bRotationSensitive });
			Shaky shaky(5);
			for (size_t index = 0; recognizer.GetTemplateCount() < 50; ++index)
			{
				recognizer.AddTemplate("Shape " + std::to_string(index % shapes.size()), shaky.Shake(shapes[index % shapes.size()], 0.03f));
			}

			const std::vector<Drawing> drawings = MakeDrawings(40);
#ifdef NDEBUG
			constexpr int Rounds = 20;
#else
			constexpr int Rounds = 1;
#endif
			// Each drawing's fastest recognition, so that other programs on the machine count less;
			// the median of the drawings, and the slowest
			std::vector<double> times(drawings.size(), 1e30);
			for (int round = 0; round < Rounds; ++round)
			{
				for (size_t index = 0; index < drawings.size(); ++index)
				{
					const auto start = std::chrono::steady_clock::now();
					const PointCloudResult result = recognizer.Recognize(drawings[index].Strokes);
					times[index] = std::min(times[index], std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
					Require(result.Best.has_value(), "Something should be closest");
				}
			}
			std::sort(times.begin(), times.end());
			const double median = times[times.size() / 2];
			std::cout << "50 templates, rotation " << (bRotationSensitive ? "sensitive" : "insensitive") << ": " << median << " ms median, "
					  << times.back() << " ms for the slowest drawing" << std::endl;
#ifdef NDEBUG
			Require(median < 1.0, "50 templates should take under 1 ms in Release, took " + std::to_string(median) + " ms");
#endif
		}
	}
}

int main()
{
	try
	{
		TestPointCloud();
		TestKnownShapesRecognised(false);
		TestKnownShapesRecognised(true);
		TestTurnedShapesRecognised();
		TestRotationSensitive();
		TestDeterministic();
		TestTemplateText();
		TestFiftyTemplatesTime();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Point cloud recognizer test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All point cloud recognizer tests passed.\n";
	return 0;
}
