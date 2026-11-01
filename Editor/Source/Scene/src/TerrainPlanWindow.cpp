#include "TerrainPlanWindow.h"

#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "ImageFile.h"
#include "NameComponent.h"
#include "SceneEditor.h"
#include "SdfTerrainComponent.h"
#include "TerrainPlanEdits.h"
#include "TerrainPlacement.h"
#include "TerrainTraversal.h"
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
		constexpr std::array<const char*, 6> ToolNames = { "Pins", "Paint", "Raise", "Lower", "Cut", "Heap" };

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

		// A Place Rule's diamonds, by its number
		ImU32 RuleColour(uint32_t rule)
		{
			static constexpr std::array<ImU32, 6> Colours = { IM_COL32(255, 220, 80, 255), IM_COL32(80, 230, 230, 255), IM_COL32(255, 120, 200, 255),
				IM_COL32(255, 160, 60, 255), IM_COL32(170, 255, 120, 255), IM_COL32(200, 170, 255, 255) };
			return Colours[rule % Colours.size()];
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
		NYX_UI(Nyx::UI::Checkbox("Places", &bShowPlaces));
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

		// The tool, and its brush
		ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
		if (NYX_UI(Nyx::UI::BeginCombo("Tool", ToolNames[size_t(Tool)])))
		{
			for (size_t tool = 0; tool < ToolNames.size(); ++tool)
			{
				if (NYX_UI(Nyx::UI::Selectable(ToolNames[tool], size_t(Tool) == tool)))
				{
					Tool = ETerrainPlanTool(tool);
					Stroke.clear();
				}
			}
			Nyx::UI::EndCombo();
		}
		if (Tool != ETerrainPlanTool::Pins)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			NYX_UI(ImGui::DragFloat("Brush", &BrushRadius, 1.0f, 4.0f, 1000.0f, "%.0f m"));
			BrushRadius = std::clamp(BrushRadius, 4.0f, 1000.0f);
		}
		if (Tool == ETerrainPlanTool::Paint)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
			if (NYX_UI(Nyx::UI::BeginCombo("Kind", LandformName(BrushLandform))))
			{
				for (const Nyx::ESdfTerrainLandform kind : { Nyx::ESdfTerrainLandform::Plains, Nyx::ESdfTerrainLandform::Roughland, Nyx::ESdfTerrainLandform::Sea,
						 Nyx::ESdfTerrainLandform::Tunnels })
				{
					if (NYX_UI(Nyx::UI::Selectable(LandformName(kind), kind == BrushLandform)))
					{
						BrushLandform = kind;
					}
				}
				Nyx::UI::EndCombo();
			}
		}
		if (Tool == ETerrainPlanTool::Raise || Tool == ETerrainPlanTool::Lower)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0f);
			NYX_UI(ImGui::DragFloat("By", &BrushHeight, 0.5f, 0.5f, 500.0f, "%.1f m"));
			BrushHeight = std::clamp(BrushHeight, 0.5f, 500.0f);
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
		const bool bPinTool = Tool == ETerrainPlanTool::Pins;
		// A rift's path being extended, until Escape, a right-click or its pin is gone
		if (Extending && (*Extending >= pins.size() || pins[*Extending].Landform != Nyx::ESdfTerrainLandform::Rift || ImGui::IsKeyPressed(ImGuiKey_Escape) ||
							 (bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Right))))
		{
			Extending.reset();
		}
		if (Extending && bPinTool && bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			std::vector<Nyx::SdfTerrainPlanPin> extended = pins;
			extended[*Extending].Path.push_back(under);
			SetTerrainPlanPins(editing, *terrain, extended, "Extend Rift Path");
			Dragged.reset();
		}
		else if (bPinTool && bHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && !FindTerrainPinAt(pins, under, tolerance))
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
		else if (bPinTool && bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
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
		// A stroke of the other tools: a point every so far while the left button is down, made into an
		// override when it lets go
		if (!bPinTool && bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			Stroke = { under };
		}
		const bool bStamps = Tool == ETerrainPlanTool::Cut || Tool == ETerrainPlanTool::Heap;
		if (!bPinTool && !Stroke.empty() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			if (glm::length(under - Stroke.back()) >= BrushRadius * (bStamps ? 1.2f : 0.4f))
			{
				Stroke.push_back(under);
			}
		}
		else if (!bPinTool && !Stroke.empty())
		{
			Nyx::SdfTerrainPlanOverride drawn;
			drawn.Points = Stroke;
			drawn.Radius = BrushRadius;
			switch (Tool)
			{
			case ETerrainPlanTool::Paint:
				drawn.Kind = Nyx::ESdfTerrainOverrideKind::Landform;
				drawn.Landform = BrushLandform;
				break;
			case ETerrainPlanTool::Raise:
			case ETerrainPlanTool::Lower:
				drawn.Kind = Nyx::ESdfTerrainOverrideKind::Height;
				drawn.Height = Tool == ETerrainPlanTool::Raise ? BrushHeight : -BrushHeight;
				break;
			default:
				drawn.Kind = Nyx::ESdfTerrainOverrideKind::Stamp;
				drawn.bCarve = Tool == ETerrainPlanTool::Cut;
				drawn.Height = drawn.bCarve ? 0.0f : -BrushRadius * 0.5f;
				break;
			}
			std::vector<Nyx::SdfTerrainPlanOverride> overrides = values.Plan.Overrides;
			overrides.push_back(AnchorTerrainPlanOverride(pins, std::move(drawn)));
			SetTerrainPlanOverrides(editing, *terrain, overrides, Tool == ETerrainPlanTool::Paint ? "Paint Plan" : (bStamps ? "Stamp Plan" : "Raise Plan"));
			Stroke.clear();
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
			else if (const std::optional<size_t> drawnAt = FindTerrainPlanOverrideAt(values.Plan, under, tolerance))
			{
				MenuOverride = drawnAt;
				ImGui::OpenPopup("##PlanOverride");
			}
		}
		if (ImGui::BeginPopup("##PlanOverride"))
		{
			if (MenuOverride && *MenuOverride < values.Plan.Overrides.size() && NYX_UI(Nyx::UI::Selectable("Remove")))
			{
				std::vector<Nyx::SdfTerrainPlanOverride> removed = values.Plan.Overrides;
				removed.erase(removed.begin() + std::ptrdiff_t(*MenuOverride));
				SetTerrainPlanOverrides(editing, *terrain, removed, "Remove Plan Override");
				MenuOverride.reset();
			}
			ImGui::EndPopup();
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

		// The hand overrides: a stroke as wide as it reaches, a stamp as a circle; the one being drawn
		auto drawStroke = [&](const std::vector<glm::vec2>& points, float radius, ImU32 colour, bool bStampsOf)
		{
			for (size_t n = 0; n < points.size(); ++n)
			{
				const ImVec2 at = toScreen(points[n]);
				if (bStampsOf)
				{
					drawList->AddCircle(at, std::max(radius * PixelsPerMetre, 3.0f), colour, 32, 2.0f);
					continue;
				}
				drawList->AddCircleFilled(at, std::max(radius * PixelsPerMetre, 2.0f), (colour & 0x00FFFFFFu) | 0x30000000u, 24);
				if (n + 1 < points.size())
				{
					drawList->AddLine(at, toScreen(points[n + 1]), colour, 2.0f);
				}
			}
		};
		for (const Nyx::SdfTerrainPlanOverride& override : values.Plan.Overrides)
		{
			if (!override.bEnabled)
			{
				continue;
			}
			const bool bStampOverride = override.Kind == Nyx::ESdfTerrainOverrideKind::Stamp;
			const ImU32 colour = override.Kind == Nyx::ESdfTerrainOverrideKind::Landform ? PinColour(override.Landform)
				: bStampOverride ? (override.bCarve ? IM_COL32(255, 90, 90, 255) : IM_COL32(230, 200, 120, 255))
								 : (override.Height >= 0.0f ? IM_COL32(255, 255, 255, 255) : IM_COL32(60, 60, 70, 255));
			drawStroke(Nyx::GetTerrainPlanOverridePoints(values.Plan, override), override.Radius, colour, bStampOverride);
		}
		if (!Stroke.empty())
		{
			drawStroke(Stroke, BrushRadius, IM_COL32(255, 255, 255, 200), Tool == ETerrainPlanTool::Cut || Tool == ETerrainPlanTool::Heap);
		}
		if (Tool != ETerrainPlanTool::Pins && bHovered)
		{
			drawList->AddCircle(io.MousePos, BrushRadius * PixelsPerMetre, IM_COL32(255, 255, 255, 120), 48, 1.0f);
		}

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

		// The places the rules found: a diamond each, hollow from a Fallback; their footprints once
		// they are a few pixels across; what is under the mouse named
		const Nyx::TerrainPlacesComponent* places = world.Has<Nyx::TerrainPlacesComponent>(*terrain) ? &world.Get<Nyx::TerrainPlacesComponent>(*terrain) : nullptr;
		const Nyx::TerrainPlace* hoveredPlace = nullptr;
		const bool bPlaces = bShowPlaces && places && places->Placement;
		for (size_t index = 0; bPlaces && bHovered && index < places->Placement->Places.size(); ++index)
		{
			const Nyx::TerrainPlace& place = places->Placement->Places[index];
			const ImVec2 at = toScreen(glm::vec2(place.Position.x, place.Position.z));
			if (std::fabs(io.MousePos.x - at.x) < 6.0f && std::fabs(io.MousePos.y - at.y) < 6.0f)
			{
				hoveredPlace = &place;
			}
		}

		// What can be reached from a place clicked: a pixel a column, by its highest floor
		if (hoveredPlace && bHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !Dragged)
		{
			ReachFrom = size_t(hoveredPlace - places->Placement->Places.data());
		}
		const Nyx::TerrainTraversalComponent* traversal = world.Has<Nyx::TerrainTraversalComponent>(*terrain) ? &world.Get<Nyx::TerrainTraversalComponent>(*terrain) : nullptr;
		const bool bReach = ReachFrom && places && places->Placement && *ReachFrom < places->Placement->Places.size() && traversal && traversal->Traversal;
		if (bReach && (traversal->Traversal != ReachDrawn || *ReachFrom != ReachDrawnFrom || ReachImage == 0))
		{
			const Nyx::TerrainTraversal& graph = *traversal->Traversal;
			const std::optional<uint32_t> start = Nyx::FindTerrainFloor(graph, places->Placement->Places[*ReachFrom].Position);
			const std::vector<uint8_t> reach = start ? Nyx::GetTerrainReach(graph, *start) : std::vector<uint8_t>(graph.Floors(), 0);
			static constexpr std::array<std::array<uint8_t, 4>, 5> Colours = { { { 230, 60, 60, 150 }, { 90, 220, 90, 110 }, { 80, 150, 255, 140 }, { 255, 225, 70, 150 },
				{ 255, 140, 40, 160 } } };
			Nyx::RgbaImage picture;
			picture.Width = uint32_t(graph.Columns);
			picture.Height = uint32_t(graph.Columns);
			picture.Pixels.assign(size_t(graph.Columns) * size_t(graph.Columns) * 4, 0);
			for (size_t column = 0; column + 1 < graph.ColumnFirst.size(); ++column)
			{
				if (graph.ColumnFirst[column] == graph.ColumnFirst[column + 1])
				{
					continue;
				}
				const std::array<uint8_t, 4>& colour = Colours[std::min<size_t>(reach[graph.ColumnFirst[column]], Colours.size() - 1)];
				std::copy(colour.begin(), colour.end(), picture.Pixels.begin() + std::ptrdiff_t(column * 4));
			}
			if (ReachImage != 0)
			{
				renderer.DestroyUIImage(ReachImage);
			}
			ReachImage = renderer.CreateUIImage(picture);
			ReachDrawn = traversal->Traversal;
			ReachDrawnFrom = *ReachFrom;
		}
		if (bReach && ReachImage != 0)
		{
			const Nyx::TerrainTraversal& graph = *ReachDrawn;
			const glm::vec2 half(0.5f * graph.Values.Spacing);
			const glm::vec2 far = graph.Origin + glm::vec2(float(graph.Columns) * graph.Values.Spacing) - half;
			drawList->AddImage(ImTextureRef(renderer.GetUIImageTextureId(ReachImage)), toScreen(graph.Origin - half), toScreen(far));
		}

		if (bPlaces)
		{
			for (const Nyx::TerrainPlace& place : places->Placement->Places)
			{
				const ImVec2 at = toScreen(glm::vec2(place.Position.x, place.Position.z));
				const ImU32 colour = RuleColour(place.Rule);
				const float radius = place.Rule < values.PlaceRules.size() ? values.PlaceRules[place.Rule].Radius * PixelsPerMetre : 0.0f;
				if (radius > 4.0f)
				{
					drawList->AddCircle(at, radius, colour, 32, 1.0f);
				}
				const ImVec2 diamond[4] = { ImVec2(at.x, at.y - 5.0f), ImVec2(at.x + 5.0f, at.y), ImVec2(at.x, at.y + 5.0f), ImVec2(at.x - 5.0f, at.y) };
				if (place.bFallback)
				{
					drawList->AddPolyline(diamond, 4, colour, ImDrawFlags_Closed, 1.5f);
				}
				else
				{
					drawList->AddConvexPolyFilled(diamond, 4, colour);
				}
			}
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
		if (hoveredPlace)
		{
			const std::string rule = hoveredPlace->Rule < values.PlaceRules.size() ? values.PlaceRules[hoveredPlace->Rule].Name : std::string("?");
			const std::string line = std::format("{}{}: x {:.0f}, z {:.0f}, ground {:.1f} m", rule, hoveredPlace->bFallback ? " (its Fallback)" : "",
				hoveredPlace->Position.x, hoveredPlace->Position.z, hoveredPlace->Position.y);
			NYX_UI(ImGui::TextUnformatted(line.c_str()));
		}
		else if (bHovered)
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
		if (ReachFrom)
		{
			if (!traversal || !traversal->Traversal)
			{
				NYX_UI(ImGui::TextDisabled("Reach: the terrain needs a Route Check for its traversal graph"));
			}
			else
			{
				NYX_UI(ImGui::TextDisabled("Reach from the place clicked: walking green, swimming blue, climbing yellow, jumping orange, nothing red"));
				ImGui::SameLine();
				if (NYX_UI(Nyx::UI::Button("Hide Reach")))
				{
					ReachFrom.reset();
				}
			}
		}
		ImGui::End();
	}
}
