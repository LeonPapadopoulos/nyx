#include "TerrainPlanWindow.h"

#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "ImageFile.h"
#include "NameComponent.h"
#include "SceneEditor.h"
#include "SdfTerrainComponent.h"
#include "TerrainPlanEdits.h"
#include "TerrainWorldPlan.h"
#include "TransformComponent.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <random>
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
		// Another world of the same plan
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("<")) && values.Seed > 0)
		{
			SetTerrainSeed(editing, *terrain, values.Seed - 1);
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button(">")))
		{
			SetTerrainSeed(editing, *terrain, values.Seed + 1);
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Roll")))
		{
			std::random_device random;
			SetTerrainSeed(editing, *terrain, random());
		}
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
		ImGui::SameLine();
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
		if (NYX_UI(Nyx::UI::BeginCombo("New pin", LandformName(NewPinKind))))
		{
			for (const Nyx::ESdfTerrainLandform kind : { Nyx::ESdfTerrainLandform::Plains, Nyx::ESdfTerrainLandform::Roughland, Nyx::ESdfTerrainLandform::Sea,
					 Nyx::ESdfTerrainLandform::Tunnels, Nyx::ESdfTerrainLandform::Rift })
			{
				if (NYX_UI(Nyx::UI::Selectable(LandformName(kind), kind == NewPinKind)))
				{
					NewPinKind = kind;
				}
			}
			Nyx::UI::EndCombo();
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

		// Steering: a pin dragged by its middle or ring, the plan made again when the mouse lets go;
		// a double-click adds one; a right-click, not a drag, opens a pin's menu
		const std::vector<Nyx::SdfTerrainPlanPin>& pins = values.Plan.Pins;
		const glm::vec2 under = toPlan(io.MousePos);
		const float tolerance = 8.0f / PixelsPerMetre;
		// A rift's path being extended, until Escape, a right-click or its pin is gone
		if (Extending && (*Extending >= pins.size() || pins[*Extending].Landform != Nyx::ESdfTerrainLandform::Rift || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
							 (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))))
		{
			Extending.reset();
		}
		if (Extending && bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			std::vector<Nyx::SdfTerrainPlanPin> extended = pins;
			extended[*Extending].Path.push_back(under);
			SetTerrainPlanPins(editing, *terrain, extended, "Extend Rift Path");
			Dragged.reset();
		}
		else if (bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !FindTerrainPinAt(pins, under, tolerance))
		{
			std::vector<Nyx::SdfTerrainPlanPin> added = pins;
			Nyx::SdfTerrainPlanPin pin;
			pin.Landform = NewPinKind;
			pin.Position = under;
			pin.Radius = NewPinKind == Nyx::ESdfTerrainLandform::Rift ? 60.0f : 250.0f;
			added.push_back(pin);
			SetTerrainPlanPins(editing, *terrain, added, "Add Plan Pin");
			Dragged.reset();
		}
		else if (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			if (const std::optional<TerrainPinHit> hit = FindTerrainPinAt(pins, under, tolerance))
			{
				Dragged = hit->Index;
				bDraggingRing = hit->bRing;
				DraggedPlace = hit->Place;
				DraggedPin = pins[hit->Index];
			}
		}
		if (Dragged && *Dragged >= pins.size())
		{
			Dragged.reset();
		}
		if (Dragged && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (DraggedPlace >= 0 && size_t(DraggedPlace) < DraggedPin.Path.size())
			{
				DraggedPin.Path[size_t(DraggedPlace)] += glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / PixelsPerMetre;
			}
			else if (bDraggingRing)
			{
				DraggedPin.Radius = std::max(glm::length(under - DraggedPin.Position), 10.0f);
			}
			else
			{
				DraggedPin.Position += glm::vec2(io.MouseDelta.x, io.MouseDelta.y) / PixelsPerMetre;
			}
		}
		else if (Dragged)
		{
			std::vector<Nyx::SdfTerrainPlanPin> moved = pins;
			moved[*Dragged] = DraggedPin;
			SetTerrainPlanPins(editing, *terrain, moved, DraggedPlace >= 0 ? "Move Rift Path Place" : (bDraggingRing ? "Size Plan Pin" : "Move Plan Pin"));
			Dragged.reset();
			DraggedPlace = -1;
		}
		if (bHovered && ImGui::IsMouseReleased(ImGuiMouseButton_Right) && ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f).x == 0.0f &&
			ImGui::GetMouseDragDelta(ImGuiMouseButton_Right, 0.0f).y == 0.0f)
		{
			if (const std::optional<TerrainPinHit> hit = FindTerrainPinAt(pins, under, tolerance))
			{
				MenuPin = hit->Index;
				MenuPlace = hit->Place;
				ImGui::OpenPopup("##PlanPin");
			}
		}
		if (ImGui::BeginPopup("##PlanPin"))
		{
			if (MenuPin && *MenuPin < pins.size() && MenuPlace >= 0 && size_t(MenuPlace) < pins[*MenuPin].Path.size())
			{
				if (NYX_UI(Nyx::UI::Selectable("Remove Place")))
				{
					std::vector<Nyx::SdfTerrainPlanPin> removed = pins;
					removed[*MenuPin].Path.erase(removed[*MenuPin].Path.begin() + MenuPlace);
					SetTerrainPlanPins(editing, *terrain, removed, "Remove Rift Path Place");
					MenuPin.reset();
				}
			}
			else if (MenuPin && *MenuPin < pins.size())
			{
				if (pins[*MenuPin].Landform == Nyx::ESdfTerrainLandform::Rift)
				{
					if (NYX_UI(Nyx::UI::Selectable("Extend Path")))
					{
						Extending = *MenuPin;
					}
					if (!pins[*MenuPin].Path.empty() && NYX_UI(Nyx::UI::Selectable("Clear Path")))
					{
						std::vector<Nyx::SdfTerrainPlanPin> cleared = pins;
						cleared[*MenuPin].Path.clear();
						SetTerrainPlanPins(editing, *terrain, cleared, "Clear Rift Path");
					}
					Nyx::UI::Separator();
				}
				for (const Nyx::ESdfTerrainLandform kind : { Nyx::ESdfTerrainLandform::Plains, Nyx::ESdfTerrainLandform::Roughland, Nyx::ESdfTerrainLandform::Sea,
					 Nyx::ESdfTerrainLandform::Tunnels, Nyx::ESdfTerrainLandform::Rift })
				{
					if (NYX_UI(Nyx::UI::Selectable(LandformName(kind), pins[*MenuPin].Landform == kind)))
					{
						std::vector<Nyx::SdfTerrainPlanPin> changed = pins;
						changed[*MenuPin].Landform = kind;
						SetTerrainPlanPins(editing, *terrain, changed, "Change Plan Pin");
					}
				}
				Nyx::UI::Separator();
				if (NYX_UI(Nyx::UI::Selectable("Remove")))
				{
					std::vector<Nyx::SdfTerrainPlanPin> removed = pins;
					removed.erase(removed.begin() + std::ptrdiff_t(*MenuPin));
					SetTerrainPlanPins(editing, *terrain, removed, "Remove Plan Pin");
					MenuPin.reset();
				}
			}
			ImGui::EndPopup();
		}

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		drawList->PushClipRect(origin, ImVec2(origin.x + size.x, origin.y + size.y), true);
		drawList->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(22, 24, 28, 255));
		drawList->AddImage(ImTextureRef(renderer.GetUIImageTextureId(Image)), toScreen(planMin), toScreen(planMin + glm::vec2(across)));

		// The pins: a ring as far as each steers, its kind by its colour
		for (size_t index = 0; index < values.Plan.Pins.size(); ++index)
		{
			const Nyx::SdfTerrainPlanPin& pin = Dragged && *Dragged == index ? DraggedPin : values.Plan.Pins[index];
			const ImVec2 at = toScreen(pin.Position);
			const ImU32 colour = PinColour(pin.Landform);
			// A rift's path: a line through its places, each a square
			ImVec2 from = at;
			for (const glm::vec2& place : pin.Path)
			{
				const ImVec2 to = toScreen(place);
				drawList->AddLine(from, to, colour, Extending && *Extending == index ? 3.0f : 2.0f);
				drawList->AddRectFilled(ImVec2(to.x - 4.0f, to.y - 4.0f), ImVec2(to.x + 4.0f, to.y + 4.0f), colour);
				from = to;
			}
			if (Extending && *Extending == index && bHovered)
			{
				drawList->AddLine(from, io.MousePos, colour, 1.0f);
			}
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
			const Nyx::TerrainPlanReadout readout = Nyx::ReadTerrainWorldPlan(*plan, under);
			const std::string line = readout.bLand
				? std::format("x {:.0f}, z {:.0f}: {}, ground {:.0f} m, slope {:.0f} degrees, tunnels {:.1f}, a rift's floor {:.0f} m off", under.x, under.y,
					  LandformName(readout.Landform), readout.Height, readout.SlopeDegrees, readout.Tunnels, readout.RiftDistance)
				: std::format("x {:.0f}, z {:.0f}: off the landmass", under.x, under.y);
			NYX_UI(ImGui::TextUnformatted(line.c_str()));
		}
		else
		{
			NYX_UI(ImGui::TextDisabled("The wheel zooms, the right or middle button drags; a double-click adds a pin, its middle moves it, its ring sizes it"));
		}
		if (Extending)
		{
			NYX_UI(ImGui::TextDisabled("Double-clicks add places to the rift's path; Escape or a right-click ends"));
		}
		ImGui::End();
	}
}
