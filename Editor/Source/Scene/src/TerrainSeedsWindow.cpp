#include "TerrainSeedsWindow.h"

#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "ImageFile.h"
#include "JobSystem.h"
#include "SceneEditor.h"
#include "SdfTerrainComponent.h"
#include "TerrainPlanEdits.h"
#include "TerrainWorldPlan.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <atomic>
#include <format>
#include <string>

namespace Nyx::Editor
{
	namespace Detail
	{
		// The seeds being made on the workers, each into its own place
		struct TerrainSeedsRun
		{
			Nyx::SdfTerrainWorldPlan Plan;
			uint64_t PlanHash = 0;
			std::vector<Nyx::TerrainPlanSeed> Seeds;
			std::atomic<uint32_t> Remaining{ 0 };
			bool bImagesMade = false;
		};
	}

	namespace
	{
		std::string Percent(float share)
		{
			return std::format("{:.0f}%", share * 100.0f);
		}

		std::string RangeOf(const Nyx::TerrainPlanSpread::Range& range, bool bPercent)
		{
			return bPercent ? std::format("{} ({} to {})", Percent(range.Mean), Percent(range.Least), Percent(range.Most))
							: std::format("{:.1f} ({:.0f} to {:.0f})", range.Mean, range.Least, range.Most);
		}
	}

	void TerrainSeedsWindow::TakeImages(Nyx::IEditorRenderer& renderer)
	{
		for (const uint64_t image : Images)
		{
			renderer.DestroyUIImage(image);
		}
		Images.clear();
		for (const Nyx::TerrainPlanSeed& seed : Run->Seeds)
		{
			const Nyx::TerrainPlanMapImage map = Nyx::DrawTerrainWorldPlanMap(*seed.Plan);
			Nyx::RgbaImage picture;
			picture.Width = map.Width;
			picture.Height = map.Height;
			picture.Pixels = map.Pixels;
			Images.push_back(renderer.CreateUIImage(picture));
		}
		Spread = Nyx::GetTerrainPlanSpread(Run->Seeds);
		Run->bImagesMade = true;
	}

	void TerrainSeedsWindow::Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing)
	{
		if (!bOpen)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(820.0f, 760.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Plan Seeds", &bOpen))
		{
			ImGui::End();
			return;
		}
		const std::optional<Nyx::Entity> terrain = FindPlannedTerrain(editing);
		if (!terrain)
		{
			NYX_UI(ImGui::TextDisabled("Select an SDF terrain with a Plan layer"));
			ImGui::End();
			return;
		}
		const Nyx::SdfTerrainComponent& values = editing.Scene.GetRegistry().Get<Nyx::SdfTerrainComponent>(*terrain);
		const bool bRunning = Run && Run->Remaining.load(std::memory_order_acquire) != 0;

		// What to run: from which seed, how many
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
		NYX_UI(ImGui::InputScalar("First seed", ImGuiDataType_U32, &FirstSeed));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0f);
		if (NYX_UI(Nyx::UI::BeginCombo("Seeds", std::to_string(Count).c_str())))
		{
			for (const uint32_t count : { 16u, 36u, 64u })
			{
				if (NYX_UI(Nyx::UI::Selectable(std::to_string(count).c_str(), count == Count)))
				{
					Count = count;
				}
			}
			Nyx::UI::EndCombo();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Run")) && !bRunning)
		{
			auto run = std::make_shared<Detail::TerrainSeedsRun>();
			run->Plan = values.Plan;
			run->PlanHash = Nyx::HashTerrainWorldPlan(values.Plan);
			run->Seeds.resize(Count);
			run->Remaining.store(Count, std::memory_order_release);
			for (uint32_t index = 0; index < Count; ++index)
			{
				Nyx::JobSystem::Get().Schedule(
					[run, index, seed = FirstSeed + index]
					{
						run->Seeds[index] = Nyx::MakeTerrainPlanSeed(seed, run->Plan);
						run->Remaining.fetch_sub(1, std::memory_order_acq_rel);
					},
					Nyx::EJobPriority::Low);
			}
			Run = run;
		}
		if (!Run)
		{
			NYX_UI(ImGui::TextDisabled("Run makes that many seeds of the terrain's plan, from the first seed"));
			ImGui::End();
			return;
		}
		if (Run->Remaining.load(std::memory_order_acquire) != 0)
		{
			NYX_UI(ImGui::Text("Making %u of %zu", uint32_t(Run->Seeds.size()) - Run->Remaining.load(), Run->Seeds.size()));
			ImGui::End();
			return;
		}
		if (!Run->bImagesMade)
		{
			TakeImages(renderer);
		}

		// How they spread, and whether that is still the plan
		if (Nyx::HashTerrainWorldPlan(values.Plan) != Run->PlanHash)
		{
			NYX_UI(ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f), "The plan has changed since: Run again"));
		}
		NYX_UI(ImGui::Text("Sea %s, rough land %s, rifts %s, tunnels %s", RangeOf(Spread.Sea, true).c_str(), RangeOf(Spread.Roughland, true).c_str(),
			RangeOf(Spread.Rift, true).c_str(), RangeOf(Spread.Tunnels, true).c_str()));
		NYX_UI(ImGui::Text("Rivers %s, lakes %s; a plan made in %.0f ms on average", RangeOf(Spread.Rivers, false).c_str(), RangeOf(Spread.Lakes, false).c_str(),
			Spread.Milliseconds.Mean));
		NYX_UI(ImGui::Separator());

		// The maps, as many to a row as fit; a click gives the terrain that seed
		const float thumbnail = ImGui::GetFontSize() * 9.0f;
		const float cell = thumbnail + ImGui::GetStyle().ItemSpacing.x;
		const int columns = std::max(int(ImGui::GetContentRegionAvail().x / cell), 1);
		ImGui::BeginChild("##SeedGrid");
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		for (size_t index = 0; index < Run->Seeds.size() && index < Images.size(); ++index)
		{
			const Nyx::TerrainPlanSeed& seed = Run->Seeds[index];
			if (index % size_t(columns) != 0)
			{
				ImGui::SameLine();
			}
			ImGui::BeginGroup();
			ImGui::PushID(int(index));
			const ImVec2 corner = ImGui::GetCursorScreenPos();
			if (NYX_UI(ImGui::InvisibleButton("##Seed", ImVec2(thumbnail, thumbnail))))
			{
				SetTerrainSeed(editing, *terrain, seed.Seed);
			}
			const bool bHovered = ImGui::IsItemHovered();
			const ImVec2 far(corner.x + thumbnail, corner.y + thumbnail);
			drawList->AddRectFilled(corner, far, IM_COL32(22, 24, 28, 255));
			drawList->AddImage(ImTextureRef(renderer.GetUIImageTextureId(Images[index])), corner, far);
			if (seed.Seed == values.Seed || bHovered)
			{
				drawList->AddRect(corner, far, seed.Seed == values.Seed ? IM_COL32(255, 210, 90, 255) : IM_COL32(255, 255, 255, 160), 0.0f, 0, 2.0f);
			}
			if (bHovered)
			{
				ImGui::SetTooltip("Seed %u: sea %s, rough land %s, rifts %s, tunnels %s, %zu rivers, %zu lakes; a click gives the terrain this seed", seed.Seed,
					Percent(seed.Shares.Sea).c_str(), Percent(seed.Shares.Roughland).c_str(), Percent(seed.Shares.Rift).c_str(), Percent(seed.Shares.Tunnels).c_str(),
					seed.Shares.Rivers, seed.Shares.Lakes);
			}
			NYX_UI(ImGui::Text("%u", seed.Seed));
			NYX_UI(ImGui::TextDisabled("%s sea, %s rough", Percent(seed.Shares.Sea).c_str(), Percent(seed.Shares.Roughland).c_str()));
			ImGui::PopID();
			ImGui::EndGroup();
		}
		ImGui::EndChild();
		ImGui::End();
	}
}
