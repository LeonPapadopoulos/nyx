// How long a physics step takes with many bodies: 1000 boxes dropped in a grid onto a floor,
// stepped at 60 Hz. Not a test (timing depends on the machine); run it in Release to compare with
// the simulation step's budget of 4 ms.
#include "Log.h"
#include "PhysicsWorld.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <vector>

int main()
{
	Nyx::Logger::Get().Init();
	using namespace Nyx;

	PhysicsWorld world;
	BodyDesc floor;
	floor.Shape.HalfExtents = glm::vec3(50.0f, 0.5f, 50.0f);
	floor.Position = glm::vec3(0.0f, -0.5f, 0.0f);
	floor.Motion = EBodyMotion::Static;
	world.CreateBody(floor);

	for (int i = 0; i < 1000; ++i)
	{
		BodyDesc box;
		box.Position = glm::vec3(float(i % 10) * 1.2f - 6.0f, 1.0f + float(i / 100) * 1.2f, float((i / 10) % 10) * 1.2f - 6.0f);
		world.CreateBody(box);
	}

	constexpr int Steps = 600;
	std::vector<double> times;
	times.reserve(Steps);
	for (int step = 0; step < Steps; ++step)
	{
		const auto start = std::chrono::steady_clock::now();
		world.Step(1.0f / 60.0f);
		times.push_back(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count());
	}

	double total = 0.0;
	for (const double time : times)
	{
		total += time;
	}
	std::sort(times.begin(), times.end());
	std::cout << world.GetBodyCount() << " bodies, " << Steps << " steps at 60 Hz: " << total / Steps << " ms a step on average, "
			  << times[Steps / 2] << " ms median, " << times[Steps * 99 / 100] << " ms at the 99th percentile, " << times.back()
			  << " ms at most (budget: 4 ms)\n";
	return 0;
}
