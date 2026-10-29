// The free ranges of a block of the mesh memory pool (RangeAllocator): ranges taken at their
// alignment, never overlapping, from the first free one they fit in; a full block refuses more;
// ranges given back merge with their free neighbours, so a block emptied in any order is one free
// range again; a block filled, thinned out and filled again finds room in the gaps.
#include "MeshMemoryPool.h"
#include "TestSupport.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Nyx::RangeAllocator;
	using Nyx::Tests::Require;

	struct Range
	{
		uint64_t Offset = 0;
		uint64_t Size = 0;
	};

	void RequireApart(std::vector<Range> ranges)
	{
		std::sort(ranges.begin(), ranges.end(), [](const Range& a, const Range& b) { return a.Offset < b.Offset; });
		for (size_t i = 1; i < ranges.size(); ++i)
		{
			Require(ranges[i - 1].Offset + ranges[i - 1].Size <= ranges[i].Offset, "Two ranges overlap");
		}
	}

	// Ranges at their alignment, apart, the first fitting free one first; a full block refuses
	void TestAllocate()
	{
		RangeAllocator block(1024);
		const std::optional<uint64_t> first = block.Allocate(100, 1);
		const std::optional<uint64_t> second = block.Allocate(100, 256);
		const std::optional<uint64_t> third = block.Allocate(10, 16);
		Require(first && *first == 0, "The first range should start the block");
		Require(second && *second == 256, "A range should start at its alignment");
		Require(third && *third == 112, "A small range should fill the gap before an aligned one");
		Require(block.GetFreeBytes() == 1024 - 210, "The free bytes should be what was not taken");
		RequireApart({ { *first, 100 }, { *second, 100 }, { *third, 10 } });

		Require(!block.Allocate(1024, 1), "A range larger than what is free should be refused");
		Require(!block.Allocate(0, 1), "An empty range should be refused");
		const std::optional<uint64_t> rest = block.Allocate(1024 - 356, 1);
		Require(rest && *rest == 356, "The rest of the block should fit after the last range");
	}

	// Given back in any order, ranges merge with their free neighbours: an emptied block is one range
	void TestFreeMerges()
	{
		for (int order = 0; order < 6; ++order)
		{
			RangeAllocator block(300);
			std::vector<Range> ranges;
			for (int i = 0; i < 3; ++i)
			{
				ranges.push_back({ *block.Allocate(100, 1), 100 });
			}
			std::vector<int> sequence{ 0, 1, 2 };
			for (int step = 0; step < order; ++step)
			{
				std::next_permutation(sequence.begin(), sequence.end());
			}
			for (const int index : sequence)
			{
				block.Free(ranges[index].Offset, ranges[index].Size);
			}
			Require(block.IsEmpty() && block.GetFreeRangeCount() == 1, "An emptied block should be one free range, in order " + std::to_string(order));
			Require(block.Allocate(300, 1) == std::optional<uint64_t>(0), "An emptied block should hold one range of its whole size");
		}
	}

	// Filled with ranges of many sizes, every other one given back, filled again: the gaps take what
	// fits, nothing overlaps, and emptying it all leaves one free range
	void TestReuseAfterFragmentation()
	{
		RangeAllocator block(1u << 20);
		std::vector<Range> ranges;
		uint32_t state = 7;
		auto nextSize = [&]
		{
			state = state * 1664525u + 1013904223u;
			return uint64_t(64 + (state >> 20) % 4000);
		};
		while (true)
		{
			const uint64_t size = nextSize();
			const std::optional<uint64_t> offset = block.Allocate(size, 16);
			if (!offset)
			{
				break;
			}
			ranges.push_back({ *offset, size });
		}
		Require(ranges.size() > 200, "The block should hold many ranges");
		RequireApart(ranges);

		std::vector<Range> kept;
		for (size_t i = 0; i < ranges.size(); ++i)
		{
			if (i % 2 == 0)
			{
				block.Free(ranges[i].Offset, ranges[i].Size);
			}
			else
			{
				kept.push_back(ranges[i]);
			}
		}
		size_t refilled = 0;
		for (int attempt = 0; attempt < 4000; ++attempt)
		{
			const uint64_t size = nextSize() / 2;
			if (const std::optional<uint64_t> offset = block.Allocate(size, 16))
			{
				kept.push_back({ *offset, size });
				++refilled;
			}
		}
		Require(refilled > ranges.size() / 2, "The gaps should take new ranges");
		RequireApart(kept);

		for (const Range& range : kept)
		{
			block.Free(range.Offset, range.Size);
		}
		Require(block.IsEmpty() && block.GetFreeRangeCount() == 1, "A block emptied after all that should be one free range");
	}
}

int main()
{
	try
	{
		TestAllocate();
		TestFreeMerges();
		TestReuseAfterFragmentation();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Mesh memory pool test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All mesh memory pool tests passed.\n";
	return 0;
}
