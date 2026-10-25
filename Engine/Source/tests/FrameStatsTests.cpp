// Frame statistics without a GPU: a frame's numbers from the renderer's and the application's
// (MakeFrameStatsSample()), their windows' averages and worst (FrameStatsWindow), the graph's
// history; a benchmark's summary, its CSV, and its camera path (FrameBenchmark.h).
#include "FrameBenchmark.h"
#include "FrameStats.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;

	using Nyx::Tests::Require;

	bool Near(double a, double b)
	{
		return std::abs(a - b) < 1.0e-4;
	}

	FrameStatsSample MakeFrame(uint64_t frame, double milliseconds, double drawCalls = 0.0)
	{
		FrameStatsSample sample;
		sample.Frame = frame;
		sample[EFrameStat::FrameTime] = milliseconds;
		sample[EFrameStat::GpuFrame] = milliseconds / 2.0;
		sample[EFrameStat::DrawCalls] = drawCalls;
		return sample;
	}

	void TestSampleFromRenderer()
	{
		RenderFrameStats render;
		render.Frame = 42;
		render.FrameTime = 16.5f;
		render.Extract = 0.5f;
		render.Cull = 0.25f;
		render.Record = 1.0f;
		render.UI = 0.75f;
		render.WaitForGpu = 9.0f;
		render.GpuFrame = 12.0f;
		render.SceneViews = 2;
		render.Views.Gpu = SceneViewGpuTimes{ .Shadows = 3.0f, .Scene = 6.0f, .AmbientOcclusion = 1.0f, .Particles = 0.75f, .Bloom = 0.5f, .Picture = 0.25f, .ColorGrading = 0.125f };
		render.MsaaSamples = 4;
		render.Views.Scene.DrawCalls = 120;
		render.Views.Scene.InstancedDrawCalls = 40;
		render.Views.Scene.Instances = 38000;
		render.Views.Scene.InstancesCulled = 12000;
		render.Views.Scene.Triangles = 5'000'000'000ull;
		render.Views.ShadowCascades = 4;
		render.Views.Shadows.DrawCalls = 300;
		render.Views.Shadows.Triangles = 2'400'000;
		render.Views.Decals = 50;
		// 12 lights, 3 out of view, 1 past the most a view lights: 8 lit
		render.Views.Scene.PointLights = 12;
		render.Views.Scene.PointLightsCulled = 3;
		render.Views.Scene.PointLightsDropped = 1;
		render.Views.Scene.ParticleEmitters = 5;
		render.Views.Scene.ParticleEmittersCulled = 2;
		render.Views.Scene.Particles = 900;

		const FrameStatsSample sample = MakeFrameStatsSample(render, ApplicationFrameTimes{ .Update = 0.5f, .Simulation = 2.0f, .SimulationSteps = 2, .LateUpdate = 0.25f });
		Require(sample.Frame == 42, "The sample should be the renderer's frame");
		Require(Near(sample[EFrameStat::FrameTime], 16.5) && Near(sample[EFrameStat::Update], 0.75), "The frame time, or the updates before and after the steps together");
		Require(Near(sample[EFrameStat::Simulation], 2.0) && sample[EFrameStat::SimulationSteps] == 2.0, "The simulation's time and steps");
		Require(Near(sample[EFrameStat::Extract], 0.5) && Near(sample[EFrameStat::Cull], 0.25) && Near(sample[EFrameStat::Record], 1.0) &&
				Near(sample[EFrameStat::UI], 0.75) && Near(sample[EFrameStat::WaitForGpu], 9.0),
			"The renderer's CPU times");
		Require(Near(sample[EFrameStat::GpuFrame], 12.0) && Near(sample[EFrameStat::GpuShadows], 3.0) && Near(sample[EFrameStat::GpuScene], 6.0) &&
				Near(sample[EFrameStat::GpuAmbientOcclusion], 1.0) && Near(sample[EFrameStat::GpuParticles], 0.75) && Near(sample[EFrameStat::GpuBloom], 0.5) &&
				Near(sample[EFrameStat::GpuPicture], 0.25) && Near(sample[EFrameStat::GpuColorGrading], 0.125),
			"The GPU times");
		Require(sample[EFrameStat::SceneViews] == 2.0 && sample[EFrameStat::DrawCalls] == 120.0 && sample[EFrameStat::InstancedDrawCalls] == 40.0 &&
				sample[EFrameStat::Instances] == 38000.0 && sample[EFrameStat::InstancesCulled] == 12000.0,
			"The views' draws");
		Require(sample[EFrameStat::Triangles] == 5.0e9, "Triangles beyond 32 bits should be kept whole");
		Require(sample[EFrameStat::ShadowCascades] == 4.0 && sample[EFrameStat::ShadowDrawCalls] == 300.0 && sample[EFrameStat::ShadowTriangles] == 2.4e6 &&
				sample[EFrameStat::Decals] == 50.0,
			"The shadows and decals");
		Require(sample[EFrameStat::MsaaSamples] == 4.0, "The samples a pixel");
		Require(sample[EFrameStat::PointLights] == 8.0, "The lights lit are those neither culled nor dropped");
		Require(sample[EFrameStat::ParticleEmitters] == 3.0 && sample[EFrameStat::Particles] == 900.0, "The emitters drawn and their particles");
	}

	void TestViewsAddUp()
	{
		SceneViewFrameStats first;
		first.Scene.DrawCalls = 3;
		first.Scene.Triangles = 100;
		first.ShadowCascades = 4;
		first.Shadows.InstancesCulled = 7;
		first.Decals = 2;
		first.Gpu.Scene = 1.5f;
		SceneViewFrameStats second = first;
		second.Gpu.Bloom = 0.5f;

		SceneViewFrameStats sum;
		sum += first;
		sum += second;
		Require(sum.Scene.DrawCalls == 6 && sum.Scene.Triangles == 200 && sum.ShadowCascades == 8 && sum.Shadows.InstancesCulled == 14 && sum.Decals == 4,
			"Two views' counts should add up");
		Require(Near(sum.Gpu.Scene, 3.0) && Near(sum.Gpu.Bloom, 0.5) && Near(sum.Gpu.GetTotal(), 3.5), "Two views' GPU times should add up");
	}

	void TestWindowAverageAndWorst()
	{
		FrameStatsWindow window(100.0);
		Require(window.GetWindowFrames() == 0 && window.GetAverage()[EFrameStat::FrameTime] == 0.0, "Nothing should be out before a window closes");

		// 10 + 20 + 30 + 40 = 100 ms: the window closes with its fourth frame
		window.Add(MakeFrame(1, 10.0, 5.0));
		window.Add(MakeFrame(2, 20.0, 9.0));
		window.Add(MakeFrame(3, 30.0, 1.0));
		Require(window.GetWindowFrames() == 0, "The window shouldn't close before its frames add up to its length");
		Require(window.GetLast().Frame == 3 && window.GetFrameCount() == 3, "The last frame is the one added last");
		window.Add(MakeFrame(4, 40.0, 5.0));
		Require(window.GetWindowFrames() == 4, "The window should close with the frame that fills it");
		Require(Near(window.GetAverage()[EFrameStat::FrameTime], 25.0) && Near(window.GetAverage()[EFrameStat::DrawCalls], 5.0), "The window's average");
		Require(Near(window.GetWorst()[EFrameStat::FrameTime], 40.0) && Near(window.GetWorst()[EFrameStat::DrawCalls], 9.0),
			"The window's worst is the highest of each number, from whichever frame");
		Require(window.GetAverage().Frame == 4 && window.GetWorst().Frame == 4, "A window's numbers carry its last frame");

		// The next window starts empty: its worst is lower than the first's, and the first's stay out
		// until it closes
		window.Add(MakeFrame(5, 60.0, 2.0));
		Require(Near(window.GetAverage()[EFrameStat::FrameTime], 25.0), "The last window's average should hold until the next closes");
		window.Add(MakeFrame(6, 60.0, 4.0));
		Require(window.GetWindowFrames() == 2 && Near(window.GetAverage()[EFrameStat::FrameTime], 60.0) && Near(window.GetWorst()[EFrameStat::DrawCalls], 4.0),
			"The second window holds only its own frames");

		// A frame longer than the window closes one by itself
		window.Add(MakeFrame(7, 250.0, 1.0));
		Require(window.GetWindowFrames() == 1 && Near(window.GetWorst()[EFrameStat::FrameTime], 250.0), "A long frame is a window of its own");
	}

	void TestHistory()
	{
		FrameStatsWindow window(1000.0);
		Require(window.GetHistoryCount() == 0, "No history before the first frame");
		for (uint64_t frame = 0; frame < 3; ++frame)
		{
			window.Add(MakeFrame(frame, 1.0 + static_cast<double>(frame)));
		}
		Require(window.GetHistoryCount() == 3, "Each frame should be in the history");
		Require(window.GetHistoryFrameTime(0) == 1.0f && window.GetHistoryFrameTime(2) == 3.0f, "The history should be oldest first");
		Require(window.GetHistoryGpuTime(2) == 1.5f, "The history keeps the GPU's time too");

		// Past its size, the oldest frames go
		const size_t frames = FrameStatsWindow::HistorySize + 10;
		for (size_t frame = 3; frame < frames; ++frame)
		{
			window.Add(MakeFrame(frame, 1.0 + static_cast<double>(frame)));
		}
		Require(window.GetHistoryCount() == FrameStatsWindow::HistorySize, "The history should keep its size");
		Require(window.GetHistoryFrameTime(0) == static_cast<float>(1 + frames - FrameStatsWindow::HistorySize), "The oldest kept frame is first");
		Require(window.GetHistoryFrameTime(FrameStatsWindow::HistorySize - 1) == static_cast<float>(frames), "The newest frame is last");
	}

	void TestBenchmarkSummary()
	{
		// 1 to 200 ms: the 99th percentile by nearest rank is the 198th frame, 198 ms
		std::vector<FrameStatsSample> frames;
		for (uint64_t frame = 0; frame < 200; ++frame)
		{
			frames.push_back(MakeFrame(frame, static_cast<double>(200 - frame), 7.0));
		}
		const FrameBenchmarkSummary summary = SummarizeFrameBenchmark(frames);
		Require(summary.Frames == 200 && Near(summary.Milliseconds, 20100.0), "The summary's frames and time");
		Require(Near(summary.Average[EFrameStat::FrameTime], 100.5), "The average frame time");
		Require(Near(summary.OnePercentLow[EFrameStat::FrameTime], 198.0), "The 1% low is what only the slowest 1% exceed");
		Require(Near(summary.Worst[EFrameStat::FrameTime], 200.0), "The worst is the slowest frame");
		Require(Near(summary.OnePercentLow[EFrameStat::DrawCalls], 7.0) && Near(summary.Worst[EFrameStat::DrawCalls], 7.0), "Each number has its own");

		const FrameBenchmarkSummary one = SummarizeFrameBenchmark(std::vector<FrameStatsSample>{ MakeFrame(0, 5.0) });
		Require(Near(one.Average[EFrameStat::FrameTime], 5.0) && Near(one.OnePercentLow[EFrameStat::FrameTime], 5.0) && Near(one.Worst[EFrameStat::FrameTime], 5.0),
			"A single frame is its own average, 1% low and worst");
		Require(SummarizeFrameBenchmark({}).Frames == 0, "No frames, no summary");

		const std::string line = FormatFrameBenchmarkSummary(summary);
		Require(line.find("200 frames") != std::string::npos && line.find("1% low 198.00 ms") != std::string::npos, "The summary line: " + line);
	}

	void TestBenchmarkCsv()
	{
		std::vector<FrameStatsSample> frames = { MakeFrame(10, 10.0, 3.0), MakeFrame(11, 20.0, 4.0) };
		const std::string csv = MakeFrameBenchmarkCsv(frames, SummarizeFrameBenchmark(frames));

		std::vector<std::string> lines;
		std::istringstream stream(csv);
		for (std::string line; std::getline(stream, line);)
		{
			lines.push_back(line);
		}
		Require(lines.size() == 1 + 2 + 3, "A header, a row a frame and three summary rows, not " + std::to_string(lines.size()) + " lines");
		Require(lines[0].starts_with("frame,time_s,frame_ms,update_ms,simulation_ms,"), "The header: " + lines[0]);
		Require(lines[0].ends_with(",decals,point_lights,particle_emitters,particles"), "The header should end with the last numbers: " + lines[0]);

		const auto columns = [](const std::string& line)
		{
			return static_cast<size_t>(std::count(line.begin(), line.end(), ',')) + 1;
		};
		for (const std::string& line : lines)
		{
			Require(columns(line) == 2 + FrameStatCount, "Every row should have every column: " + line);
		}

		// Frames from 0, the time at each frame's end, times with three decimals, counts whole
		Require(lines[1].starts_with("0,0.010,10.000,"), "The first frame's row: " + lines[1]);
		Require(lines[2].starts_with("1,0.030,20.000,"), "The second frame's row: " + lines[2]);
		const size_t drawCallsColumn = 2 + static_cast<size_t>(EFrameStat::DrawCalls);
		const auto column = [](const std::string& line, size_t index)
		{
			size_t start = 0;
			for (size_t comma = 0; comma < index; ++comma)
			{
				start = line.find(',', start) + 1;
			}
			return line.substr(start, line.find(',', start) - start);
		};
		Require(column(lines[2], drawCallsColumn) == "4", "A count should be whole: " + column(lines[2], drawCallsColumn));
		Require(lines[3].starts_with("average,0.030,15.000,") && lines[4].starts_with("1% low,0.030,20.000,") && lines[5].starts_with("worst,0.030,20.000,"),
			"The summary rows: " + lines[3] + " / " + lines[4] + " / " + lines[5]);
	}

	void TestCameraPath()
	{
		std::string error;
		const std::optional<BenchmarkCameraPath> path = ParseBenchmarkCameraPath(
			"# A pass over the valley\r\n"
			"0   0 10 -20   0 0 0\r\n"
			"\r\n"
			"10  20 10 0    0 0 0   # round the side\n"
			"10  20 10 0    0 2 0\n",
			error);
		Require(path.has_value(), "A valid path should be read: " + error);
		Require(path->Keys.size() == 3, "Comments and blank lines aren't keys");

		const BenchmarkCameraKey start = SampleBenchmarkCameraPath(*path, -1.0f);
		Require(start.Position == glm::vec3(0.0f, 10.0f, -20.0f), "Before the first key, the camera is at it");
		const BenchmarkCameraKey middle = SampleBenchmarkCameraPath(*path, 5.0f);
		Require(glm::length(middle.Position - glm::vec3(10.0f, 10.0f, -10.0f)) < 1.0e-4f, "Halfway, the camera is halfway along");
		Require(SampleBenchmarkCameraPath(*path, 10.0f).LookAt == glm::vec3(0.0f, 2.0f, 0.0f), "At a key's time, the last key of that time");
		Require(SampleBenchmarkCameraPath(*path, 99.0f).Position == glm::vec3(20.0f, 10.0f, 0.0f), "After the last key, the camera stays at it");

		for (const char* bad : { "", "# only a comment\n", "0 1 2 3 4 5", "0 1 2 3 4 5 6 7", "zero 1 2 3 4 5 6", "5 0 0 0 1 1 1\n1 0 0 0 1 1 1",
				 "0 1 1 1 1 1 1" })
		{
			error.clear();
			Require(!ParseBenchmarkCameraPath(bad, error) && !error.empty(), std::string("Accepted the camera path '") + bad + "'");
		}
		error.clear();
		ParseBenchmarkCameraPath("0 0 0 0 0 0 1\n0 0 0 0 0 0", error);
		Require(error.find("Line 2") != std::string::npos, "The error should name the line: " + error);
	}
}

int main()
{
	try
	{
		TestSampleFromRenderer();
		TestViewsAddUp();
		TestWindowAverageAndWorst();
		TestHistory();
		TestBenchmarkSummary();
		TestBenchmarkCsv();
		TestCameraPath();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Frame stats test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All frame stats tests passed.\n";
	return 0;
}
