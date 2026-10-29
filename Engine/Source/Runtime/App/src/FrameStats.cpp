#include "FrameStats.h"

#include <algorithm>

namespace Nyx
{
	const FrameStatInfo& GetFrameStatInfo(EFrameStat stat)
	{
		static const std::array<FrameStatInfo, FrameStatCount> infos = { {
			{ "frame_ms", true },
			{ "update_ms", true },
			{ "simulation_ms", true },
			{ "simulation_steps", false },
			{ "extract_ms", true },
			{ "cull_ms", true },
			{ "record_ms", true },
			{ "ui_ms", true },
			{ "wait_for_gpu_ms", true },
			{ "gpu_frame_ms", true },
			{ "gpu_sky_ms", true },
			{ "gpu_clouds_ms", true },
			{ "gpu_volumetric_clouds_ms", true },
			{ "gpu_cloud_lightning_ms", true },
			{ "gpu_shadows_ms", true },
			{ "gpu_scene_ms", true },
			{ "gpu_ambient_occlusion_ms", true },
			{ "gpu_lightning_ms", true },
			{ "gpu_particles_ms", true },
			{ "gpu_light_shafts_ms", true },
			{ "gpu_bloom_ms", true },
			{ "gpu_picture_ms", true },
			{ "gpu_color_grading_ms", true },
			{ "scene_views", false },
			{ "msaa_samples", false },
			{ "draw_calls", false },
			{ "instanced_draw_calls", false },
			{ "instances", false },
			{ "instances_culled", false },
			{ "triangles", false },
			{ "shadow_cascades", false },
			{ "shadow_draw_calls", false },
			{ "shadow_triangles", false },
			{ "decals", false },
			{ "point_lights", false },
			{ "particle_emitters", false },
			{ "particles", false },
			{ "lightning_bolts", false },
		} };
		return infos[std::min(static_cast<size_t>(stat), FrameStatCount - 1)];
	}

	FrameStatsSample MakeFrameStatsSample(const RenderFrameStats& render, const ApplicationFrameTimes& application)
	{
		FrameStatsSample sample;
		sample.Frame = render.Frame;
		sample[EFrameStat::FrameTime] = render.FrameTime;
		sample[EFrameStat::Update] = application.Update + application.LateUpdate;
		sample[EFrameStat::Simulation] = application.Simulation;
		sample[EFrameStat::SimulationSteps] = application.SimulationSteps;
		sample[EFrameStat::Extract] = render.Extract;
		sample[EFrameStat::Cull] = render.Cull;
		sample[EFrameStat::Record] = render.Record;
		sample[EFrameStat::UI] = render.UI;
		sample[EFrameStat::WaitForGpu] = render.WaitForGpu;

		const SceneViewFrameStats& views = render.Views;
		sample[EFrameStat::GpuFrame] = render.GpuFrame;
		sample[EFrameStat::GpuSky] = views.Gpu.Sky;
		sample[EFrameStat::GpuClouds] = views.Gpu.Clouds;
		sample[EFrameStat::GpuVolumetricClouds] = views.Gpu.VolumetricClouds;
		sample[EFrameStat::GpuCloudLightning] = views.Gpu.CloudLightning;
		sample[EFrameStat::GpuShadows] = views.Gpu.Shadows;
		sample[EFrameStat::GpuScene] = views.Gpu.Scene;
		sample[EFrameStat::GpuAmbientOcclusion] = views.Gpu.AmbientOcclusion;
		sample[EFrameStat::GpuLightning] = views.Gpu.Lightning;
		sample[EFrameStat::GpuParticles] = views.Gpu.Particles;
		sample[EFrameStat::GpuLightShafts] = views.Gpu.LightShafts;
		sample[EFrameStat::GpuBloom] = views.Gpu.Bloom;
		sample[EFrameStat::GpuPicture] = views.Gpu.Picture;
		sample[EFrameStat::GpuColorGrading] = views.Gpu.ColorGrading;

		sample[EFrameStat::SceneViews] = render.SceneViews;
		sample[EFrameStat::MsaaSamples] = render.MsaaSamples;
		sample[EFrameStat::DrawCalls] = views.Scene.DrawCalls;
		sample[EFrameStat::InstancedDrawCalls] = views.Scene.InstancedDrawCalls;
		sample[EFrameStat::Instances] = views.Scene.Instances;
		sample[EFrameStat::InstancesCulled] = views.Scene.InstancesCulled;
		sample[EFrameStat::Triangles] = static_cast<double>(views.Scene.Triangles);
		sample[EFrameStat::ShadowCascades] = views.ShadowCascades;
		sample[EFrameStat::ShadowDrawCalls] = views.Shadows.DrawCalls;
		sample[EFrameStat::ShadowTriangles] = static_cast<double>(views.Shadows.Triangles);
		sample[EFrameStat::Decals] = views.Decals;
		sample[EFrameStat::PointLights] = views.Scene.PointLights - std::min(views.Scene.PointLights, views.Scene.PointLightsCulled + views.Scene.PointLightsDropped);
		sample[EFrameStat::ParticleEmitters] = views.Scene.ParticleEmitters - std::min(views.Scene.ParticleEmitters, views.Scene.ParticleEmittersCulled);
		sample[EFrameStat::Particles] = views.Scene.Particles;
		sample[EFrameStat::LightningBolts] = views.Scene.Lightning - std::min(views.Scene.Lightning, views.Scene.LightningCulled);
		return sample;
	}

	FrameStatsWindow::FrameStatsWindow(double windowMilliseconds)
		: WindowMilliseconds(windowMilliseconds)
	{
	}

	void FrameStatsWindow::Add(const FrameStatsSample& sample)
	{
		const size_t slot = static_cast<size_t>(FrameCount % HistorySize);
		HistoryFrameTimes[slot] = static_cast<float>(sample[EFrameStat::FrameTime]);
		HistoryGpuTimes[slot] = static_cast<float>(sample[EFrameStat::GpuFrame]);
		Last = sample;
		++FrameCount;

		for (size_t stat = 0; stat < FrameStatCount; ++stat)
		{
			Sum.Values[stat] += sample.Values[stat];
			Highest.Values[stat] = Frames == 0 ? sample.Values[stat] : std::max(Highest.Values[stat], sample.Values[stat]);
		}
		++Frames;
		Elapsed += sample[EFrameStat::FrameTime];
		if (Elapsed < WindowMilliseconds)
		{
			return;
		}

		// The window closes: its average and worst are out, the next one starts empty
		for (size_t stat = 0; stat < FrameStatCount; ++stat)
		{
			Average.Values[stat] = Sum.Values[stat] / Frames;
		}
		Average.Frame = sample.Frame;
		Worst = Highest;
		Worst.Frame = sample.Frame;
		WindowFrames = Frames;

		Sum = FrameStatsSample{};
		Highest = FrameStatsSample{};
		Frames = 0;
		Elapsed = 0.0;
	}

	size_t FrameStatsWindow::GetHistoryCount() const
	{
		return static_cast<size_t>(std::min<uint64_t>(FrameCount, HistorySize));
	}

	float FrameStatsWindow::GetHistoryFrameTime(size_t index) const
	{
		return HistoryFrameTimes[static_cast<size_t>((FrameCount - GetHistoryCount() + index) % HistorySize)];
	}

	float FrameStatsWindow::GetHistoryGpuTime(size_t index) const
	{
		return HistoryGpuTimes[static_cast<size_t>((FrameCount - GetHistoryCount() + index) % HistorySize)];
	}
}
