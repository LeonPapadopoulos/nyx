#pragma once

#include "Entity.h"
#include "TerrainWorldPlanMap.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace Nyx
{
	class IEditorRenderer;
	struct TerrainWorldPlan;
}

namespace Nyx::Editor
{
	class SceneEditor;

	// The world plan of an SDF terrain with a Plan layer, from above (Window > Plan Map): the
	// selected terrain's, else the scene's first. The kinds of ground in their colours, rivers and
	// lakes, shaded by an overlay (height, slope, tunnels); its pins as rings; each scene view's
	// camera as a wedge; what the plan made the place under the mouse to be. The wheel zooms round
	// the mouse, the right or middle button drags it, Fit shows all of it. X runs right, Z down.
	//
	// Steering it (TerrainPlanEdits.h), each an undo step, the plan made again when the mouse lets
	// go: a pin's middle dragged moves it, its ring sizes it; a double-click adds a pin of the kind
	// chosen (a Rift pin: a rift passes there); a right-click on a pin changes its kind or removes
	// it; the seed stepped or rolled. A Rift pin's path: Extend Path in its menu, then each
	// double-click adds a place the rift passes after the last, until Escape or a right-click; a
	// place dragged moves it, right-clicked removes it.
	class TerrainPlanWindow
	{
	public:
		void Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing);

		bool bOpen = false;
		Nyx::ETerrainPlanMapOverlay Overlay = Nyx::ETerrainPlanMapOverlay::Kinds;
		// What a double-click adds
		Nyx::ESdfTerrainLandform NewPinKind = Nyx::ESdfTerrainLandform::Plains;

	private:
		// The map's picture, and the plan and overlay it was drawn from
		uint64_t Image = 0;
		std::shared_ptr<const Nyx::TerrainWorldPlan> Drawn;
		Nyx::ETerrainPlanMapOverlay DrawnOverlay = Nyx::ETerrainPlanMapOverlay::Kinds;
		// The terrain's point at the map's middle, and how many pixels a metre is; 0 fits it
		glm::vec2 Center{ 0.0f };
		float PixelsPerMetre = 0.0f;
		// The pin being dragged, as it is now, and whether by its ring; the pin the menu is for
		std::optional<size_t> Dragged;
		bool bDraggingRing = false;
		Nyx::SdfTerrainPlanPin DraggedPin;
		// The place of its path being dragged, -1 for none
		int DraggedPlace = -1;
		std::optional<size_t> MenuPin;
		int MenuPlace = -1;
		// The Rift pin double-clicks add places to the path of
		std::optional<size_t> Extending;
	};
}
