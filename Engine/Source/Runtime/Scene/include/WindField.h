#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace Nyx
{
	// A box of cells along the world's axes, x fastest, then y, then z: where a wind field is asked
	// for its air at once (the texture of local air round a camera that grass and trees bend in)
	struct WindFieldGrid
	{
		// The corner of the first cell, in the world, in metres
		glm::vec3 Origin{ 0.0f };
		float CellSize = 1.0f;
		glm::ivec3 Cells{ 0 };

		size_t GetCellCount() const
		{
			return static_cast<size_t>(glm::max(Cells.x, 0)) * static_cast<size_t>(glm::max(Cells.y, 0)) * static_cast<size_t>(glm::max(Cells.z, 0));
		}

		glm::vec3 GetCellCentre(int32_t x, int32_t y, int32_t z) const
		{
			return Origin + (glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) + 0.5f) * CellSize;
		}
	};

	// The air a game keeps in its world on top of the prevailing wind (WindState): its own winds, the
	// draught of a fire, the air a moving body pushes (Docs/Design/wind.md). What it gives is what
	// it adds to the prevailing wind, 0 where it adds nothing. A game gives one to its world
	// (GameWorld::SetWindField(), which Nyx's game hands to the renderer,
	// IRenderer::SetWorldWindField()); the renderer asks it for the particles' air, once per emitter
	// a frame, and for the cells of each view's texture of local air, which grass and trees bend in.
	// It must outlive its world's use of it, and may be asked during IRenderer's DrawFrame(), on the
	// thread that calls it. Nyx knows nothing else of it.
	class IWindField
	{
	public:
		virtual ~IWindField() = default;

		// The air's speed it adds at each of `positions` (in the world, in metres), in metres a
		// second, into `outVelocities` (as many), at the world's `time` in seconds
		virtual void SampleWind(std::span<const glm::vec3> positions, float time, std::span<glm::vec3> outVelocities) const = 0;

		// The same at the centre of each of the grid's cells, into `outVelocities` (one a cell, in
		// the grid's order), which comes filled with 0: a field that knows where it is writes only
		// its own cells; SampleWindGridAtCentres() asks SampleWind() for every one
		virtual void SampleWindGrid(const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities) const = 0;
	};

	// SampleWindGrid() by asking the field's SampleWind() for the centre of every cell
	void SampleWindGridAtCentres(const IWindField& field, const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities);
}
