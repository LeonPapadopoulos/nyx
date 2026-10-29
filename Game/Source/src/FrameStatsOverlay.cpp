#include "FrameStatsOverlay.h"

#include "FrameStats.h"

#include <imgui.h>

#include <algorithm>
#include <cstdio>

namespace Nyx::Game
{
	namespace
	{
		// The graph: as many of the last frames as fit in this much time, as high as two frames at
		// 30 fps or the slowest frame in it
		constexpr float GraphMilliseconds = 5000.0f;
		constexpr float GraphLowestTop = 1000.0f / 30.0f;
		const ImVec2 GraphSize(320.0f, 64.0f);

		const ImU32 CpuColor = IM_COL32(235, 235, 235, 255);
		const ImU32 GpuColor = IM_COL32(255, 170, 60, 255);
		const ImU32 GuideColor = IM_COL32(255, 255, 255, 48);

		// A count shortened: 950, 38.0 k, 1.24 M
		const char* FormatCount(double count, char (&text)[32])
		{
			if (count >= 1.0e6)
			{
				std::snprintf(text, sizeof(text), "%.2f M", count / 1.0e6);
			}
			else if (count >= 1.0e4)
			{
				std::snprintf(text, sizeof(text), "%.1f k", count / 1.0e3);
			}
			else
			{
				std::snprintf(text, sizeof(text), "%.0f", count);
			}
			return text;
		}

		// The last frames' times as a line, newest on the right, each frame as wide as it took
		void DrawTimeLine(ImDrawList& drawList, const Nyx::FrameStatsWindow& stats, size_t frames, bool bGpu, const ImVec2& min, float top, ImU32 color)
		{
			const size_t count = stats.GetHistoryCount();
			float elapsed = 0.0f;
			for (size_t back = 0; back < frames; ++back)
			{
				const size_t index = count - 1 - back;
				const float milliseconds = bGpu ? stats.GetHistoryGpuTime(index) : stats.GetHistoryFrameTime(index);
				const float x = min.x + GraphSize.x * (1.0f - elapsed / GraphMilliseconds);
				const float y = min.y + GraphSize.y * (1.0f - std::min(milliseconds / top, 1.0f));
				drawList.PathLineTo(ImVec2(x, y));
				elapsed += stats.GetHistoryFrameTime(index);
			}
			drawList.PathStroke(color, ImDrawFlags_None, 1.0f);
		}

		void DrawGraph(const Nyx::FrameStatsWindow& stats, bool bGpu)
		{
			// The frames that fit, and the slowest of them
			const size_t count = stats.GetHistoryCount();
			size_t frames = 0;
			float elapsed = 0.0f;
			float slowest = 0.0f;
			while (frames < count && elapsed < GraphMilliseconds)
			{
				const size_t index = count - 1 - frames;
				slowest = std::max({ slowest, stats.GetHistoryFrameTime(index), bGpu ? stats.GetHistoryGpuTime(index) : 0.0f });
				elapsed += stats.GetHistoryFrameTime(index);
				++frames;
			}
			const float top = std::max(GraphLowestTop, slowest * 1.1f);

			const ImVec2 min = ImGui::GetCursorScreenPos();
			const ImVec2 max(min.x + GraphSize.x, min.y + GraphSize.y);
			ImGui::Dummy(GraphSize);
			ImDrawList& drawList = *ImGui::GetWindowDrawList();
			drawList.AddRectFilled(min, max, IM_COL32(0, 0, 0, 90));
			drawList.PushClipRect(min, max, true);
			// 60 and 30 frames a second
			for (const float guide : { 1000.0f / 60.0f, 1000.0f / 30.0f })
			{
				const float y = min.y + GraphSize.y * (1.0f - guide / top);
				drawList.AddLine(ImVec2(min.x, y), ImVec2(max.x, y), GuideColor);
			}
			if (frames > 1)
			{
				if (bGpu)
				{
					DrawTimeLine(drawList, stats, frames, true, min, top, GpuColor);
				}
				DrawTimeLine(drawList, stats, frames, false, min, top, CpuColor);
			}
			drawList.PopClipRect();

			char scale[32];
			std::snprintf(scale, sizeof(scale), "%.0f ms", top);
			drawList.AddText(ImVec2(min.x + 4.0f, min.y + 2.0f), GuideColor, scale);
		}
	}

	void DrawFrameStatsOverlay(const Nyx::FrameStatsWindow& stats, const glm::vec2& topLeft)
	{
		using Nyx::EFrameStat;

		const ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(ImVec2(topLeft.x, topLeft.y));
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::SetNextWindowBgAlpha(0.7f);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings |
			ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoMove;
		if (!ImGui::Begin("Frame stats (F8)", nullptr, flags))
		{
			ImGui::End();
			return;
		}

		// The last second's average, or the last frame's until a second has passed
		const bool bWindow = stats.GetWindowFrames() > 0;
		const Nyx::FrameStatsSample& shown = bWindow ? stats.GetAverage() : stats.GetLast();
		const Nyx::FrameStatsSample& worst = bWindow ? stats.GetWorst() : stats.GetLast();
		const double frameTime = shown[EFrameStat::FrameTime];
		ImGui::Text("%.0f fps  %.2f ms  worst %.2f ms", frameTime > 0.0 ? 1000.0 / frameTime : 0.0, frameTime, worst[EFrameStat::FrameTime]);

		ImGui::Text("CPU  update %.2f  simulation %.2f (%.1f steps)", shown[EFrameStat::Update], shown[EFrameStat::Simulation], shown[EFrameStat::SimulationSteps]);
		ImGui::Text("     extract %.2f  cull %.2f  record %.2f  UI %.2f  wait for GPU %.2f", shown[EFrameStat::Extract], shown[EFrameStat::Cull],
			shown[EFrameStat::Record], shown[EFrameStat::UI], shown[EFrameStat::WaitForGpu]);

		const bool bGpu = shown[EFrameStat::GpuFrame] > 0.0;
		if (bGpu)
		{
			ImGui::Text("GPU  %.2f ms  worst %.2f", shown[EFrameStat::GpuFrame], worst[EFrameStat::GpuFrame]);
			ImGui::Text("     sky %.2f  clouds %.2f  volume %.2f  shadows %.2f  scene %.2f  occlusion %.2f  lightning %.2f  particles %.2f  shafts %.2f  bloom %.2f  picture %.2f  grading %.2f", shown[EFrameStat::GpuSky],
				shown[EFrameStat::GpuClouds], shown[EFrameStat::GpuVolumetricClouds], shown[EFrameStat::GpuShadows], shown[EFrameStat::GpuScene], shown[EFrameStat::GpuAmbientOcclusion], shown[EFrameStat::GpuLightning], shown[EFrameStat::GpuParticles], shown[EFrameStat::GpuLightShafts],
				shown[EFrameStat::GpuBloom], shown[EFrameStat::GpuPicture], shown[EFrameStat::GpuColorGrading]);
		}
		else
		{
			ImGui::TextUnformatted("GPU  no timestamps on this device");
		}

		char draws[32], instanced[32], instances[32], culled[32], triangles[32];
		ImGui::Text("%.0f %s at %.0fx MSAA  draws %s (%s instanced)  instances %s (%s culled)  triangles %s", shown[EFrameStat::SceneViews],
			shown[EFrameStat::SceneViews] == 1.0 ? "view" : "views", shown[EFrameStat::MsaaSamples], FormatCount(shown[EFrameStat::DrawCalls], draws),
			FormatCount(shown[EFrameStat::InstancedDrawCalls], instanced), FormatCount(shown[EFrameStat::Instances], instances),
			FormatCount(shown[EFrameStat::InstancesCulled], culled), FormatCount(shown[EFrameStat::Triangles], triangles));
		char shadowDraws[32], shadowTriangles[32], decals[32];
		ImGui::Text("Shadows %.0f cascades, %s draws, %s triangles  decals %s", shown[EFrameStat::ShadowCascades],
			FormatCount(shown[EFrameStat::ShadowDrawCalls], shadowDraws), FormatCount(shown[EFrameStat::ShadowTriangles], shadowTriangles),
			FormatCount(shown[EFrameStat::Decals], decals));
		char emitters[32], particles[32];
		ImGui::Text("Point lights %.0f  particles %s from %s emitters", shown[EFrameStat::PointLights], FormatCount(shown[EFrameStat::Particles], particles),
			FormatCount(shown[EFrameStat::ParticleEmitters], emitters));

		DrawGraph(stats, bGpu);
		ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(CpuColor), "CPU frame");
		if (bGpu)
		{
			ImGui::SameLine();
			ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(GpuColor), "GPU");
		}
		ImGui::SameLine();
		ImGui::TextDisabled("last %.0f s, lines at 60 and 30 fps", GraphMilliseconds / 1000.0f);

		ImGui::End();
	}
}
