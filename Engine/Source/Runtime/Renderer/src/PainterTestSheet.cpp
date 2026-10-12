#include "PainterTestSheet.h"

#include "Painter.h"

#include <cmath>
#include <string>

namespace Nyx::Engine
{
	namespace
	{
		constexpr float Pi = 3.14159265f;

		const glm::vec4 Light{ 0.92f, 0.93f, 0.96f, 1.0f };
		const glm::vec4 Muted{ 0.55f, 0.58f, 0.66f, 1.0f };
		const glm::vec4 Accent{ 1.0f, 0.55f, 0.15f, 1.0f };
		const glm::vec4 Blue{ 0.3f, 0.55f, 1.0f, 1.0f };
		const glm::vec4 Red{ 0.95f, 0.3f, 0.32f, 1.0f };
		const glm::vec4 Green{ 0.3f, 0.85f, 0.45f, 1.0f };

		// Where the samples of a row start, right of its label
		constexpr float SamplesX = 150.0f;

		void DrawRowLabel(Painter& painter, ImFont* font, float rowY, const char* label)
		{
			painter.AddText(font, 13.0f, { 16.0f, rowY + 4.0f }, Muted, label);
		}

		PainterRect RectAt(float x, float y, float width, float height)
		{
			return PainterRect{ { x, y }, { x + width, y + height } };
		}

		void DrawRects(Painter& painter, float y)
		{
			painter.AddRect(RectAt(SamplesX, y + 8, 60, 50), PainterStyle::Filled(Accent));
			painter.AddRoundedRect(RectAt(225, y + 8, 60, 50), 10.0f, PainterStyle::Filled(Blue));
			painter.AddRoundedRect(RectAt(300, y + 8, 60, 50), PainterCornerRadii{ 0.0f, 24.0f, 6.0f, 16.0f }, PainterStyle::Filled(Green));

			// A pill: corners as round as the rect is high
			painter.AddRoundedRect(RectAt(375, y + 20, 80, 26), 13.0f, PainterStyle::Filled(Light));

			painter.AddRect(RectAt(470, y + 8, 50, 50), PainterStyle::Outlined(Light, 1.0f));
			painter.AddRoundedRect(RectAt(535, y + 8, 60, 50), 8.0f, PainterStyle::Outlined(Accent, 3.0f));

			// Half transparent, overlapping: the overlap is darker where both cover it
			painter.AddRect(RectAt(610, y + 8, 60, 36), PainterStyle::Filled(glm::vec4(Red.r, Red.g, Red.b, 0.6f)));
			painter.AddRect(RectAt(640, y + 24, 60, 36), PainterStyle::Filled(glm::vec4(Blue.r, Blue.g, Blue.b, 0.6f)));
		}

		void DrawCirclesAndSectors(Painter& painter, float y)
		{
			const float centreY = y + 33.0f;
			painter.AddCircle({ 180, centreY }, 25.0f, PainterStyle::Filled(Accent));
			painter.AddCircle({ 245, centreY }, 25.0f, PainterStyle::Outlined(Light, 2.0f));

			// Small circles show how anti-aliasing holds up near a pixel's size.
			painter.AddCircle({ 290, centreY }, 1.5f, PainterStyle::Filled(Light));
			painter.AddCircle({ 302, centreY }, 3.0f, PainterStyle::Filled(Light));
			painter.AddCircle({ 320, centreY }, 6.0f, PainterStyle::Filled(Light));

			// Angles grow clockwise from the right, since y points down.
			painter.AddCircleSector({ 380, centreY }, 0.0f, 25.0f, -0.5f * Pi, 0.25f * Pi, PainterStyle::Filled(Blue));
			painter.AddCircleSector({ 445, centreY }, 15.0f, 25.0f, 0.0f, 2.0f * Pi, PainterStyle::Filled(Green));
			painter.AddArc({ 510, centreY + 4 }, 20.0f, 5.0f, 0.75f * Pi, 2.25f * Pi, PainterStyle::Filled(Accent));
			painter.AddCircleSector({ 575, centreY }, 12.0f, 25.0f, 0.3f, 2.0f * Pi, PainterStyle::Filled(Red));
			painter.AddCircleSector({ 640, centreY + 10 }, 10.0f, 25.0f, -0.9f * Pi, -0.1f * Pi, PainterStyle::Outlined(Light, 1.5f));
		}

		void DrawLines(Painter& painter, float y)
		{
			// A fan: every angle from flat to upright, one pixel wide
			const glm::vec2 fanCentre{ SamplesX + 10.0f, y + 64.0f };
			for (int step = 0; step <= 12; ++step)
			{
				const float angle = -0.5f * Pi * static_cast<float>(step) / 12.0f;
				painter.AddLine(fanCentre, fanCentre + 58.0f * glm::vec2(std::cos(angle), std::sin(angle)), 1.0f, EPainterLineCap::Flat, PainterStyle::Filled(Light));
			}

			// Widths from half a pixel up, slightly sloped, so every edge is anti-aliased
			const float widths[] = { 0.5f, 1.0f, 1.5f, 2.0f, 3.0f };
			for (int index = 0; index < 5; ++index)
			{
				const float lineY = y + 8.0f + 12.0f * static_cast<float>(index);
				painter.AddLine({ 240, lineY }, { 340, lineY + 4.0f }, widths[index], EPainterLineCap::Flat, PainterStyle::Filled(Light));
			}

			// Thick lines: round caps reach past the end points, flat caps stop at them.
			painter.AddLine({ 370, y + 18 }, { 450, y + 18 }, 10.0f, EPainterLineCap::Round, PainterStyle::Filled(Accent));
			painter.AddLine({ 370, y + 46 }, { 450, y + 46 }, 10.0f, EPainterLineCap::Flat, PainterStyle::Filled(Accent));

			// A graph: a sine wave of short round-capped segments
			glm::vec2 previous{ 480.0f, y + 33.0f };
			for (int step = 1; step <= 40; ++step)
			{
				const float x = 480.0f + 220.0f * static_cast<float>(step) / 40.0f;
				const glm::vec2 point{ x, y + 33.0f - 24.0f * std::sin(static_cast<float>(step) * 0.35f) };
				painter.AddLine(previous, point, 2.0f, EPainterLineCap::Round, PainterStyle::Filled(Green));
				previous = point;
			}
		}

		void DrawTriangles(Painter& painter, float y)
		{
			painter.AddTriangle({ SamplesX, y + 60 }, { 200, y + 8 }, { 250, y + 60 }, PainterStyle::Filled(Blue));
			painter.AddTriangle({ 270, y + 60 }, { 320, y + 8 }, { 370, y + 60 }, PainterStyle::Outlined(Light, 2.0f));

			// A sliver: thinner than a pixel at its tip
			painter.AddTriangle({ 390, y + 60 }, { 490, y + 52 }, { 490, y + 60 }, PainterStyle::Filled(Light));

			// A play button
			painter.AddCircle({ 540, y + 33 }, 25.0f, PainterStyle::Filled(Accent));
			painter.AddTriangle({ 532, y + 20 }, { 554, y + 33 }, { 532, y + 46 }, PainterStyle::Filled(Light));

			// Small chevrons, as in drop-down menus
			painter.AddTriangle({ 600, y + 28 }, { 610, y + 28 }, { 605, y + 34 }, PainterStyle::Filled(Light));
			painter.AddTriangle({ 625, y + 34 }, { 635, y + 34 }, { 630, y + 28 }, PainterStyle::Filled(Light));
		}

		void DrawShadowsAndGlows(Painter& painter, ImFont* font, float y)
		{
			PainterStyle card = PainterStyle::Filled(Light);
			card.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.6f);
			card.Effects.Shadow.Offset = glm::vec2(0.0f, 6.0f);
			card.Effects.Shadow.Blur = 14.0f;
			painter.AddRoundedRect(RectAt(SamplesX, y + 8, 110, 56), 8.0f, card);

			PainterStyle hardShadow = PainterStyle::Filled(Blue);
			hardShadow.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.8f);
			hardShadow.Effects.Shadow.Offset = glm::vec2(5.0f, 5.0f);
			painter.AddRect(RectAt(290, y + 8, 60, 50), hardShadow);

			PainterStyle glowing = PainterStyle::Filled(Accent);
			glowing.Effects.Glow.Color = Accent;
			glowing.Effects.Glow.Distance = 16.0f;
			glowing.Effects.Glow.Strength = 0.8f;
			painter.AddCircle({ 400, y + 36 }, 14.0f, glowing);

			// A switch: track and knob as one group, so they cast one shadow together
			PainterEffects switchShadow;
			switchShadow.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.6f);
			switchShadow.Shadow.Offset = glm::vec2(0.0f, 3.0f);
			switchShadow.Shadow.Blur = 6.0f;
			painter.BeginGroup(switchShadow);
			painter.AddRoundedRect(RectAt(450, y + 24, 64, 26), 13.0f, PainterStyle::Filled(Green));
			painter.AddCircle({ 500, y + 37 }, 11.0f, PainterStyle::Filled(Light));
			painter.EndGroup();

			// A focused button: a glow around it, a label in it
			PainterStyle button = PainterStyle::Filled(Blue);
			button.Effects.Glow.Color = Blue;
			button.Effects.Glow.Distance = 10.0f;
			button.Effects.Glow.Strength = 0.7f;
			painter.AddRoundedRect(RectAt(550, y + 20, 110, 34), 8.0f, button);
			painter.AddText(font, 13.0f, { 580.0f, y + 30.0f }, Light, "Focused");
		}

		void DrawTextAndImages(Painter& painter, ImFont* font, ImTextureRef image, float y)
		{
			const float sizes[] = { 10.0f, 13.0f, 16.0f, 24.0f };
			float lineY = y + 4.0f;
			for (const float size : sizes)
			{
				const std::string sample = "Nyx " + std::to_string(static_cast<int>(size)) + " px";
				painter.AddText(font, size, { SamplesX, lineY }, Light, sample);
				lineY += size + 6.0f;
			}

			painter.AddText(font, 13.0f, { 290.0f, y + 4.0f }, Light, "Two lines,\nUTF-8: \xC3\xA4\xC3\xB6\xC3\xBC \xC2\xB0");

			// Text cut off at a clip rect, with the rect's outline
			const PainterRect clip = RectAt(290, y + 50, 90, 22);
			painter.AddRect(clip, PainterStyle::Outlined(Muted, 1.0f));
			painter.PushClipRect(clip);
			painter.AddText(font, 13.0f, { 296.0f, y + 54.0f }, Accent, "Clipped at the edge of its rect");
			painter.PopClipRect();

			// Of a texture ImGui fills as it goes (the font atlas), only the part filled so far
			PainterRect uvs{ { 0.0f, 0.0f }, { 1.0f, 1.0f } };
			if (const ImTextureData* data = image._TexData; data != nullptr && data->UsedRect.w > 0 && data->UsedRect.h > 0)
			{
				uvs.Max = glm::vec2(static_cast<float>(data->UsedRect.x + data->UsedRect.w) / static_cast<float>(data->Width),
					static_cast<float>(data->UsedRect.y + data->UsedRect.h) / static_cast<float>(data->Height));
			}

			// The image as it is, then tinted, each on a card; the second card casts a shadow.
			PainterStyle card = PainterStyle::Filled(glm::vec4(0.2f, 0.21f, 0.25f, 1.0f));
			painter.AddRoundedRect(RectAt(420, y + 4, 120, 110), 6.0f, card);
			painter.AddImage(image, RectAt(425, y + 9, 110, 100), PainterStyle::Filled(glm::vec4(1.0f)), uvs);

			card.Effects.Shadow.Color = glm::vec4(0.0f, 0.0f, 0.0f, 0.6f);
			card.Effects.Shadow.Offset = glm::vec2(0.0f, 5.0f);
			card.Effects.Shadow.Blur = 10.0f;
			painter.AddRoundedRect(RectAt(560, y + 4, 120, 110), 6.0f, card);
			painter.AddImage(image, RectAt(565, y + 9, 110, 100), PainterStyle::Filled(Accent), uvs);
		}
	}

	void DrawPainterTestSheet(Painter& painter, ImFont* font, ImTextureRef image)
	{
		const char* const labels[] = { "Rects", "Circles, sectors", "Lines", "Triangles", "Shadows, glows", "Text, images" };
		constexpr float RowHeight = 80.0f;
		for (int row = 0; row < 6; ++row)
		{
			DrawRowLabel(painter, font, 16.0f + RowHeight * static_cast<float>(row), labels[row]);
		}

		DrawRects(painter, 16.0f);
		DrawCirclesAndSectors(painter, 16.0f + RowHeight);
		DrawLines(painter, 16.0f + RowHeight * 2.0f);
		DrawTriangles(painter, 16.0f + RowHeight * 3.0f);
		DrawShadowsAndGlows(painter, font, 16.0f + RowHeight * 4.0f);
		DrawTextAndImages(painter, font, image, 16.0f + RowHeight * 5.0f);
	}
}
