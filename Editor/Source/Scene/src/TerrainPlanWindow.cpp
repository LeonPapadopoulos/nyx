#include "TerrainPlanWindow.h"

#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "ImageFile.h"
#include "NameComponent.h"
#include "SceneEditor.h"
#include "SdfTerrainComponent.h"
#include "TerrainWorldPlan.h"
#include "TransformComponent.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <string>

namespace Nyx::Editor
{
	namespace
	{
		constexpr std::array<const char*, 4> OverlayNames = { "Kinds", "Height", "Slope", "Tunnels" };

		const char* LandformName(Nyx::ESdfTerrainLandform landform)
		{
			switch (landform)
			{
			case Nyx::ESdfTerrainLandform::Roughland:
				return "Rough land";
			case Nyx::ESdfTerrainLandform::Rift:
				return "Rift";
			case Nyx::ESdfTerrainLandform::Sea:
				return "Sea";
			case Nyx::ESdfTerrainLandform::Tunnels:
				return "Tunnels";
			default:
				return "Plains";
			}
		}

		// A pin's ring, by what it steers towards
		ImU32 PinColour(Nyx::ESdfTerrainLandform landform)
		{
			switch (landform)
			{
			case Nyx::ESdfTerrainLandform::Roughland:
				return IM_COL32(230, 170, 110, 255);
			case Nyx::ESdfTerrainLandform::Rift:
				return IM_COL32(255, 90, 90, 255);
			case Nyx::ESdfTerrainLandform::Sea:
				return IM_COL32(110, 180, 255, 255);
			case Nyx::ESdfTerrainLandform::Tunnels:
				return IM_COL32(190, 140, 255, 255);
			default:
				return IM_COL32(150, 235, 120, 255);
			}
		}

		// The selected entity if it is a terrain with a Plan layer, else the scene's first
		std::optional<Nyx::Entity> FindPlannedTerrain(SceneEditor& editing)
		{
			Nyx::Registry& world = editing.Scene.GetRegistry();
			const std::optional<Nyx::Entity>& selected = editing.Scene.GetSelection();
			if (selected && world.IsAlive(*selected) && world.Has<Nyx::SdfTerrainComponent>(*selected) &&
				Nyx::UsesTerrainWorldPlan(world.Get<Nyx::SdfTerrainComponent>(*selected)))
			{
				return selected;
			}
			std::optional<Nyx::Entity> first;
			world.Each<Nyx::SdfTerrainComponent>(
				[&](Nyx::Entity entity, Nyx::SdfTerrainComponent& terrain)
				{
					if (!first && Nyx::UsesTerrainWorldPlan(terrain))
					{
						first = entity;
					}
				});
			return first;
		}
	}

	void TerrainPlanWindow::Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing)
	{
		if (!bOpen)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(640.0f, 720.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Plan Map", &bOpen))
		{
			ImGui::End();
			return;
		}
		Nyx::Registry& world = editing.Scene.GetRegistry();
		const std::optional<Nyx::Entity> terrain = FindPlannedTerrain(editing);
		if (!terrain)
		{
			NYX_UI(ImGui::TextDisabled("Select an SDF terrain with a Plan layer"));
			ImGui::End();
			return;
		}
		const Nyx::SdfTerrainComponent& values = world.Get<Nyx::SdfTerrainComponent>(*terrain);
		const std::shared_ptr<const Nyx::TerrainWorldPlan> plan = Nyx::GetTerrainWorldPlan(values);
		if (!plan || plan->Nodes == 0)
		{
			NYX_UI(ImGui::TextDisabled("The plan is being worked out"));
			ImGui::End();
			return;
		}

		// What it shows: the terrain, the overlay, all of it
		const std::string name = world.Has<Nyx::NameComponent>(*terrain) ? world.Get<Nyx::NameComponent>(*terrain).Name : std::string("Terrain");
		NYX_UI(ImGui::Text("%s, seed %u", name.c_str(), values.Seed));
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 7.0f);
		if (NYX_UI(Nyx::UI::BeginCombo("##Overlay", OverlayNames[size_t(Overlay)])))
		{
			for (size_t overlay = 0; overlay < OverlayNames.size(); ++overlay)
			{
				if (NYX_UI(Nyx::UI::Selectable(OverlayNames[overlay], size_t(Overlay) == overlay)))
				{
					Overlay = Nyx::ETerrainPlanMapOverlay(overlay);
				}
			}
			Nyx::UI::EndCombo();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Fit")))
		{
			PixelsPerMetre = 0.0f;
		}

		// Its picture, drawn again when the plan or the overlay changed
		if (plan != Drawn || Overlay != DrawnOverlay || Image == 0)
		{
			if (Image != 0)
			{
				renderer.DestroyUIImage(Image);
			}
			const Nyx::TerrainPlanMapImage map = Nyx::DrawTerrainWorldPlanMap(*plan, Overlay);
			Nyx::RgbaImage picture;
			picture.Width = map.Width;
			picture.Height = map.Height;
			picture.Pixels = map.Pixels;
			Image = renderer.CreateUIImage(picture);
			Drawn = plan;
			DrawnOverlay = Overlay;
		}

		// The map's area, a line left for what is under the mouse
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 avail = ImGui::GetContentRegionAvail();
		const ImVec2 size(std::max(avail.x, 64.0f), std::max(avail.y - ImGui::GetTextLineHeightWithSpacing() * 1.5f, 64.0f));
		NYX_UI(ImGui::InvisibleButton("##PlanMap", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle));
		const bool bHovered = ImGui::IsItemHovered();
		const bool bActive = ImGui::IsItemActive();
		const ImVec2 middle(origin.x + size.x * 0.5f, origin.y + size.y * 0.5f);

		// The plan's square: a pixel a node, each node in the middle of its pixel
		const float across = float(plan->Nodes) * Nyx::TerrainWorldPlan::CellSize;
		const glm::vec2 planMin = plan->Origin - glm::vec2(0.5f * Nyx::TerrainWorldPlan::CellSize);
		if (PixelsPerMetre <= 0.0f)
		{
			PixelsPerMetre = std::min(size.x, size.y) / across;
			Center = planMin + glm::vec2(0.5f * across);
		}
		const ImGuiIO& io = ImGui::GetIO();
		auto toScreen = [&](const glm::vec2& point) { return ImVec2(middle.x + (point.x - Center.x) * PixelsPerMetre, middle.y + (point.y - Center.y) * PixelsPerMetre); };
		auto toPlan = [&](const ImVec2& screen) { return Center + glm::vec2(screen.x - middle.x, screen.y - middle.y) / PixelsPerMetre; };
		// The wheel round the mouse, a drag with the right or middle button
		if (bHovered && io.MouseWheel != 0.0f)
		{
			const glm::vec2 under = toPlan(io.MousePos);
			PixelsPerMetre = std::clamp(PixelsPerMetre * std::pow(1.2f, io.MouseWheel), 0.01f, 8.0f);
			Center = under - glm::vec2(io.MousePos.x - middle.x, io.MousePos.y - middle.y) / PixelsPerMetre;
		}
		if (bActive && (ImGui::IsMouseDragging(ImGuiMouseButton_Right) || ImGui::IsMouseDragging(ImGuiMouseButton_Middle)))
		{
			Center -= glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / PixelsPerMetre;
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(22, 24, 28, 255));
		drawList->AddImage(ImTextureRef(renderer.GetUIImageTextureId(Image)), toScreen(planMin), toScreen(planMin + glm::vec2(across)));

		// The pins: a ring as far as each steers, its kind by its colour
		for (const Nyx::SdfTerrainPlanPin& pin : values.Plan.Pins)
		{
			const ImVec2 at = toScreen(pin.Position);
			const ImU32 colour = PinColour(pin.Landform);
			drawList->AddCircle(at, std::max(pin.Radius * PixelsPerMetre, 3.0f), colour, 48, 2.0f);
			drawList->AddCircleFilled(at, 4.0f, colour);
			drawList->AddText(ImVec2(at.x + 6.0f, at.y - 6.0f), colour, LandformName(pin.Landform));
		}

		// Each scene view's camera, in the terrain's space: where it is and which way it looks
		const glm::mat4 toTerrain = world.Has<Nyx::TransformComponent>(*terrain) ? glm::inverse(world.Get<Nyx::TransformComponent>(*terrain).ToMatrix()) : glm::mat4(1.0f);
		for (const uint64_t view : renderer.GetSceneViewIds())
		{
			Nyx::SceneViewCameraData camera;
			if (!renderer.GetSceneViewCameraData(view, camera))
			{
				continue;
			}
			const glm::vec4 ahead = camera.InverseViewProjection * glm::vec4(0.0f, 0.0f, 0.5f, 1.0f);
			const glm::vec3 position = glm::vec3(toTerrain * glm::vec4(camera.CameraWorldPos, 1.0f));
			const glm::vec3 looks = glm::vec3(toTerrain * glm::vec4(glm::vec3(ahead) / ahead.w - camera.CameraWorldPos, 0.0f));
			glm::vec2 flat(looks.x, looks.z);
			flat = glm::length(flat) > 1e-4f ? glm::normalize(flat) : glm::vec2(0.0f, -1.0f);
			const ImVec2 at = toScreen(glm::vec2(position.x, position.z));
			const float reach = 40.0f;
			const float spread = 0.5f;
			const glm::vec2 left(flat.x * std::cos(spread) - flat.y * std::sin(spread), flat.x * std::sin(spread) + flat.y * std::cos(spread));
			const glm::vec2 right(flat.x * std::cos(-spread) - flat.y * std::sin(-spread), flat.x * std::sin(-spread) + flat.y * std::cos(-spread));
			drawList->AddTriangleFilled(at, ImVec2(at.x + left.x * reach, at.y + left.y * reach), ImVec2(at.x + right.x * reach, at.y + right.y * reach), IM_COL32(255, 255, 255, 70));
			drawList->AddTriangle(at, ImVec2(at.x + left.x * reach, at.y + left.y * reach), ImVec2(at.x + right.x * reach, at.y + right.y * reach), IM_COL32(255, 255, 255, 220), 1.5f);
		}

		// A scale: 100 m, 500 m or 1 km, whichever is near a fifth of the map
		const float metres = std::min(size.x, size.y) * 0.2f / PixelsPerMetre;
		const float bar = metres > 600.0f ? 1000.0f : (metres > 200.0f ? 500.0f : 100.0f);
		const ImVec2 barStart(origin.x + 12.0f, origin.y + size.y - 14.0f);
		drawList->AddLine(barStart, ImVec2(barStart.x + bar * PixelsPerMetre, barStart.y), IM_COL32(255, 255, 255, 220), 2.0f);
		drawList->AddText(ImVec2(barStart.x, barStart.y - ImGui::GetTextLineHeight() - 2.0f), IM_COL32(255, 255, 255, 220), bar >= 1000.0f ? "1 km" : (bar >= 500.0f ? "500 m" : "100 m"));
		drawList->PopClipRect();

		// What is under the mouse
		if (bHovered)
		{
			const glm::vec2 under = toPlan(io.MousePos);
			const Nyx::TerrainPlanReadout readout = Nyx::ReadTerrainWorldPlan(*plan, under);
			const std::string line = readout.bLand
				? std::format("x {:.0f}, z {:.0f}: {}, ground {:.0f} m, slope {:.0f} degrees, tunnels {:.1f}, a rift's floor {:.0f} m off", under.x, under.y,
					  LandformName(readout.Landform), readout.Height, readout.SlopeDegrees, readout.Tunnels, readout.RiftDistance)
				: std::format("x {:.0f}, z {:.0f}: off the landmass", under.x, under.y);
			NYX_UI(ImGui::TextUnformatted(line.c_str()));
		}
		else
		{
			NYX_UI(ImGui::TextDisabled("The wheel zooms, the right or middle button drags"));
		}
		ImGui::End();
	}
}
