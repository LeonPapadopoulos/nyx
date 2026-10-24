#include "NyxPCH.h"
#include "SparseField.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Nyx
{
	namespace
	{
		constexpr int32_t Size = SparseField<float>::BrickSize;

		// A brick has 2^SizeShift cells a side: a cell's brick is its coordinate shifted, rounding
		// down also below zero (C++20 shifts signed numbers arithmetically), and its place in the
		// brick the low bits; no division in the loops over cells
		constexpr int32_t SizeShift = 3;
		constexpr int32_t SizeMask = Size - 1;
		static_assert((1 << SizeShift) == Size);

		int32_t GetBrickOf(int32_t cell)
		{
			return cell >> SizeShift;
		}

		// Rounded down, without a call into the C library for every cell
		glm::ivec3 FloorToCell(glm::vec3 value)
		{
			const glm::ivec3 truncated(value);
			return truncated - glm::ivec3(glm::lessThan(value, glm::vec3(truncated)));
		}

		size_t GetCellIndex(int32_t x, int32_t y, int32_t z)
		{
			return static_cast<size_t>(x + Size * (y + Size * z));
		}

		float GetMagnitude(float value)
		{
			return std::abs(value);
		}

		float GetMagnitude(const glm::vec3& value)
		{
			return glm::length(value);
		}

		int32_t GetNeighbourIndex(int32_t dx, int32_t dy, int32_t dz)
		{
			return (dx + 1) + 3 * (dy + 1) + 9 * (dz + 1);
		}

		constexpr int32_t PaddedSide = SparseField<float>::BrickSize + 2;

		// In a padded brick, the cell at x, y, z of the brick, -1 to BrickSize each
		size_t GetPaddedIndex(int32_t x, int32_t y, int32_t z)
		{
			return static_cast<size_t>((x + 1) + PaddedSide * ((y + 1) + PaddedSide * (z + 1)));
		}

		// Between eight corners (x, then y, then z: corner bits x 1, y 2, z 4), at t in each
		template<typename T>
		T Trilinear(const T* corners, glm::vec3 t)
		{
			const T x00 = corners[0] + (corners[1] - corners[0]) * t.x;
			const T x10 = corners[2] + (corners[3] - corners[2]) * t.x;
			const T x01 = corners[4] + (corners[5] - corners[4]) * t.x;
			const T x11 = corners[6] + (corners[7] - corners[6]) * t.x;
			const T y0 = x00 + (x10 - x00) * t.y;
			const T y1 = x01 + (x11 - x01) * t.y;
			return y0 + (y1 - y0) * t.z;
		}

		glm::ivec3 GetCellOf(glm::vec3 position, float cellSize)
		{
			return FloorToCell(position / cellSize);
		}

		BrickCoord GetBrickOfCell(glm::ivec3 cell)
		{
			return BrickCoord{ GetBrickOf(cell.x), GetBrickOf(cell.y), GetBrickOf(cell.z) };
		}

		// A box's cells: those whose centres are in it, or the one its centre is in
		void GetBoxCells(glm::vec3 boxMin, glm::vec3 boxMax, float cellSize, glm::ivec3& outFirst, glm::ivec3& outLast)
		{
			const glm::vec3 low = glm::min(boxMin, boxMax) / cellSize - 0.5f;
			const glm::vec3 high = glm::max(boxMin, boxMax) / cellSize - 0.5f;
			outFirst = glm::ivec3(glm::ceil(low));
			outLast = glm::ivec3(glm::floor(high));
			for (int axis = 0; axis < 3; ++axis)
			{
				if (outFirst[axis] > outLast[axis])
				{
					const int32_t centre = static_cast<int32_t>(std::floor(0.5f * (low[axis] + high[axis]) + 0.5f));
					outFirst[axis] = centre;
					outLast[axis] = centre;
				}
			}
		}
	}

	template<typename T>
	SparseField<T>::SparseField(SparseFieldSettings settings, T ambient)
		: Settings(settings)
		, Ambient(ambient)
	{
		if (!(Settings.CellSize > 0.0f))
		{
			Settings.CellSize = 0.5f;
		}
	}

	template<typename T>
	void SparseField<T>::ClearRequests()
	{
		for (const int32_t slot : Order)
		{
			Slots[slot].bRequested = false;
		}
	}

	template<typename T>
	size_t SparseField<T>::Allocate(glm::vec3 boxMin, glm::vec3 boxMax)
	{
		const BrickCoord first = GetBrickAt(glm::min(boxMin, boxMax));
		const BrickCoord last = GetBrickAt(glm::max(boxMin, boxMax));

		// Nearest the box's centre first, in brick units; ties in the bricks' order
		struct Wanted
		{
			BrickCoord Coord;
			int64_t Distance = 0;
		};
		std::vector<Wanted> wanted;
		const int64_t centreX = int64_t(first.X) + last.X;
		const int64_t centreY = int64_t(first.Y) + last.Y;
		const int64_t centreZ = int64_t(first.Z) + last.Z;
		for (int32_t z = first.Z; z <= last.Z; ++z)
		{
			for (int32_t y = first.Y; y <= last.Y; ++y)
			{
				for (int32_t x = first.X; x <= last.X; ++x)
				{
					// Twice the distance in each axis, to stay in whole numbers
					const int64_t dx = 2 * int64_t(x) - centreX;
					const int64_t dy = 2 * int64_t(y) - centreY;
					const int64_t dz = 2 * int64_t(z) - centreZ;
					wanted.push_back(Wanted{ BrickCoord{ x, y, z }, dx * dx + dy * dy + dz * dz });
				}
			}
		}
		std::stable_sort(wanted.begin(), wanted.end(), [](const Wanted& a, const Wanted& b)
		{
			return a.Distance < b.Distance;
		});

		// The bricks there already first, so that none of them makes way for a new one of the same box
		for (const Wanted& brick : wanted)
		{
			if (const int32_t slot = FindSlot(brick.Coord); slot >= 0)
			{
				Slots[slot].bRequested = true;
			}
		}

		size_t refused = 0;
		bool bChanged = false;
		for (const Wanted& brick : wanted)
		{
			if (FindSlot(brick.Coord) >= 0)
			{
				continue;
			}

			if (Order.size() >= Settings.MaxBricks)
			{
				// The least important brick not requested: nearest the ambient, the last in order on a tie
				int32_t leastImportant = -1;
				float leastDeviation = std::numeric_limits<float>::max();
				for (const int32_t slot : Order)
				{
					if (!Slots[slot].bRequested)
					{
						const float deviation = GetDeviation(Slots[slot]);
						if (deviation <= leastDeviation)
						{
							leastDeviation = deviation;
							leastImportant = slot;
						}
					}
				}
				if (leastImportant < 0)
				{
					++refused;
					continue;
				}
				FreeBrick(leastImportant);
			}

			const int32_t slot = MakeBrick(brick.Coord);
			Slots[slot].bRequested = true;
			bChanged = true;
		}

		if (bChanged)
		{
			UpdateNeighbours();
		}
		return refused;
	}

	template<typename T>
	size_t SparseField<T>::FreeSettled(float tolerance)
	{
		std::vector<int32_t> settled;
		for (const int32_t slot : Order)
		{
			if (!Slots[slot].bRequested && GetDeviation(Slots[slot]) <= tolerance)
			{
				settled.push_back(slot);
			}
		}
		for (const int32_t slot : settled)
		{
			FreeBrick(slot);
		}
		if (!settled.empty())
		{
			UpdateNeighbours();
		}
		return settled.size();
	}

	template<typename T>
	void SparseField<T>::Clear()
	{
		Slots.clear();
		FreeSlots.clear();
		Order.clear();
		Current = 0;
	}

	template<typename T>
	std::vector<BrickCoord> SparseField<T>::GetBricks() const
	{
		std::vector<BrickCoord> bricks;
		bricks.reserve(Order.size());
		for (const int32_t slot : Order)
		{
			bricks.push_back(Slots[slot].Coord);
		}
		return bricks;
	}

	template<typename T>
	BrickCoord SparseField<T>::GetBrickAt(glm::vec3 position) const
	{
		return GetBrickOfCell(GetCellOf(position, Settings.CellSize));
	}

	template<typename T>
	void SparseField<T>::ResetToAmbient()
	{
		for (const int32_t slot : Order)
		{
			std::fill(Slots[slot].Values[Current].begin(), Slots[slot].Values[Current].end(), Ambient);
		}
	}

	template<typename T>
	float SparseField<T>::AddSource(glm::vec3 boxMin, glm::vec3 boxMax, T amount, const std::function<float(glm::vec3)>& weight)
	{
		glm::ivec3 first;
		glm::ivec3 last;
		GetBoxCells(boxMin, boxMax, Settings.CellSize, first, last);

		// The weights of every cell in the box, found or not, in the bricks' order of cells
		const auto getWeight = [&](glm::ivec3 cell)
		{
			return weight ? std::max(0.0f, weight((glm::vec3(cell) + 0.5f) * Settings.CellSize)) : 1.0f;
		};
		float totalWeight = 0.0f;
		if (!weight)
		{
			const glm::vec3 counts = glm::vec3(last - first + 1);
			totalWeight = counts.x * counts.y * counts.z;
		}
		for (int32_t z = first.z; weight && z <= last.z; ++z)
		{
			for (int32_t y = first.y; y <= last.y; ++y)
			{
				for (int32_t x = first.x; x <= last.x; ++x)
				{
					totalWeight += getWeight(glm::ivec3(x, y, z));
				}
			}
		}
		if (!(totalWeight > 0.0f))
		{
			return 0.0f;
		}

		float foundWeight = 0.0f;
		const BrickCoord firstBrick = GetBrickOfCell(first);
		const BrickCoord lastBrick = GetBrickOfCell(last);
		for (const int32_t slot : Order)
		{
			Brick& brick = Slots[slot];
			const BrickCoord coord = brick.Coord;
			if (coord.X < firstBrick.X || coord.X > lastBrick.X || coord.Y < firstBrick.Y || coord.Y > lastBrick.Y || coord.Z < firstBrick.Z ||
				coord.Z > lastBrick.Z)
			{
				continue;
			}
			const glm::ivec3 origin(coord.X * Size, coord.Y * Size, coord.Z * Size);
			const glm::ivec3 low = glm::max(first, origin);
			const glm::ivec3 high = glm::min(last, origin + (Size - 1));
			std::vector<T>& values = brick.Values[Current];
			for (int32_t z = low.z; z <= high.z; ++z)
			{
				for (int32_t y = low.y; y <= high.y; ++y)
				{
					for (int32_t x = low.x; x <= high.x; ++x)
					{
						const float cellWeight = getWeight(glm::ivec3(x, y, z));
						foundWeight += cellWeight;
						values[GetCellIndex(x - origin.x, y - origin.y, z - origin.z)] += amount * (cellWeight / totalWeight);
					}
				}
			}
		}
		return std::min(1.0f, foundWeight / totalWeight);
	}

	template<typename T>
	void SparseField<T>::ForEachCell(const std::function<void(glm::vec3 centre, T& value)>& visit)
	{
		for (const int32_t slot : Order)
		{
			Brick& brick = Slots[slot];
			const glm::vec3 origin(brick.Coord.X * Size, brick.Coord.Y * Size, brick.Coord.Z * Size);
			std::vector<T>& values = brick.Values[Current];
			for (int32_t z = 0; z < Size; ++z)
			{
				for (int32_t y = 0; y < Size; ++y)
				{
					for (int32_t x = 0; x < Size; ++x)
					{
						visit((origin + glm::vec3(x, y, z) + 0.5f) * Settings.CellSize, values[GetCellIndex(x, y, z)]);
					}
				}
			}
		}
	}

	template<typename T>
	void SparseField<T>::ForEachCell(const std::function<void(glm::vec3 centre, const T& value)>& visit) const
	{
		for (const int32_t slot : Order)
		{
			const Brick& brick = Slots[slot];
			const glm::vec3 origin(brick.Coord.X * Size, brick.Coord.Y * Size, brick.Coord.Z * Size);
			const std::vector<T>& values = brick.Values[Current];
			for (int32_t z = 0; z < Size; ++z)
			{
				for (int32_t y = 0; y < Size; ++y)
				{
					for (int32_t x = 0; x < Size; ++x)
					{
						visit((origin + glm::vec3(x, y, z) + 0.5f) * Settings.CellSize, values[GetCellIndex(x, y, z)]);
					}
				}
			}
		}
	}

	template<typename T>
	void SparseField<T>::Diffuse(float diffusivity, float deltaSeconds)
	{
		if (!(diffusivity > 0.0f) || !(deltaSeconds > 0.0f) || Order.empty())
		{
			return;
		}

		// Explicit steps are stable up to a sixth of a cell's square per diffusivity
		const float perStep = diffusivity * deltaSeconds / (Settings.CellSize * Settings.CellSize);
		const int parts = std::max(1, static_cast<int>(std::ceil(perStep * 6.0f)));
		const float factor = perStep / static_cast<float>(parts);

		for (int part = 0; part < parts; ++part)
		{
			for (const int32_t slot : Order)
			{
				Brick& brick = Slots[slot];
				FillPadded(brick, Padded);
				const T* const padded = Padded.data();
				std::vector<T>& next = brick.Values[1 - Current];
				for (int32_t z = 0; z < Size; ++z)
				{
					for (int32_t y = 0; y < Size; ++y)
					{
						for (int32_t x = 0; x < Size; ++x)
						{
							const size_t at = GetPaddedIndex(x, y, z);
							const T value = padded[at];
							// Opposite neighbours summed first, so a mirrored field stays mirrored
							const T sum = (padded[at - 1] + padded[at + 1]) + (padded[at - PaddedSide] + padded[at + PaddedSide]) +
								(padded[at - PaddedSide * PaddedSide] + padded[at + PaddedSide * PaddedSide]);
							next[GetCellIndex(x, y, z)] = value + factor * (sum - 6.0f * value);
						}
					}
				}
			}
			SwapBuffers();
		}
	}

	template<typename T>
	void SparseField<T>::Decay(float rate, float deltaSeconds)
	{
		if (!(rate > 0.0f) || !(deltaSeconds > 0.0f))
		{
			return;
		}
		const float factor = 1.0f / (1.0f + rate * deltaSeconds);
		for (const int32_t slot : Order)
		{
			for (T& value : Slots[slot].Values[Current])
			{
				value = Ambient + (value - Ambient) * factor;
			}
		}
	}

	template<typename T>
	void SparseField<T>::Advect(const SparseField<glm::vec3>& velocity, float deltaSeconds)
	{
		if (Order.empty())
		{
			return;
		}

		// A velocity on the same cells is read at each cell directly; any other is sampled
		const bool bSameCells = velocity.GetSettings().CellSize == Settings.CellSize;
		const float cellsPerMetre = 1.0f / Settings.CellSize;
		for (const int32_t slot : Order)
		{
			Brick& brick = Slots[slot];
			std::vector<T>& next = brick.Values[1 - Current];
			const glm::ivec3 origin(brick.Coord.X * Size, brick.Coord.Y * Size, brick.Coord.Z * Size);

			FillPadded(brick, Padded);
			const T* const padded = Padded.data();

			const int32_t velocitySlot = bSameCells ? velocity.FindSlot(brick.Coord) : -1;
			const std::vector<glm::vec3>* velocities = velocitySlot >= 0 ? &velocity.Slots[velocitySlot].Values[velocity.Current] : nullptr;

			for (int32_t z = 0; z < Size; ++z)
			{
				for (int32_t y = 0; y < Size; ++y)
				{
					for (int32_t x = 0; x < Size; ++x)
					{
						const size_t index = GetCellIndex(x, y, z);
						// In cell units, a cell's centre at its coordinates plus a half
						const glm::vec3 centre = glm::vec3(origin + glm::ivec3(x, y, z)) + 0.5f;
						glm::vec3 flow;
						if (bSameCells)
						{
							flow = velocities ? (*velocities)[index] : velocity.GetAmbient();
						}
						else
						{
							flow = velocity.Sample(centre * Settings.CellSize);
						}
						// Where the centre was: within a cell of the brick, from the padded values
						const glm::vec3 from = centre - flow * (deltaSeconds * cellsPerMetre);
						const glm::vec3 shifted = from - 0.5f;
						const glm::ivec3 base = FloorToCell(shifted);
						const glm::ivec3 local = base - origin;
						if (local.x >= -1 && local.x < Size && local.y >= -1 && local.y < Size && local.z >= -1 && local.z < Size)
						{
							const size_t at = GetPaddedIndex(local.x, local.y, local.z);
							const T corners[8] = { padded[at], padded[at + 1], padded[at + PaddedSide], padded[at + PaddedSide + 1],
								padded[at + PaddedSide * PaddedSide], padded[at + PaddedSide * PaddedSide + 1], padded[at + PaddedSide * PaddedSide + PaddedSide],
								padded[at + PaddedSide * PaddedSide + PaddedSide + 1] };
							next[index] = Trilinear(corners, shifted - glm::vec3(base));
						}
						else
						{
							next[index] = SampleNear(brick, from);
						}
					}
				}
			}
		}

		// The velocity read what it held before this step, also when it is this field
		SwapBuffers();
	}

	template<typename T>
	T SparseField<T>::Sample(glm::vec3 position) const
	{
		const glm::vec3 cellPosition = position / Settings.CellSize;
		const int32_t slot = FindSlot(GetBrickOfCell(FloorToCell(cellPosition)));
		if (slot >= 0)
		{
			return SampleNear(Slots[slot], cellPosition);
		}

		// Not in a brick: one round it may still be near enough for some of the eight cells
		const glm::vec3 low(FloorToCell(cellPosition - 0.5f));
		T result = Ambient;
		bool bNear = false;
		for (int corner = 0; corner < 8 && !bNear; ++corner)
		{
			const glm::ivec3 cell = glm::ivec3(low) + glm::ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1);
			const int32_t near = FindSlot(GetBrickOfCell(cell));
			if (near >= 0)
			{
				result = SampleNear(Slots[near], cellPosition);
				bNear = true;
			}
		}
		return result;
	}

	template<typename T>
	T SparseField<T>::GetCell(glm::ivec3 cell) const
	{
		const BrickCoord coord = GetBrickOfCell(cell);
		const int32_t slot = FindSlot(coord);
		if (slot < 0)
		{
			return Ambient;
		}
		return Slots[slot].Values[Current][GetCellIndex(cell.x & SizeMask, cell.y & SizeMask, cell.z & SizeMask)];
	}

	template<typename T>
	int32_t SparseField<T>::FindSlot(BrickCoord coord) const
	{
		const auto found = std::lower_bound(Order.begin(), Order.end(), coord, [this](int32_t slot, const BrickCoord& value)
		{
			return Slots[slot].Coord < value;
		});
		return found != Order.end() && Slots[*found].Coord == coord ? *found : -1;
	}

	template<typename T>
	int32_t SparseField<T>::MakeBrick(BrickCoord coord)
	{
		int32_t slot;
		if (!FreeSlots.empty())
		{
			slot = FreeSlots.back();
			FreeSlots.pop_back();
		}
		else
		{
			slot = static_cast<int32_t>(Slots.size());
			Slots.emplace_back();
		}

		Brick& brick = Slots[slot];
		brick.Coord = coord;
		brick.bRequested = false;
		brick.Values[0].assign(CellsPerBrick, Ambient);
		brick.Values[1].assign(CellsPerBrick, Ambient);

		const auto place = std::lower_bound(Order.begin(), Order.end(), coord, [this](int32_t other, const BrickCoord& value)
		{
			return Slots[other].Coord < value;
		});
		Order.insert(place, slot);
		return slot;
	}

	template<typename T>
	void SparseField<T>::FreeBrick(int32_t slot)
	{
		Order.erase(std::find(Order.begin(), Order.end(), slot));
		Slots[slot].Values[0].clear();
		Slots[slot].Values[1].clear();
		FreeSlots.push_back(slot);
	}

	template<typename T>
	void SparseField<T>::UpdateNeighbours()
	{
		for (const int32_t slot : Order)
		{
			Brick& brick = Slots[slot];
			for (int32_t dz = -1; dz <= 1; ++dz)
			{
				for (int32_t dy = -1; dy <= 1; ++dy)
				{
					for (int32_t dx = -1; dx <= 1; ++dx)
					{
						brick.Neighbours[GetNeighbourIndex(dx, dy, dz)] =
							FindSlot(BrickCoord{ brick.Coord.X + dx, brick.Coord.Y + dy, brick.Coord.Z + dz });
					}
				}
			}
		}
	}

	template<typename T>
	float SparseField<T>::GetDeviation(const Brick& brick) const
	{
		float deviation = 0.0f;
		for (const T& value : brick.Values[Current])
		{
			deviation = std::max(deviation, GetMagnitude(value - Ambient));
		}
		return deviation;
	}

	template<typename T>
	T SparseField<T>::GetCellNear(const Brick& brick, glm::ivec3 cell) const
	{
		const glm::ivec3 local = cell - glm::ivec3(brick.Coord.X * Size, brick.Coord.Y * Size, brick.Coord.Z * Size);
		if (local.x >= 0 && local.x < Size && local.y >= 0 && local.y < Size && local.z >= 0 && local.z < Size)
		{
			return brick.Values[Current][GetCellIndex(local.x, local.y, local.z)];
		}

		const glm::ivec3 offset(GetBrickOf(local.x), GetBrickOf(local.y), GetBrickOf(local.z));
		if (offset.x < -1 || offset.x > 1 || offset.y < -1 || offset.y > 1 || offset.z < -1 || offset.z > 1)
		{
			return GetCell(cell);
		}
		const int32_t other = brick.Neighbours[GetNeighbourIndex(offset.x, offset.y, offset.z)];
		if (other < 0)
		{
			return Ambient;
		}
		return Slots[other].Values[Current][GetCellIndex(local.x & SizeMask, local.y & SizeMask, local.z & SizeMask)];
	}

	template<typename T>
	T SparseField<T>::SampleNear(const Brick& brick, glm::vec3 cellPosition) const
	{
		const glm::vec3 shifted = cellPosition - 0.5f;
		const glm::ivec3 base = FloorToCell(shifted);
		const glm::vec3 t = shifted - glm::vec3(base);

		T corners[8];
		const glm::ivec3 local = base - glm::ivec3(brick.Coord.X * Size, brick.Coord.Y * Size, brick.Coord.Z * Size);
		if (local.x >= 0 && local.x < Size - 1 && local.y >= 0 && local.y < Size - 1 && local.z >= 0 && local.z < Size - 1)
		{
			// All eight in this brick
			const std::vector<T>& values = brick.Values[Current];
			for (int corner = 0; corner < 8; ++corner)
			{
				corners[corner] = values[GetCellIndex(local.x + (corner & 1), local.y + ((corner >> 1) & 1), local.z + ((corner >> 2) & 1))];
			}
		}
		else
		{
			for (int corner = 0; corner < 8; ++corner)
			{
				corners[corner] = GetCellNear(brick, base + glm::ivec3(corner & 1, (corner >> 1) & 1, (corner >> 2) & 1));
			}
		}

		return Trilinear(corners, t);
	}

	template<typename T>
	void SparseField<T>::FillPadded(const Brick& brick, std::vector<T>& outPadded) const
	{
		outPadded.resize(static_cast<size_t>(PaddedSize * PaddedSize * PaddedSize));

		// 27 boxes: the brick, and a layer, row or corner of each neighbour, or of the ambient
		for (int32_t dz = -1; dz <= 1; ++dz)
		{
			for (int32_t dy = -1; dy <= 1; ++dy)
			{
				for (int32_t dx = -1; dx <= 1; ++dx)
				{
					const int32_t slot = (dx == 0 && dy == 0 && dz == 0) ? -2 : brick.Neighbours[GetNeighbourIndex(dx, dy, dz)];
					const std::vector<T>* source = slot == -2 ? &brick.Values[Current] : (slot >= 0 ? &Slots[slot].Values[Current] : nullptr);
					const int32_t fromX = dx < 0 ? -1 : (dx > 0 ? Size : 0);
					const int32_t toX = dx < 0 ? -1 : (dx > 0 ? Size : Size - 1);
					const int32_t fromY = dy < 0 ? -1 : (dy > 0 ? Size : 0);
					const int32_t toY = dy < 0 ? -1 : (dy > 0 ? Size : Size - 1);
					const int32_t fromZ = dz < 0 ? -1 : (dz > 0 ? Size : 0);
					const int32_t toZ = dz < 0 ? -1 : (dz > 0 ? Size : Size - 1);
					for (int32_t z = fromZ; z <= toZ; ++z)
					{
						for (int32_t y = fromY; y <= toY; ++y)
						{
							for (int32_t x = fromX; x <= toX; ++x)
							{
								outPadded[GetPaddedIndex(x, y, z)] =
									source ? (*source)[GetCellIndex(x & SizeMask, y & SizeMask, z & SizeMask)] : Ambient;
							}
						}
					}
				}
			}
		}
	}

	template class SparseField<float>;
	template class SparseField<glm::vec3>;
}
