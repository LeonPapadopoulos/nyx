// The sparse field (SparseField): a source in still air spreads into a symmetric bump that decays
// to nothing and its bricks are freed; a wind carries it downwind; diffusion stays stable at long
// steps; the cap on bricks holds and drops the least important; the same values to the bit over 600
// steps, whatever order the bricks came in; the ambient outside every brick; and what a step costs
// at the cap.
#include "SparseField.h"
#include "TestSupport.h"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Nyx::BrickCoord;
	using Nyx::SparseField;
	using Nyx::SparseFieldSettings;
	using Nyx::Tests::Require;

	using ScalarField = SparseField<float>;
	using VectorField = SparseField<glm::vec3>;

	constexpr float Step = 1.0f / 15.0f;

	bool Near(float a, float b, float relative)
	{
		return std::abs(a - b) <= relative * std::max(std::abs(a), std::abs(b)) + 1e-12f;
	}

	// The sum of the values above the ambient, and where their centre is
	struct Mass
	{
		float Total = 0.0f;
		glm::vec3 Centre{ 0.0f };
	};

	Mass GetMass(const ScalarField& field)
	{
		Mass mass;
		glm::vec3 weighted(0.0f);
		field.ForEachCell([&](glm::vec3 centre, const float& value)
		{
			mass.Total += value - field.GetAmbient();
			weighted += (value - field.GetAmbient()) * centre;
		});
		mass.Centre = mass.Total != 0.0f ? weighted / mass.Total : glm::vec3(0.0f);
		return mass;
	}

	float GetLargest(const ScalarField& field)
	{
		float largest = -1e30f;
		field.ForEachCell([&](glm::vec3, const float& value)
		{
			largest = std::max(largest, value);
		});
		return largest;
	}

	float GetSmallest(const ScalarField& field)
	{
		float smallest = 1e30f;
		field.ForEachCell([&](glm::vec3, const float& value)
		{
			smallest = std::min(smallest, value);
		});
		return smallest;
	}

	// A source in still air: a bump, the same mirrored in every axis, highest in the middle, that
	// decays back to the ambient; its bricks are then freed, and the ambient is all there is
	void TestBumpSpreadsAndDecays()
	{
		ScalarField field(SparseFieldSettings{}, 20.0f);
		Require(field.Allocate(glm::vec3(-4.0f), glm::vec3(3.9f)) == 0, "Eight bricks should be made under the cap");
		Require(field.GetBrickCount() == 8, "A box of 8 m from -4 should take 2 x 2 x 2 bricks, took " + std::to_string(field.GetBrickCount()));

		// The eight cells round the origin, 100 in all
		Require(field.AddSource(glm::vec3(-0.5f), glm::vec3(0.5f), 100.0f) == 1.0f, "Every cell of the source should have a brick");
		Require(field.GetCell({ 0, 0, 0 }) == 32.5f && field.GetCell({ -1, -1, -1 }) == 32.5f, "Each of the eight cells should get an eighth");
		Require(Near(GetMass(field).Total, 100.0f, 1e-5f), "The source should add 100 above the ambient");

		for (int step = 0; step < 30; ++step)
		{
			field.Diffuse(0.05f, Step);
		}
		const float middle = field.GetCell({ 0, 0, 0 });
		Require(middle > 20.0f && middle < 32.5f, "The middle should have spread, is " + std::to_string(middle));
		for (int x = 0; x < 8; ++x)
		{
			for (const glm::ivec3 cell : { glm::ivec3(x, 0, 0), glm::ivec3(x, 2, 1), glm::ivec3(3, x, 5) })
			{
				const float value = field.GetCell(cell);
				for (const glm::ivec3 mirrored : { glm::ivec3(-1 - cell.x, cell.y, cell.z), glm::ivec3(cell.x, -1 - cell.y, cell.z),
						 glm::ivec3(cell.x, cell.y, -1 - cell.z), glm::ivec3(cell.z, cell.x, cell.y), -1 - cell })
				{
					Require(Near(field.GetCell(mirrored), value, 1e-5f), "The bump should be the same mirrored and turned");
				}
			}
			if (x > 0)
			{
				Require(field.GetCell({ x, 0, 0 }) < field.GetCell({ x - 1, 0, 0 }), "The bump should fall away from the middle");
			}
		}
		Require(Near(GetMass(field).Total, 100.0f, 0.01f), "Spreading should keep what was added while it stays in the bricks");
		Require(Near(field.Sample(glm::vec3(0.0f)), field.GetCell({ 0, 0, 0 }), 1e-6f), "The middle, sampled, should be its eight cells' value");

		for (int step = 0; step < 300; ++step)
		{
			field.Diffuse(0.05f, Step);
			field.Decay(0.5f, Step);
		}
		Require(std::abs(GetLargest(field) - 20.0f) < 1e-3f, "The bump should have decayed to the ambient");
		Require(field.FreeSettled(1e-3f) == 0, "Requested bricks should not be freed");
		field.ClearRequests();
		Require(field.FreeSettled(1e-3f) == 8, "Settled bricks no longer requested should be freed");
		Require(field.GetBrickCount() == 0 && field.Sample(glm::vec3(0.0f)) == 20.0f, "Without bricks the ambient is all there is");
	}

	// A step far longer than the explicit limit is taken in parts: nothing overshoots or grows
	void TestDiffusionStable()
	{
		ScalarField field(SparseFieldSettings{}, 0.0f);
		field.Allocate(glm::vec3(-4.0f), glm::vec3(3.9f));
		field.AddSource(glm::vec3(0.1f), glm::vec3(0.1f), 1.0f);
		Require(field.GetCell({ 0, 0, 0 }) == 1.0f, "A source smaller than a cell should go into the cell its centre is in");
		for (int step = 0; step < 20; ++step)
		{
			// 2 m^2/s over a 0.5 m cell for a fifteenth of a second: 2.1 a step, 13 times the limit
			field.Diffuse(2.0f, Step);
			Require(GetSmallest(field) >= 0.0f && GetLargest(field) <= 1.0f, "Diffusion should never overshoot");
		}
		Require(GetLargest(field) < 0.01f, "Strong diffusion should have spread the cell out");
	}

	// A wind carries the bump downwind: as a field without bricks (the ambient everywhere) and as one
	// on the same cells
	void TestAdvectionMovesDownwind()
	{
		for (const bool bWindBricks : { false, true })
		{
			const glm::vec3 wind(2.0f, 0.0f, 0.0f);
			VectorField air(SparseFieldSettings{}, bWindBricks ? glm::vec3(0.0f) : wind);
			ScalarField field(SparseFieldSettings{}, 0.0f);
			const glm::vec3 low(-4.0f, -4.0f, -4.0f);
			const glm::vec3 high(15.9f, 3.9f, 3.9f);
			field.Allocate(low, high);
			if (bWindBricks)
			{
				air.Allocate(low, high);
				air.ForEachCell([&](glm::vec3, glm::vec3& value)
				{
					value = wind;
				});
			}
			field.AddSource(glm::vec3(-0.5f), glm::vec3(0.5f), 100.0f);

			// Two seconds of wind: 4 m downwind
			for (int step = 0; step < 30; ++step)
			{
				field.Advect(air, Step);
			}
			const Mass mass = GetMass(field);
			const std::string what = bWindBricks ? "In wind on bricks" : "In the ambient wind";
			Require(std::abs(mass.Centre.x - 4.0f) < 0.1f, what + ", the bump should move 4 m downwind, moved " + std::to_string(mass.Centre.x));
			Require(std::abs(mass.Centre.y) < 1e-3f && std::abs(mass.Centre.z) < 1e-3f, what + ", the bump should not move across the wind");
			Require(Near(mass.Total, 100.0f, 1e-3f), what + ", the bump should keep its mass, has " + std::to_string(mass.Total));
			Require(field.Sample({ 0.0f, 0.0f, 0.0f }) < 0.1f * field.Sample({ 4.0f, 0.0f, 0.0f }), what + ", the bump should have left where it was");
		}

		// The wind carrying itself, from a still edge: what stays still stays, what blows moves on
		VectorField air(SparseFieldSettings{}, glm::vec3(0.0f));
		air.Allocate(glm::vec3(-4.0f), glm::vec3(3.9f));
		air.AddSource(glm::vec3(-2.0f), glm::vec3(2.0f), glm::vec3(512.0f, 0.0f, 0.0f));
		air.Advect(air, Step);
		Require(air.Sample(glm::vec3(-3.0f)) == glm::vec3(0.0f), "Still air should stay still");
		Require(air.GetCell({ 0, 0, 0 }).x > 0.0f, "Blowing air should carry itself on");
	}

	// The cap holds; at it, a new brick takes the place of the least important not requested, and
	// with all requested it is refused
	void TestCapHolds()
	{
		ScalarField field(SparseFieldSettings{ .MaxBricks = 8 }, 0.0f);
		// 3 x 3 x 3 bricks of 4 m
		const glm::vec3 boxMin(-4.0f);
		const glm::vec3 boxMax(7.9f);
		const size_t refused = field.Allocate(boxMin, boxMax);
		Require(refused == 19 && field.GetBrickCount() == 8, "Of 27 bricks 8 should be made and 19 refused, " + std::to_string(refused) + " were");
		Require(field.HasBrick({ 0, 0, 0 }) && field.HasBrick({ 1, 0, 0 }) && field.HasBrick({ 0, 0, -1 }) && !field.HasBrick({ 1, 1, 1 }),
			"The bricks nearest the box's centre should come first");

		// A hot brick, and seven nearly at the ambient
		field.ClearRequests();
		const BrickCoord hot = field.GetBricks().back();
		const glm::vec3 hotCentre = (glm::vec3(hot.X, hot.Y, hot.Z) + 0.5f) * 4.0f;
		field.AddSource(hotCentre, hotCentre, 50.0f);
		field.AddSource(boxMin, boxMax, 1.0f);

		// Four new bricks far away
		Require(field.Allocate(glm::vec3(40.0f), glm::vec3(47.9f, 47.9f, 43.9f)) == 0, "New bricks should replace bricks no longer requested");
		Require(field.GetBrickCount() == 8, "The cap should hold");
		Require(field.HasBrick(hot), "The hot brick should be kept over those nearer the ambient");
		Require(field.HasBrick({ 10, 10, 10 }) && field.HasBrick({ 11, 11, 10 }), "The new bricks should be there");

		// All eight requested this round: another is refused, first come first served
		Require(field.Allocate(boxMin, boxMax) == 23, "The rest of the first box should be refused");
		Require(field.Allocate(glm::vec3(100.0f), glm::vec3(100.0f)) == 1, "With every brick requested a new one should be refused");
		Require(field.GetBrickCount() == 8 && field.HasBrick(hot), "The cap should hold, and the requested bricks stay");
	}

	// Bricks made and freed in any order are linked with the bricks round them as if made at once:
	// spreading over them gives the same bits
	void TestBricksComingAndGoing()
	{
		const glm::vec3 near(0.0f);
		const glm::vec3 nearEnd(7.9f);
		const glm::vec3 beside(8.0f, 0.0f, 0.0f);
		const glm::vec3 besideEnd(11.9f, 7.9f, 7.9f);

		ScalarField changed(SparseFieldSettings{}, 0.0f);
		changed.Allocate(beside, besideEnd);
		changed.Allocate(glm::vec3(40.0f), glm::vec3(47.9f));
		changed.ClearRequests();
		changed.Allocate(near, nearEnd);
		changed.FreeSettled(1e30f);
		changed.Allocate(beside, besideEnd);
		changed.Allocate(glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(-0.1f, 3.9f, 3.9f));

		ScalarField made(SparseFieldSettings{}, 0.0f);
		made.Allocate(glm::vec3(-4.0f, 0.0f, 0.0f), glm::vec3(-0.1f, 3.9f, 3.9f));
		made.Allocate(near, besideEnd);
		Require(changed.GetBricks() == made.GetBricks(), "The same bricks should be there");

		for (ScalarField* field : { &changed, &made })
		{
			field->AddSource(glm::vec3(3.9f), glm::vec3(4.1f), 100.0f);
			field->AddSource(glm::vec3(8.1f, 1.0f, 1.0f), glm::vec3(8.3f, 1.2f, 1.2f), 50.0f);
			for (int i = 0; i < 20; ++i)
			{
				field->Diffuse(0.5f, Step);
			}
		}
		std::vector<float> a;
		std::vector<float> b;
		std::as_const(changed).ForEachCell([&](glm::vec3, const float& value) { a.push_back(value); });
		std::as_const(made).ForEachCell([&](glm::vec3, const float& value) { b.push_back(value); });
		Require(a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0,
			"Bricks made and freed one by one should spread as bricks made at once");
	}

	// The ambient wherever no brick is near; the cells' values, and between them, where one is
	void TestSamplingOutsideBricks()
	{
		ScalarField field(SparseFieldSettings{ .CellSize = 0.25f }, -3.0f);
		Require(field.Sample(glm::vec3(1.0f, 2.0f, 3.0f)) == -3.0f, "An empty field should be the ambient everywhere");
		field.Allocate(glm::vec3(0.0f), glm::vec3(1.9f));
		Require(field.GetBrickCount() == 1, "Two metres of quarter metre cells should be one brick");
		field.ForEachCell([](glm::vec3 centre, float& value)
		{
			value = centre.x;
		});
		Require(field.Sample({ 100.0f, 0.0f, -50.0f }) == -3.0f && field.Sample({ -1.0f, 1.0f, 1.0f }) == -3.0f, "Far from the brick should be the ambient");
		Require(field.GetCell({ 8, 0, 0 }) == -3.0f && field.GetCell({ -1, 3, 3 }) == -3.0f, "A cell in no brick should be the ambient");
		Require(field.Sample({ 0.625f, 1.0f, 1.0f }) == 0.625f && field.GetCell({ 2, 0, 7 }) == 0.625f, "A cell's centre should give its value");
		Require(Near(field.Sample({ 0.75f, 0.9f, 1.3f }), 0.75f, 1e-6f), "Between cells it should be trilinear");

		// Half way between the last cell's centre and the next, in no brick: half the ambient
		const float edge = field.Sample({ 2.0f, 1.0f, 1.0f });
		Require(Near(edge, 0.5f * (1.875f - 3.0f), 1e-6f), "At the brick's side, half the ambient should come in, got " + std::to_string(edge));
		Require(field.Sample({ 2.125f, 1.0f, 1.0f }) == -3.0f, "A cell's centre beyond the brick should be the ambient");
		Require(Near(field.Sample({ -0.0625f, 1.0f, 1.0f }), 0.25f * 0.125f + 0.75f * -3.0f, 1e-6f), "Just outside the brick it should blend into the ambient");
	}

	// A field's whole state, to the bit
	std::vector<uint32_t> GetBits(const ScalarField& field)
	{
		std::vector<uint32_t> bits;
		for (const BrickCoord& brick : field.GetBricks())
		{
			bits.push_back(static_cast<uint32_t>(brick.X));
			bits.push_back(static_cast<uint32_t>(brick.Y));
			bits.push_back(static_cast<uint32_t>(brick.Z));
		}
		field.ForEachCell([&](glm::vec3, const float& value)
		{
			uint32_t valueBits = 0;
			std::memcpy(&valueBits, &value, sizeof(valueBits));
			bits.push_back(valueBits);
		});
		return bits;
	}

	// 600 steps of sources moving through a changing wind, bricks made, dropped at the cap and
	// freed as they go
	struct Run
	{
		std::vector<uint32_t> Bits;
		size_t Refused = 0;
	};

	Run RunSteps(bool bReversed, uint32_t maxBricks, int steps)
	{
		ScalarField field(SparseFieldSettings{ .MaxBricks = maxBricks }, 15.0f);
		VectorField air(SparseFieldSettings{ .MaxBricks = maxBricks }, glm::vec3(0.0f));
		Run run;
		for (int step = 0; step < steps; ++step)
		{
			const float time = static_cast<float>(step) * Step;
			const glm::vec3 first(0.02f * static_cast<float>(step), 1.0f, 0.0f);
			const glm::vec3 second(-6.0f, 0.0f, 0.01f * static_cast<float>(step));
			field.ClearRequests();
			air.ClearRequests();
			const glm::vec3 boxes[2][2] = { { first - 2.0f, first + glm::vec3(2.0f, 6.0f, 2.0f) }, { second - 3.0f, second + 3.0f } };
			for (int box = 0; box < 2; ++box)
			{
				const int index = bReversed ? 1 - box : box;
				run.Refused += field.Allocate(boxes[index][0], boxes[index][1]);
				air.Allocate(boxes[index][0], boxes[index][1]);
			}

			// A wind that turns, rising where it is warm
			air.ResetToAmbient();
			air.ForEachCell([&](glm::vec3 centre, glm::vec3& value)
			{
				value = glm::vec3(0.5f + 0.01f * time, 0.0f, 0.2f) + glm::vec3(0.0f, 0.05f * (field.Sample(centre) - 15.0f), 0.0f);
			});
			field.AddSource(first - 0.5f, first + 0.5f, 40.0f * Step, [&](glm::vec3 centre)
			{
				return 1.0f - glm::length(centre - first);
			});
			field.AddSource(second - 1.0f, second + 1.0f, 25.0f * Step);
			field.Diffuse(0.08f, Step);
			field.Advect(air, Step);
			field.Decay(0.3f, Step);
			if (step % 30 == 0)
			{
				field.FreeSettled(1e-4f);
				air.FreeSettled(1e-4f);
			}
		}
		Require(field.GetBrickCount() > 8 && field.GetBrickCount() <= maxBricks, "The run should have used bricks up to the cap");
		run.Bits = GetBits(field);
		return run;
	}

	void TestDeterministic()
	{
		// At the cap, where bricks go to the boxes in the order they are asked for
		const Run capped = RunSteps(false, 12, 600);
		Require(capped.Refused > 0, "The cap should have refused bricks");
		Require(capped.Bits == RunSteps(false, 12, 600).Bits, "600 steps at the cap should give the same values to the bit");

		// Under it, the order the bricks came in makes no difference
		const Run uncapped = RunSteps(false, 256, 120);
		Require(uncapped.Refused == 0, "Under the cap nothing should be refused");
		Require(uncapped.Bits == RunSteps(true, 256, 120).Bits, "Boxes asked for in another order should give the same bricks and values to the bit");
	}

	// What a step costs at the cap: 64 bricks, sources, diffusion, decay and advection by a wind on
	// the same bricks, and a wind made again from scratch
	void TestCostAtCap()
	{
		ScalarField field(SparseFieldSettings{ .MaxBricks = 64 }, 0.0f);
		VectorField air(SparseFieldSettings{ .MaxBricks = 64 }, glm::vec3(0.0f));
		field.Allocate(glm::vec3(-8.0f), glm::vec3(7.9f));
		air.Allocate(glm::vec3(-8.0f), glm::vec3(7.9f));
		Require(field.GetBrickCount() == 64 && air.GetBrickCount() == 64, "Both fields should be at the cap");

#ifdef NDEBUG
		constexpr int Steps = 200;
#else
		constexpr int Steps = 10;
#endif
		double sources = 0.0;
		double wind = 0.0;
		double diffuse = 0.0;
		double advect = 0.0;
		double decay = 0.0;
		const auto time = [](double& total, auto&& run)
		{
			const auto start = std::chrono::steady_clock::now();
			run();
			total += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		};
		for (int step = 0; step < Steps; ++step)
		{
			time(sources, [&]
			{
				field.AddSource(glm::vec3(-2.0f), glm::vec3(2.0f), 10.0f);
			});
			time(wind, [&]
			{
				air.ResetToAmbient();
				air.ForEachCell([&](glm::vec3 centre, glm::vec3& value)
				{
					value = glm::vec3(1.0f, 0.1f * centre.x, 0.0f);
				});
			});
			time(diffuse, [&]
			{
				field.Diffuse(0.05f, Step);
			});
			time(advect, [&]
			{
				field.Advect(air, Step);
			});
			time(decay, [&]
			{
				field.Decay(0.2f, Step);
			});
		}
		const double total = sources + wind + diffuse + advect + decay;
		std::cout << "A step at the cap (64 bricks, 32768 cells): " << total / Steps << " ms: sources " << sources / Steps << ", wind made "
				  << wind / Steps << ", diffuse " << diffuse / Steps << ", advect " << advect / Steps << ", decay " << decay / Steps << "\n";
	}
}

int main()
{
	try
	{
		TestBumpSpreadsAndDecays();
		TestDiffusionStable();
		TestAdvectionMovesDownwind();
		TestCapHolds();
		TestBricksComingAndGoing();
		TestSamplingOutsideBricks();
		TestDeterministic();
		TestCostAtCap();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Sparse field test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All sparse field tests passed.\n";
	return 0;
}
