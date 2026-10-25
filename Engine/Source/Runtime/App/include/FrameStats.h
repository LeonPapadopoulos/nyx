#pragma once

#include "SceneViewTypes.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace Nyx
{
	// The numbers a frame is measured by (FrameStatsSample): times in milliseconds, then counts.
	enum class EFrameStat : uint8_t
	{
		// CPU: the whole frame; the layers' OnUpdate() and OnLateUpdate(); the fixed steps of the
		// simulation, and how many there were
		FrameTime,
		Update,
		Simulation,
		SimulationSteps,
		// The renderer on the CPU (RenderFrameStats): extracting the worlds, culling, recording the
		// scene views; building and recording the UI; waiting for the GPU
		Extract,
		Cull,
		Record,
		UI,
		WaitForGpu,
		// The GPU: the frame's whole command buffer, and the scene views' passes (the scene pass
		// with the MSAA resolve at its end; the colour grading's bake, in frames that bake)
		GpuFrame,
		GpuShadows,
		GpuScene,
		GpuAmbientOcclusion,
		GpuParticles,
		GpuBloom,
		GpuPicture,
		GpuColorGrading,
		// The scene views drawn and the samples a pixel they drew with (MSAA, 1 without), and what all
		// of them drew (SceneViewFrameStats), summed: their scene passes' draw calls, those of many
		// instances, the instances drawn and culled, the triangles; their shadow cascades, their
		// draw calls and triangles; the surface decals; the point lights each lit its pixels with;
		// the particle emitters drawn, and their particles
		SceneViews,
		MsaaSamples,
		DrawCalls,
		InstancedDrawCalls,
		Instances,
		InstancesCulled,
		Triangles,
		ShadowCascades,
		ShadowDrawCalls,
		ShadowTriangles,
		Decals,
		PointLights,
		ParticleEmitters,
		Particles,
		Count
	};

	constexpr size_t FrameStatCount = static_cast<size_t>(EFrameStat::Count);

	// A number's name as a CSV column (e.g. "frame_ms", "draw_calls"), and whether it is a time in
	// milliseconds or a count
	struct FrameStatInfo
	{
		std::string_view Name;
		bool bMilliseconds = false;
	};
	const FrameStatInfo& GetFrameStatInfo(EFrameStat stat);

	// One frame's numbers, or a window's average or worst of them (FrameStatsWindow)
	struct FrameStatsSample
	{
		// The renderer's frame (RenderFrameStats::Frame); for a window, its last frame's
		uint64_t Frame = 0;
		std::array<double, FrameStatCount> Values{};

		double& operator[](EFrameStat stat) { return Values[static_cast<size_t>(stat)]; }
		double operator[](EFrameStat stat) const { return Values[static_cast<size_t>(stat)]; }
	};

	// What the application's frame loop took itself on the CPU, in milliseconds (Application)
	struct ApplicationFrameTimes
	{
		// Every layer's OnUpdate(), the fixed steps, and every layer's OnLateUpdate()
		float Update = 0.0f;
		float Simulation = 0.0f;
		uint32_t SimulationSteps = 0;
		float LateUpdate = 0.0f;
	};

	// A frame's numbers: the renderer's, its scene views' summed, and the application's
	FrameStatsSample MakeFrameStatsSample(const RenderFrameStats& render, const ApplicationFrameTimes& application);

	// Frames' numbers over windows of about a second: each window's average and its worst (the
	// highest of each number), out once the window has closed, so what an overlay shows holds still
	// for a second; and the last frames' CPU and GPU times, for a graph. A window closes once its
	// frames' FrameTime adds up to its length. Adding a frame allocates nothing.
	class FrameStatsWindow
	{
	public:
		// The frames the graph's history keeps: five seconds up to 800 frames a second
		static constexpr size_t HistorySize = 4096;

		explicit FrameStatsWindow(double windowMilliseconds = 1000.0);

		void Add(const FrameStatsSample& sample);

		// The last window that closed: the average and worst of each number, and its frames; all 0
		// before the first window has closed
		const FrameStatsSample& GetAverage() const { return Average; }
		const FrameStatsSample& GetWorst() const { return Worst; }
		uint32_t GetWindowFrames() const { return WindowFrames; }

		// The frame added last, and how many were added
		const FrameStatsSample& GetLast() const { return Last; }
		uint64_t GetFrameCount() const { return FrameCount; }

		// The last frames' CPU frame time and GPU time, oldest first, `index` below
		// GetHistoryCount()
		size_t GetHistoryCount() const;
		float GetHistoryFrameTime(size_t index) const;
		float GetHistoryGpuTime(size_t index) const;

	private:
		double WindowMilliseconds = 1000.0;

		// The window being filled: its sums, highest values and frames
		FrameStatsSample Sum;
		FrameStatsSample Highest;
		uint32_t Frames = 0;
		double Elapsed = 0.0;

		FrameStatsSample Average;
		FrameStatsSample Worst;
		uint32_t WindowFrames = 0;

		FrameStatsSample Last;
		uint64_t FrameCount = 0;

		std::array<float, HistorySize> HistoryFrameTimes{};
		std::array<float, HistorySize> HistoryGpuTimes{};
	};
}
