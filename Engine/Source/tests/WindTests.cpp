// The world's wind without a GPU (Docs/Design/wind.md): the contract (WindState, SampleWind()) with
// hand-made states, its height profile and gust fronts crossing the land along it; the wind turned
// to new values smoothly and the same whatever the steps; a driver ramping it from calm to a storm
// on a game world, whose systems read it; a game's local air on a grid; the debug view; an old
// scene's environment wind moved into a wind of its own. Clouds, high cloud layer and volumetric,
// carried by a hand-made wind at their altitude, or by their own with Own Wind.
#include "CloudLayer.h"
#include "CloudLayerComponent.h"
#include "ComponentRegistration.h"
#include "DebugDraw.h"
#include "EnvironmentComponent.h"
#include "GameWorld.h"
#include "LocalWind.h"
#include "Log.h"
#include "TestSupport.h"
#include "VolumetricClouds.h"
#include "VolumetricCloudsComponent.h"
#include "Wind.h"
#include "WindComponent.h"
#include "WindDebug.h"
#include "WindField.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	bool SameBits(const WindState& a, const WindState& b)
	{
		return std::memcmp(&a, &b, sizeof(WindState)) == 0;
	}

	// A steady wind the same at every height, as a hand-made state
	WindState Steady(const glm::vec2& direction, float speed)
	{
		WindState wind;
		wind.Direction = glm::normalize(direction);
		wind.Speed = speed;
		return wind;
	}

	// Calm gives nothing anywhere; a steady wind blows its speed along its direction, everywhere
	void TestTheContract()
	{
		const WindState calm;
		Require(SampleWind(calm, glm::vec3(3.0f, 2.0f, -9.0f), 5.0f) == glm::vec3(0.0f) && SampleWindGust(calm, glm::vec3(0.0f), 0.0f) == 0.0f,
			"A calm wind blows");
		const WindState steady = Steady(glm::vec2(0.6f, -0.8f), 5.0f);
		for (const glm::vec3& point : { glm::vec3(0.0f), glm::vec3(100.0f, 30.0f, -2000.0f), glm::vec3(-7.5f, 1.0f, 3.25f) })
		{
			const glm::vec3 blows = SampleWind(steady, point, 123.0f);
			Require(Near(blows, glm::vec3(3.0f, 0.0f, -4.0f)), "A steady wind doesn't blow its speed along its way at " + Describe(point) + ": " + Describe(blows));
		}
		// The same question, the same answer, to the bit
		WindComponent gusty;
		const WindState wind = MakeSteadyWind(gusty, 77.0);
		const glm::vec3 point(12.3f, 0.7f, -45.6f);
		const glm::vec3 once = SampleWind(wind, point, 77.25f);
		const glm::vec3 again = SampleWind(wind, point, 77.25f);
		Require(std::memcmp(&once, &again, sizeof(glm::vec3)) == 0, "The same wind at the same point and time blows differently");
		Require(SameBits(MakeSteadyWind(gusty, 77.0), wind), "The same values at the same time make another wind");
	}

	// A box of cells at once, as SampleWind() gives each cell, to the bit, added to what is there
	void TestABoxOfCells()
	{
		WindComponent gusty;
		gusty.Speed = 7.0f;
		const WindState wind = MakeSteadyWind(gusty, 77.0);
		const glm::ivec3 first(-3, -1, 5);
		const glm::ivec3 cells(5, 4, 3);
		const float cellSize = 0.5f;
		std::vector<glm::vec3> velocities(static_cast<size_t>(cells.x * cells.y * cells.z), glm::vec3(1.0f, 2.0f, 3.0f));
		AddWindToCells(wind, first, cells, cellSize, 77.3f, velocities);
		size_t index = 0;
		for (int32_t z = 0; z < cells.z; ++z)
		{
			for (int32_t y = 0; y < cells.y; ++y)
			{
				for (int32_t x = 0; x < cells.x; ++x, ++index)
				{
					const glm::vec3 centre = (glm::vec3(first + glm::ivec3(x, y, z)) + 0.5f) * cellSize;
					const glm::vec3 expected = glm::vec3(1.0f, 2.0f, 3.0f) + SampleWind(wind, centre, 77.3f);
					Require(std::memcmp(&velocities[index], &expected, sizeof(glm::vec3)) == 0,
						"A box of cells doesn't get each cell's wind to the bit at " + Describe(centre) + ": " + Describe(velocities[index]) + " for " + Describe(expected));
				}
			}
		}
	}

	// Slower near the ground, its speed at the reference height, faster aloft up to Aloft Height
	void TestTheHeightProfile()
	{
		WindComponent values;
		values.Gustiness = 0.0f;
		values.Turbulence = 0.0f;
		values.GroundHeight = 2.0f;
		values.GroundFactor = 0.5f;
		values.ReferenceHeight = 10.0f;
		values.Aloft = 1.0f;
		values.AloftHeight = 1010.0f;
		values.Speed = 4.0f;
		const WindState wind = MakeSteadyWind(values, 0.0);
		Require(Near(GetWindHeightFactor(wind, 2.0f), 0.5f) && Near(GetWindHeightFactor(wind, -5.0f), 0.5f), "The wind isn't Near Ground's part of its speed at the ground");
		Require(Near(GetWindHeightFactor(wind, 12.0f), 1.0f), "The wind isn't its speed at the reference height");
		Require(Near(GetWindHeightFactor(wind, 1012.0f), 2.0f, 1e-3f) && Near(GetWindHeightFactor(wind, 5000.0f), 2.0f, 1e-3f), "The wind isn't (1 + Aloft) as fast high up");
		float last = 0.0f;
		for (float y = 2.0f; y < 1200.0f; y += 0.5f)
		{
			const float factor = GetWindHeightFactor(wind, y);
			Require(factor >= last - 1e-6f, "The wind slows going up at " + std::to_string(y));
			last = factor;
		}
		Require(Near(glm::length(SampleWind(wind, glm::vec3(0.0f, 12.0f, 0.0f), 0.0f)), 4.0f, 1e-3f), "Speed isn't the wind's speed at the reference height");
	}

	// Fronts cross the land along the wind at their frequency times their spacing; a place sees them
	// come and go; across the wind a front is strong in places and weak in others
	void TestGustFrontsTravel()
	{
		WindComponent values;
		values.DirectionDegrees = 0.0f;
		values.Speed = 6.0f;
		values.GustFrequency = 0.1f;
		values.GustSpacing = 40.0f;
		values.GustPatchWidth = 25.0f;
		values.Turbulence = 0.0f;
		const WindState wind = MakeSteadyWind(values, 50.0);
		const float frontSpeed = values.GustFrequency * values.GustSpacing;

		// Carried downwind: what was at x is at x + 4 m/s × 3 s three seconds later
		int same = 0;
		for (int i = 0; i < 200; ++i)
		{
			const glm::vec3 point(static_cast<float>(i) * 0.37f - 30.0f, 1.0f, static_cast<float>(i % 17) * 3.1f - 20.0f);
			const float now = SampleWindGust(wind, point, 50.0f);
			const float later = SampleWindGust(wind, point + glm::vec3(frontSpeed * 3.0f, 0.0f, 0.0f), 53.0f);
			same += Near(now, later, 2e-3f) ? 1 : 0;
		}
		Require(same >= 196, "The gust fronts aren't carried downwind at their speed: " + std::to_string(same) + " of 200");

		// A place sees fronts come and go
		float lowest = 1.0f;
		float highest = 0.0f;
		for (float t = 50.0f; t < 150.0f; t += 0.1f)
		{
			const float gust = SampleWindGust(wind, glm::vec3(3.0f, 1.0f, 4.0f), t);
			lowest = std::min(lowest, gust);
			highest = std::max(highest, gust);
		}
		Require(lowest < 0.02f && highest > 0.3f && highest <= 1.0f, "A place doesn't see gust fronts come and go: " + std::to_string(lowest) + " to " + std::to_string(highest));

		// Along a front's crest, across the wind, its strength varies
		const float crest = (wind.GustPhase + 0.8f) / wind.To.GustsPerMetre;
		float weakest = 1.0f;
		float strongest = 0.0f;
		for (float z = -200.0f; z < 200.0f; z += 5.0f)
		{
			const float gust = SampleWindGust(wind, glm::vec3(crest, 1.0f, z), 50.0f);
			weakest = std::min(weakest, gust);
			strongest = std::max(strongest, gust);
		}
		Require(strongest - weakest > 0.3f, "A front is as strong all along its crest: " + std::to_string(weakest) + " to " + std::to_string(strongest));

		// And the wind there is (1 + Gustiness × the gust) as fast
		const glm::vec3 point(5.0f, 10.0f, 2.0f);
		const float gust = SampleWindGust(wind, point, 50.0f);
		const float expected = values.Speed * (1.0f + values.Gustiness * gust);
		Require(Near(SampleWind(wind, point, 50.0f).x, expected, 1e-3f), "The wind in a gust isn't as fast as its gustiness says");
	}

	// After hours of wind the phase and offset stay small, and a front still moves smoothly
	void TestLongWinds()
	{
		WindComponent values;
		const double later = 360000.0;
		const WindState wind = MakeSteadyWind(values, later);
		Require(wind.GustPhase >= 0.0f && wind.GustPhase < static_cast<float>(WindGustCycles), "The gust phase isn't kept within its repeat");
		const float repeat = static_cast<float>(WindEddyCycles) * values.EddySize;
		Require(std::abs(wind.To.EddyOffset.x) <= repeat && std::abs(wind.To.EddyOffset.y) <= repeat, "The eddies' offset isn't kept within its repeat");
		// A sixtieth of a second on: about as little change as at the start
		WindDrive drive;
		SetWindTarget(drive, values, 0.0);
		const auto change = [&](double t)
		{
			const WindState a = EvaluateWind(drive, t);
			const WindState b = EvaluateWind(drive, t + 1.0 / 60.0);
			const glm::vec3 point(4.0f, 1.0f, -3.0f);
			return glm::length(SampleWind(b, point, b.Time) - SampleWind(a, point, a.Time));
		};
		Require(change(later) < 0.5f, "After 100 hours a frame's change of wind jumps: " + std::to_string(change(later)));
	}

	// The largest change of the wind at any of a few points from one frame to the next, over a span
	float LargestFrameChange(const WindDrive& drive, double from, double to, double frame)
	{
		const glm::vec3 points[] = { { 0.0f, 0.5f, 0.0f }, { 35.0f, 2.0f, -12.0f }, { -120.0f, 8.0f, 60.0f }, { 400.0f, 1.0f, 400.0f } };
		float largest = 0.0f;
		WindState last = EvaluateWind(drive, from);
		for (double t = from + frame; t <= to; t += frame)
		{
			const WindState now = EvaluateWind(drive, t);
			for (const glm::vec3& point : points)
			{
				largest = std::max(largest, glm::length(SampleWind(now, point, now.Time) - SampleWind(last, point, last.Time)));
			}
			last = now;
		}
		return largest;
	}

	// Set new values: the wind turns to them over Change Time, never jumping, its fronts carried on
	void TestTurningSmoothly()
	{
		WindComponent calm;
		calm.Speed = 0.0f;
		WindComponent storm;
		storm.Speed = 16.0f;
		storm.Gustiness = 1.0f;
		storm.GustFrequency = 0.3f;
		storm.DirectionDegrees = 60.0f;
		storm.ChangeSeconds = 8.0f;

		WindDrive drive;
		Require(SetWindTarget(drive, calm, 0.0) && !IsWindChanging(drive, 0.0), "A first target doesn't start the wind steady");
		Require(!SetWindTarget(drive, calm, 5.0), "The same target turns the wind again");
		Require(SetWindTarget(drive, storm, 10.0) && IsWindChanging(drive, 12.0) && !IsWindChanging(drive, 18.5), "A new target doesn't turn the wind over its change time");

		// Before the change calm; halfway on its way; after it, the storm's
		Require(EvaluateWind(drive, 9.0).Speed == 0.0f && Near(EvaluateWind(drive, 14.0).Speed, 8.0f, 1e-3f) && EvaluateWind(drive, 30.0).Speed == 16.0f,
			"The wind doesn't turn from calm to the storm along the way");
		Require(Near(EvaluateWind(drive, 30.0).Direction.x, std::cos(glm::radians(60.0f))) && Near(EvaluateWind(drive, 30.0).Direction.y, -std::sin(glm::radians(60.0f))),
			"The wind doesn't turn to the storm's direction");

		// No frame of the ramp jumps more than the storm's own gusts and eddies move it in a frame
		const float ramp = LargestFrameChange(drive, 9.0, 20.0, 1.0 / 60.0);
		const float stormItself = LargestFrameChange(drive, 20.0, 31.0, 1.0 / 60.0);
		Require(ramp <= std::max(stormItself, 0.05f) * 1.5f, "The ramp from calm to the storm jumps: " + std::to_string(ramp) + " m/s in a frame, the storm itself " + std::to_string(stormItself));

		// Set the same targets at the same times and the wind is the same, to the bit, whatever the
		// frames or steps between
		const auto drive60 = [&](double step)
		{
			WindDrive other;
			for (double t = 0.0; t <= 25.0; t += step)
			{
				SetWindTarget(other, t < 10.0 ? calm : storm, t < 10.0 ? 0.0 : 10.0);
			}
			return other;
		};
		Require(SameBits(EvaluateWind(drive60(1.0 / 60.0), 21.3), EvaluateWind(drive, 21.3)) && SameBits(EvaluateWind(drive60(0.5), 13.7), EvaluateWind(drive, 13.7)),
			"The same targets at the same times give another wind for other steps");

		// Turned again halfway: from where it was, without a jump
		WindComponent breeze = storm;
		breeze.Speed = 3.0f;
		breeze.DirectionDegrees = -40.0f;
		breeze.GustSpacing = 70.0f;
		const WindState before = EvaluateWind(drive, 14.0);
		SetWindTarget(drive, breeze, 14.0);
		const WindState after = EvaluateWind(drive, 14.0);
		Require(Near(before.Speed, after.Speed, 1e-4f) && Near(before.GustPhase, after.GustPhase, 1e-3f) && Near(before.Direction.x, after.Direction.x, 1e-5f) &&
				Near(before.To.EddyOffset.x, after.To.EddyOffset.x, 1e-2f) && before.PatternBlend == after.PatternBlend,
			"Turning the wind again halfway jumps");
		Require(LargestFrameChange(drive, 13.0, 30.0, 1.0 / 60.0) <= std::max(stormItself, 0.05f) * 1.5f, "Turning the wind again halfway jumps in a frame");

		// Not blowing: it dies down, then is still
		WindComponent stopped = breeze;
		stopped.bBlowing = false;
		SetWindTarget(drive, stopped, 40.0);
		Require(EvaluateWind(drive, 40.0).Speed > 2.9f && EvaluateWind(drive, 60.0).Speed == 0.0f && SampleWind(EvaluateWind(drive, 60.0), glm::vec3(1.0f), 60.0f) == glm::vec3(0.0f),
			"A wind that stops blowing doesn't die down to still");
	}

	// What a weather system will do: set the wind's values over time, here from calm to a storm, on
	// a game world. The world turns its wind as it steps; its systems read it from SimContext and
	// know nothing else of it. Two worlds stepped alike see the same wind to the bit.
	class StormDriver final : public ISimSystem
	{
	public:
		const char* GetName() const override { return "Storm Driver"; }

		void Step(const SimContext& context) override
		{
			const double time = context.Step.WorldTimeSeconds;
			context.World.Each<WindComponent>(
				[&](Entity, WindComponent& wind)
				{
					// A target every few seconds: calm, a breeze, a storm
					wind.ChangeSeconds = 6.0f;
					wind.Speed = time < 2.0 ? 0.0f : (time < 6.0 ? 4.0f : 18.0f);
					wind.Gustiness = time < 6.0 ? 0.5f : 1.2f;
					wind.DirectionDegrees = time < 6.0 ? 20.0f : 75.0f;
				});
		}
	};

	class WindReader final : public ISimSystem
	{
	public:
		explicit WindReader(std::vector<glm::vec3>& felt) : Felt(felt) {}

		const char* GetName() const override { return "Wind Reader"; }

		void Step(const SimContext& context) override
		{
			Felt.push_back(SampleWind(context.Wind, glm::vec3(10.0f, 1.0f, -5.0f), static_cast<float>(context.Step.WorldTimeSeconds)));
		}

	private:
		std::vector<glm::vec3>& Felt;
	};

	std::vector<glm::vec3> RunStorm(GameWorld& world)
	{
		std::vector<glm::vec3> felt;
		Registry& registry = world.GetRegistry();
		WindComponent calm;
		calm.Speed = 0.0f;
		registry.Add<WindComponent>(registry.CreateEntity(), calm);
		world.AddSystem(std::make_unique<StormDriver>());
		world.AddSystem(std::make_unique<WindReader>(felt), 1, ESimStage::AfterPhysics);
		for (uint64_t step = 0; step < 60 * 20; ++step)
		{
			world.Step(StepTime{ .FixedDeltaSeconds = 1.0 / 60.0, .StepIndex = step, .WorldTimeSeconds = static_cast<double>(step) / 60.0 });
		}
		return felt;
	}

	void TestADriverRampsTheWind()
	{
		GameWorld first;
		GameWorld second;
		const std::vector<glm::vec3> a = RunStorm(first);
		const std::vector<glm::vec3> b = RunStorm(second);
		Require(a.size() == 1200 && std::memcmp(a.data(), b.data(), a.size() * sizeof(glm::vec3)) == 0, "Two worlds stepped alike feel another wind");

		// Calm, then it picks up to a storm, never jumping
		Require(glm::length(a[60]) == 0.0f && glm::length(a.back()) > 8.0f, "The driver's storm didn't blow: " + std::to_string(glm::length(a.back())));
		float largest = 0.0f;
		for (size_t step = 1; step < a.size(); ++step)
		{
			largest = std::max(largest, glm::length(a[step] - a[step - 1]));
		}
		Require(largest < 1.5f, "The ramp from calm to a storm jumps: " + std::to_string(largest) + " m/s in a step");

		// The world keeps its wind's drive where the renderer finds it, as it began the last step
		const Registry& registry = first.GetRegistry();
		const WindState world = GetWorldWind(registry, 1199.0 / 60.0);
		Require(SameBits(world, first.GetWind()), "The renderer wouldn't draw the wind the game's systems felt");
	}

	// A game's local air on a grid: the cells' centres, the grid round the camera on whole cells,
	// what the texture holds
	struct Plume final : IWindField
	{
		void SampleWind(std::span<const glm::vec3> positions, float, std::span<glm::vec3> outVelocities) const override
		{
			for (size_t index = 0; index < positions.size(); ++index)
			{
				outVelocities[index] = glm::length(positions[index] - glm::vec3(2.0f, 1.0f, 2.0f)) < 1.5f ? glm::vec3(0.0f, 3.0f, 0.0f) : glm::vec3(0.0f);
			}
		}

		void SampleWindGrid(const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities) const override
		{
			SampleWindGridAtCentres(*this, grid, time, outVelocities);
		}
	};

	void TestLocalAir()
	{
		LocalWindSettings settings;
		settings.Cells = 16;
		settings.Layers = 6;
		settings.CellSize = 0.5f;
		const WindFieldGrid grid = PlaceLocalWindGrid(glm::vec3(2.2f, 3.1f, 1.9f), settings);
		Require(grid.Cells == glm::ivec3(16, 6, 16) && grid.CellSize == 0.5f, "The local air's grid isn't as big as its settings");
		Require(grid.Origin == glm::floor(grid.Origin / 0.5f) * 0.5f, "The local air's grid isn't on whole cells");
		Require(grid.Origin.x <= 2.2f - 3.5f && grid.Origin.x + 8.0f >= 2.2f + 3.5f && grid.Origin.y < 3.1f - 1.5f && grid.Origin.y + 3.0f > 3.1f,
			"The local air's grid isn't round the camera");
		Require(PlaceLocalWindGrid(glm::vec3(2.3f, 3.1f, 1.9f), settings).Origin == grid.Origin, "The local air's grid swims as the camera moves within a cell");
		Require(ClampLocalWindSettings(LocalWindSettings{ .Cells = 1000, .Layers = 0, .CellSize = 0.0f }).Cells == 128, "The local air's settings aren't kept within range");

		const Plume plume;
		std::vector<glm::vec3> cells(grid.GetCellCount(), glm::vec3(0.0f));
		plume.SampleWindGrid(grid, 0.0f, cells);
		size_t rising = 0;
		for (int32_t z = 0; z < grid.Cells.z; ++z)
		{
			for (int32_t y = 0; y < grid.Cells.y; ++y)
			{
				for (int32_t x = 0; x < grid.Cells.x; ++x)
				{
					const glm::vec3 centre = grid.GetCellCentre(x, y, z);
					const glm::vec3 air = cells[static_cast<size_t>(x + grid.Cells.x * (y + grid.Cells.y * z))];
					Require(air == (glm::length(centre - glm::vec3(2.0f, 1.0f, 2.0f)) < 1.5f ? glm::vec3(0.0f, 3.0f, 0.0f) : glm::vec3(0.0f)),
						"A cell of the grid isn't asked at its centre, in the grid's order");
					rising += air.y > 0.0f ? 1 : 0;
				}
			}
		}
		Require(rising > 10, "The plume isn't in the grid");
		std::vector<uint16_t> texels;
		Require(PackLocalWind(cells, texels) && texels.size() == cells.size() * 4, "The local air isn't packed a texel a cell");
		std::vector<glm::vec3> still(cells.size(), glm::vec3(0.0f));
		Require(!PackLocalWind(still, texels), "Still air is packed as if it moved");
	}

	// The debug view: an arrow a point round the camera, and the fronts' crests
	void TestTheDebugView()
	{
		if (!DebugDraw::Enabled)
		{
			return;
		}
		WindComponent values;
		const WindState wind = MakeSteadyWind(values, 12.0);
		DebugDraw arrows;
		DrawWindDebug(arrows, wind, glm::vec3(5.0f, 3.0f, 5.0f), 0.0f, 12.0f, false);
		const size_t perSide = static_cast<size_t>(2.0f * WindDebugArrowReach / WindDebugArrowSpacing) + 1;
		Require(arrows.GetLines().size() >= perSide * perSide, "The debug view doesn't draw an arrow a point");
		DebugDraw fronts;
		DrawWindDebug(fronts, wind, glm::vec3(5.0f, 3.0f, 5.0f), 0.0f, 12.0f, true);
		Require(fronts.GetLines().size() > arrows.GetLines().size() + 50, "The debug view doesn't draw the fronts' crests");
		DebugDraw calm;
		DrawWindDebug(calm, WindState{}, glm::vec3(0.0f), 0.0f, 0.0f, true);
		Require(!calm.GetLines().empty(), "The debug view of a calm wind draws nothing at all");
	}

	// How far the wind has carried things: steady, as fast as it blows; across a change, without a
	// jump; the same for a drive as for the steady wind it starts as
	void TestTheWindsTravel()
	{
		WindComponent breeze;
		breeze.DirectionDegrees = 0.0f;
		breeze.Speed = 5.0f;
		Require(GetSteadyWindTravel(breeze, 10.0) == glm::dvec2(50.0, -0.0) || glm::length(GetSteadyWindTravel(breeze, 10.0) - glm::dvec2(50.0, 0.0)) < 1e-9,
			"A steady wind hasn't carried things as far as it blows");
		WindDrive drive;
		SetWindTarget(drive, breeze, 0.0);
		Require(glm::length(GetWindTravel(drive, 20.0) - GetSteadyWindTravel(breeze, 20.0)) < 1e-9, "A drive started steady doesn't carry things as the steady wind does");
		WindComponent faster = breeze;
		faster.Speed = 15.0f;
		faster.DirectionDegrees = 90.0f;
		const glm::dvec2 before = GetWindTravel(drive, 30.0);
		SetWindTarget(drive, faster, 30.0);
		Require(glm::length(GetWindTravel(drive, 30.0) - before) < 1e-9, "A change of the wind makes what it carries jump");
		const glm::dvec2 later = GetWindTravel(drive, 100.0) - GetWindTravel(drive, 99.0);
		Require(glm::length(later - glm::dvec2(0.0, -15.0)) < 1e-6, "After the change the wind doesn't carry things as fast as it blows, its new way");
	}

	// Clouds at an altitude in a hand-made wind: carried by it as far as it carried things at its
	// reference height times its profile there, their streaks along it; with Own Wind, by their own
	void TestCloudsTakeTheWindAtTheirAltitude()
	{
		WindState wind;
		wind.Direction = glm::normalize(glm::vec2(0.6f, -0.8f));
		wind.Speed = 6.0f;
		wind.GroundFactor = 0.5f;
		wind.InverseReferenceHeight = 0.1f;
		wind.Aloft = 1.0f;
		wind.InverseAloftSpan = 1.0f / 990.0f;
		wind.AloftStart = 10.0f / 990.0f;
		const glm::dvec2 travel(1200.0, -1600.0);
		const CloudDrift drift = GetCloudDrift(wind, travel, 4000.0f);
		Require(Near(drift.Velocity.x, 0.6f * 12.0f, 1e-3f) && Near(drift.Velocity.y, -0.8f * 12.0f, 1e-3f) && glm::length(drift.Travel - travel * 2.0) < 1e-6,
			"Clouds high up aren't carried by the wind there, twice as fast as at its reference height");

		const glm::vec3 camera(30.0f, 2.0f, -70.0f);
		CloudLayerComponent layer;
		const SceneViewCloudLayerSettings carried = MakeSceneViewCloudLayerSettings(layer, camera, 50.0, &drift);
		Require(Near(carried.WindAxis.x, 0.6f) && Near(carried.WindAxis.y, -0.8f), "The layer's streaks don't lie along the world's wind");
		// The pattern over a point is the pattern the wind has carried there from upwind
		const glm::dvec2 point(100.0, 200.0);
		const glm::dvec2 there = GetCloudPatternPoint(carried, point + drift.Travel, drift.Travel);
		const glm::dvec2 before = GetCloudPatternPoint(carried, point, glm::dvec2(0.0));
		Require(glm::length(there - before) < 1e-9, "The layer isn't carried as far as the world's wind carried it");
		CloudLayerComponent own = layer;
		own.bOwnWind = true;
		const SceneViewCloudLayerSettings ownWind = MakeSceneViewCloudLayerSettings(own, camera, 50.0, &drift);
		const SceneViewCloudLayerSettings alone = MakeSceneViewCloudLayerSettings(own, camera, 50.0);
		Require(ownWind.Origin == alone.Origin && ownWind.WindAxis == alone.WindAxis, "With Own Wind the layer is carried by the world's wind");
		Require(MakeSceneViewCloudLayerSettings(layer, camera, 50.0).Origin == alone.Origin, "Without a world wind the layer isn't carried by its own");

		VolumetricCloudsComponent band;
		const CloudDrift low = GetCloudDrift(wind, travel, 0.5f * (band.BottomAltitude + band.TopAltitude));
		const SceneViewVolumetricCloudsSettings volume = MakeSceneViewVolumetricCloudsSettings(band, camera, 50.0, &low);
		Require(glm::length(volume.WindVelocity - low.Velocity * 0.001f) < 1e-6f, "The volumetric clouds don't move with the world's wind in their band");
		const glm::dvec2 weather = GetVolumetricCloudPatternPoint(point + low.Travel, low.Travel, volume.WeatherRepeat);
		Require(glm::length(weather - GetVolumetricCloudPatternPoint(point, glm::dvec2(0.0), volume.WeatherRepeat)) < 1e-9,
			"The volumetric clouds aren't carried as far as the world's wind carried them");
		VolumetricCloudsComponent ownBand = band;
		ownBand.bOwnWind = true;
		const SceneViewVolumetricCloudsSettings ownVolume = MakeSceneViewVolumetricCloudsSettings(ownBand, camera, 50.0, &low);
		const SceneViewVolumetricCloudsSettings aloneVolume = MakeSceneViewVolumetricCloudsSettings(ownBand, camera, 50.0);
		Require(ownVolume.WeatherOrigin == aloneVolume.WeatherOrigin && ownVolume.WindVelocity == aloneVolume.WindVelocity,
			"With Own Wind the volumetric clouds are carried by the world's wind");
	}

	// An old scene's wind is moved into one of its own only if the world has none
	void TestTheOldWindMoves()
	{
		Registry world;
		const Entity first = world.CreateEntity();
		EnvironmentComponent environment;
		environment.Wind = glm::vec3(0.0f, 0.0f, -3.0f);
		world.Add<EnvironmentComponent>(first, environment);
		const Entity sky = world.CreateEntity();
		world.Add<CloudLayerComponent>(sky, CloudLayerComponent{});
		world.Add<VolumetricCloudsComponent>(sky, VolumetricCloudsComponent{});
		MoveLegacyEnvironmentWind(world);
		Require(world.Has<WindComponent>(first) && world.Get<EnvironmentComponent>(first).Wind == glm::vec3(0.0f), "The environment's wind isn't moved into a wind");
		Require(world.Get<CloudLayerComponent>(sky).bOwnWind && world.Get<VolumetricCloudsComponent>(sky).bOwnWind,
			"An old scene's clouds, which blew by their own wind, don't keep doing so once it has a world wind");
		Require(Near(SampleWind(MakeSteadyWind(world.Get<WindComponent>(first), 0.0), glm::vec3(0.0f, 500.0f, 0.0f), 0.0f), glm::vec3(0.0f, 0.0f, -3.0f)),
			"The moved wind doesn't blow as the environment's did");

		Registry both;
		const Entity own = both.CreateEntity();
		both.Add<WindComponent>(own, WindComponent{});
		const Entity old = both.CreateEntity();
		both.Add<EnvironmentComponent>(old, environment);
		MoveLegacyEnvironmentWind(both);
		Require(!both.Has<WindComponent>(old) && both.Get<EnvironmentComponent>(old).Wind == glm::vec3(0.0f), "A world with a wind of its own got another from its environment");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();
		TestTheContract();
		TestABoxOfCells();
		TestTheHeightProfile();
		TestGustFrontsTravel();
		TestLongWinds();
		TestTurningSmoothly();
		TestADriverRampsTheWind();
		TestLocalAir();
		TestTheDebugView();
		TestTheWindsTravel();
		TestCloudsTakeTheWindAtTheirAltitude();
		TestTheOldWindMoves();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All wind tests passed\n";
	return 0;
}
