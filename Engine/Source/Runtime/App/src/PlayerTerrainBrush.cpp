#include "NyxPCH.h"
#include "PlayerTerrainBrush.h"

#include "CommandQueue.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "SdfTerrainEditCommand.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Nyx
{
	PlayerTerrainBrush::PlayerTerrainBrush(Input::InputSystem& input)
		: BrushInput(input, Input::TerrainBrushInput::Settings{})
	{
		Settings.Radius = 1.5f;
		Settings.Reach = 60.0f;
	}

	void PlayerTerrainBrush::Update(const PlayerFrame& frame, const std::optional<Ray>& aim)
	{
		const Registry& world = frame.World.GetRegistry();
		bool bTerrain = false;
		world.Each<SdfTerrainComponent>([&](Entity, const SdfTerrainComponent&) { bTerrain = true; });
		BrushInput.SetActive(bTerrain, "The world has an SDF terrain (B: the terrain brush)");

		Input::InputUser& user = frame.Input.GetUser();
		const Input::TerrainBrushInput::Actions& actions = BrushInput.GetActions();
		if (bTerrain && user.ConsumeTriggered(actions.Toggle))
		{
			bOn = !bOn;
		}
		bOn = bOn && bTerrain;
		BrushInput.SetStrokeActive(bOn, "The terrain brush is on (B: off)");

		Hit.reset();
		if (!bOn)
		{
			Stroke.reset();
			return;
		}
		if (const float notches = user.Get(actions.Size).Float(); notches != 0.0f)
		{
			Settings.Radius = std::clamp(Settings.Radius * std::pow(SizeStep, notches), MinRadius, MaxRadius);
		}
		if (aim)
		{
			Hit = CastTerrainBrush(world, *aim, Settings.Reach, Stroke ? std::optional<Entity>(Stroke->GetTerrain()) : std::nullopt);
		}

		// A stroke while the button is held, of the mode it began with; with Shift both fire
		const bool bHeap = user.Get(actions.Heap).bTriggered;
		const bool bDig = !bHeap && user.Get(actions.Dig).bTriggered;
		if (!bDig && !bHeap)
		{
			Stroke.reset();
			return;
		}
		if (!Hit || !world.Has<GuidComponent>(Hit->Terrain))
		{
			return;
		}
		if (!Stroke)
		{
			StrokeMode = bDig ? ESdfTerrainEditMode::Subtract : ESdfTerrainEditMode::Add;
			TerrainBrushSettings stroke = Settings;
			stroke.Mode = StrokeMode;
			Stroke.emplace(stroke, Hit->Terrain);
		}
		const EntityGuid terrain = world.Get<GuidComponent>(Hit->Terrain).Guid;
		for (const SdfTerrainEdit& edit : Stroke->MoveTo(Hit->LocalPoint))
		{
			frame.Commands.Add(SdfTerrainEditCommand{ .Terrain = terrain, .Edit = edit });
		}
	}

	void PlayerTerrainBrush::DrawUI(const PlayerView& view) const
	{
		if (!bOn)
		{
			return;
		}
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const bool bHeap = Stroke && StrokeMode == ESdfTerrainEditMode::Add;
		const ImU32 color = bHeap ? IM_COL32(120, 230, 120, 230) : IM_COL32(255, 170, 80, 230);
		if (Hit)
		{
			std::vector<ImVec2> ring;
			for (const glm::vec3& point : GetTerrainBrushOutline(*Hit, Settings.Radius))
			{
				if (const std::optional<glm::vec2> screen = view.ToScreen(point))
				{
					ring.push_back(ImVec2(screen->x, screen->y));
				}
			}
			if (ring.size() > 2)
			{
				drawList->AddPolyline(ring.data(), int(ring.size()), color, ImDrawFlags_Closed, 2.0f);
			}
		}
		char line[128];
		std::snprintf(line, sizeof(line), "Terrain brush %.1f m: left mouse digs, Shift heaps, wheel sizes, B off", Settings.Radius);
		drawList->AddText(ImVec2(view.PictureMin.x + 12.0f, view.PictureMin.y + view.PictureSize.y - 28.0f), color, line);
	}
}
