#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

#ifndef NYX_DEBUG_DRAW
	#define NYX_DEBUG_DRAW 1
#endif

namespace Nyx
{
	// One line of debug drawing, in world space
	struct DebugLine
	{
		glm::vec3 From{ 0.0f };
		glm::vec3 To{ 0.0f };
		glm::vec3 Color{ 1.0f };
		// Its owner's time left, in seconds; 0: until the owner's next Advance()
		float SecondsLeft = 0.0f;
	};

	// Shapes drawn as lines over a world, to see what code does: forces, anchors, bounds. Plain data:
	// whoever owns it (a GameWorld, the editor's overlays) advances it, and the renderer draws its
	// lines in the views of that world (IRenderer::SetWorldDebugDraw()). Release builds compile it
	// out (NYX_DEBUG_DRAW 0): drawing does nothing, and there are never any lines.
	class DebugDraw
	{
	public:
		static constexpr bool Enabled = NYX_DEBUG_DRAW != 0;

		// More lines than this are dropped, and counted (GetDroppedLineCount())
		static constexpr size_t MaxLines = 32768;

		// `seconds`: how long a shape stays, in its owner's time; 0 until the owner's next Advance(),
		// which for a GameWorld is the next step. Colors are linear RGB.
		void Line(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color, float seconds = 0.0f);
		// A line with a head at `to`, a quarter of its length, at least long enough to see
		void Arrow(const glm::vec3& from, const glm::vec3& to, const glm::vec3& color, float seconds = 0.0f);
		// A box between two corners, along the world's axes
		void Box(const glm::vec3& min, const glm::vec3& max, const glm::vec3& color, float seconds = 0.0f);
		// A box between two corners in the space of `transform`: an object's bounds
		void Box(const glm::mat4& transform, const glm::vec3& localMin, const glm::vec3& localMax, const glm::vec3& color,
			float seconds = 0.0f);
		// Three circles around the axes
		void Sphere(const glm::vec3& center, float radius, const glm::vec3& color, float seconds = 0.0f);
		// The axes of `transform` at its origin, `length` long: X red, Y green, Z blue
		void Axes(const glm::mat4& transform, float length, float seconds = 0.0f);
		// A small cross along the axes: a point
		void Cross(const glm::vec3& point, float size, const glm::vec3& color, float seconds = 0.0f);

		// The owner's next step or frame, `seconds` after the last: drops the shapes drawn until then
		// and those whose time ran out
		void Advance(float seconds);
		// Drops every shape
		void Clear();

		const std::vector<DebugLine>& GetLines() const { return Lines; }
		size_t GetDroppedLineCount() const { return DroppedLines; }

	private:
		std::vector<DebugLine> Lines;
		size_t DroppedLines = 0;
	};
}
