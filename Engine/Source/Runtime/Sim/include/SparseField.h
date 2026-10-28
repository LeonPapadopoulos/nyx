#pragma once

#include <glm/glm.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

// A field over the world, a number or a vector in every cell of a grid, kept only where something
// happens: in bricks of 8 x 8 x 8 cells, made and freed as needed, at most a fixed number of them,
// so that what a step of the field costs has a bound. Outside every brick the field has its
// ambient value. What the values mean (a temperature, a concentration, a wind) and every rate are
// the caller's.
//
// Deterministic: the bricks are kept sorted by their coordinates and always walked in that order,
// and every step that reads neighbours (Diffuse(), Advect()) reads only the values before the step
// and writes the new ones apart (double buffered), so the result doesn't depend on the order cells
// are visited in, and spreading the bricks over threads later changes nothing. Only correctly
// rounded arithmetic (no exp or sin of a C library): the same steps give the same values to the bit
// on any machine running the same build.
namespace Nyx
{
	// A brick's place: the cells it holds are BrickSize times these, up to BrickSize - 1 more
	struct BrickCoord
	{
		int32_t X = 0;
		int32_t Y = 0;
		int32_t Z = 0;

		// The order bricks are kept and walked in: by Z, then Y, then X
		friend bool operator<(const BrickCoord& a, const BrickCoord& b)
		{
			if (a.Z != b.Z)
			{
				return a.Z < b.Z;
			}
			if (a.Y != b.Y)
			{
				return a.Y < b.Y;
			}
			return a.X < b.X;
		}

		friend bool operator==(const BrickCoord& a, const BrickCoord& b) = default;
	};

	struct SparseFieldSettings
	{
		// The length of a cell's side, in metres
		float CellSize = 0.5f;

		// No more bricks than this ever exist; with it a step of the field costs at most
		// MaxBricks * 512 cells' work
		uint32_t MaxBricks = 64;
	};

	// T is float or glm::vec3
	template<typename T>
	class SparseField
	{
	public:
		static constexpr int32_t BrickSize = 8;
		static constexpr size_t CellsPerBrick = BrickSize * BrickSize * BrickSize;

		SparseField(SparseFieldSettings settings, T ambient);

		const SparseFieldSettings& GetSettings() const
		{
			return Settings;
		}

		T GetAmbient() const
		{
			return Ambient;
		}

		// Bricks

		// Ends a round of requests: from now on no brick counts as requested, until Allocate() asks
		// for it again. Call it before each step's Allocate() calls, so that only the bricks asked
		// for this step are kept from being dropped or freed.
		void ClearRequests();

		// Makes the bricks that cover the box, at the ambient value, and marks them requested, nearest
		// the box's centre first. At the cap, a new brick takes the place of the least important brick
		// not requested since ClearRequests(): the one whose values are all nearest the ambient (ties:
		// the last in the bricks' order), whose values are lost. With every brick requested it is
		// refused: within a round, bricks go to the calls in the order they are made, so the
		// caller's order of calls is its priority. Returns how many bricks of the box were refused.
		size_t Allocate(glm::vec3 boxMin, glm::vec3 boxMax);

		// Frees the bricks not requested since ClearRequests() whose every value is within tolerance
		// of the ambient (for vectors: in length): what has settled back. Returns how many.
		size_t FreeSettled(float tolerance);

		void Clear();

		size_t GetBrickCount() const
		{
			return Order.size();
		}

		// In the bricks' order
		std::vector<BrickCoord> GetBricks() const;

		bool HasBrick(BrickCoord coord) const
		{
			return FindSlot(coord) >= 0;
		}

		// The brick a point is in
		BrickCoord GetBrickAt(glm::vec3 position) const;

		// Values

		// Every brick back to the ambient, e.g. before a field made again from scratch each step
		void ResetToAmbient();

		// Spreads amount over the cells whose centres are in the box (or, for a box smaller than a
		// cell, the cell its centre is in), in proportion to weight(cell centre) (1 for each without):
		// the sum over the cells is amount, whatever the box's size. Cells in no brick lose their
		// share. Returns the part of the weight that found a brick, from 0 to 1.
		float AddSource(glm::vec3 boxMin, glm::vec3 boxMax, T amount, const std::function<float(glm::vec3)>& weight = {});

		// Each cell's value, with the centre of its cell, to read or change: bricks in their order,
		// cells by z, then y, then x
		void ForEachCell(const std::function<void(glm::vec3 centre, T& value)>& visit);
		void ForEachCell(const std::function<void(glm::vec3 centre, const T& value)>& visit) const;

		// Spreads the values to their neighbours, explicitly, as d/dt = diffusivity * laplacian, in
		// m^2/s; the ambient flows in from outside the bricks. Stable at any step: a step longer than
		// the explicit limit (CellSize^2 / (6 diffusivity)) is taken in as many equal parts as it needs.
		void Diffuse(float diffusivity, float deltaSeconds);

		// Draws every value towards the ambient at rate per second, as an implicit step: by
		// 1 / (1 + rate * deltaSeconds), about exp(-rate * deltaSeconds) for short steps, stable at any.
		void Decay(float rate, float deltaSeconds);

		// Carries the values along a velocity field, in m/s, semi-Lagrangian: each cell takes the value
		// found where its centre was deltaSeconds ago (Sample()), so it is stable at any step and
		// smooths a little. What would come from outside the bricks is the ambient; what leaves them
		// is lost. The velocity may be this field itself.
		void Advect(const SparseField<glm::vec3>& velocity, float deltaSeconds);

		// The value at a point, trilinear between the eight cell centres round it; the ambient for
		// cells in no brick, so it is the ambient wherever no brick is near
		T Sample(glm::vec3 position) const;

		// A cell's value, by the cell's coordinates (position / CellSize, rounded down)
		T GetCell(glm::ivec3 cell) const;

	private:
		struct Brick
		{
			BrickCoord Coord;
			bool bRequested = false;

			// The slots of the bricks round this one, by (dx + 1) + 3 (dy + 1) + 9 (dz + 1); -1 for none
			std::array<int32_t, 27> Neighbours{};

			// Two buffers: the values, and where the next ones are written
			std::array<std::vector<T>, 2> Values;
		};

		int32_t FindSlot(BrickCoord coord) const;
		// Each links the brick with the bricks round it, or unlinks it
		int32_t MakeBrick(BrickCoord coord);
		void FreeBrick(int32_t slot);
		float GetDeviation(const Brick& brick) const;

		// A cell's current value, by global cell coordinates, near a brick
		T GetCellNear(const Brick& brick, glm::ivec3 cell) const;
		// Trilinear in cell units (cell centres at whole numbers plus a half), near a brick
		T SampleNear(const Brick& brick, glm::vec3 cellPosition) const;

		// A brick's current values with a cell of its neighbours' (or the ambient) all round:
		// PaddedSize cells a side, for the steps that read neighbours without asking where each is
		static constexpr int32_t PaddedSize = BrickSize + 2;
		void FillPadded(const Brick& brick, std::vector<T>& outPadded) const;

		void SwapBuffers()
		{
			Current = 1 - Current;
		}

	private:
		SparseFieldSettings Settings;
		T Ambient;

		// Bricks in slots, which stay where they are while others come and go
		std::vector<Brick> Slots;
		std::vector<int32_t> FreeSlots;

		// The slots in use, sorted by coordinate: the order everything walks
		std::vector<int32_t> Order;

		// Which of each brick's two buffers holds the values
		int Current = 0;

		// FillPadded()'s, kept for the next step
		std::vector<T> Padded;

		template<typename U>
		friend class SparseField;
	};

	extern template class SparseField<float>;
	extern template class SparseField<glm::vec3>;
}
