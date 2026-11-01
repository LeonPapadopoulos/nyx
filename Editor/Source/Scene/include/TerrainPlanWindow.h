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
	struct TerrainTraversal;
	struct TerrainWorldPlan;
}

namespace Nyx::Editor
{
	class SceneEditor;

	// The world plan of an SDF terrain with a Plan layer, from above (Window > Plan Map): the
	// selected terrain's, else the scene's first. The kinds of ground in their colours, rivers and
	// lakes, shaded by an overlay (height, slope, tunnels); its pins as rings; the places its Place
	// Rules found (TerrainPlacement.h) as diamonds, a colour a rule, hollow where a rule's Fallback
	// gave them, with their footprints once zoomed in; a place clicked, what can be reached from it
	// by the traversal graph (TerrainTraversal.h, kept while the terrain has Route Checks): walking
	// green, swimming blue, climbing yellow, jumping orange, nothing red; each scene view's camera as a wedge; what the plan made the place under the mouse to be. The wheel zooms round
	// the mouse, the right or middle button drags it, Fit shows all of it. X runs right, Z down.
	//
	// Steering it (TerrainPlanEdits.h), each an undo step, the plan made again when the mouse lets
	// go: a pin's middle dragged moves it, its ring sizes it; a double-click adds a pin of the kind
	// chosen (a Rift pin: a rift passes there); a right-click on a pin changes its kind or removes
	// it; the seed stepped or rolled. A Rift pin's path: Extend Path in its menu, then each
	// double-click adds a place the rift passes after the last, until Escape or a right-click; a
	// place dragged moves it, right-clicked removes it.
	//
	// Drawing over the plan by hand (SdfTerrainPlanOverride), with the Tool chosen, each stroke an undo
	// step, kept through every regeneration: Paint (more of a kind of ground along the stroke), Raise
	// and Lower (the ground along it), Cut and Heap (a ball cut or added every so far along it), all
	// Brush wide; a stroke starting in a pin's ring belongs to the pin and moves with it. They show as
	// wide lines and circles; a right-click on one removes it.
	enum class ETerrainPlanTool : uint8_t
	{
		Pins,
		Paint,
		Raise,
		Lower,
		Cut,
		Heap
	};

	class TerrainPlanWindow
	{
	public:
		void Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing);

		bool bOpen = false;
		Nyx::ETerrainPlanMapOverlay Overlay = Nyx::ETerrainPlanMapOverlay::Kinds;
		// What a double-click adds
		Nyx::ESdfTerrainLandform NewPinKind = Nyx::ESdfTerrainLandform::Plains;
		bool bShowPlaces = true;
		ETerrainPlanTool Tool = ETerrainPlanTool::Pins;
		// The tools' stroke: how far round it they reach, the kind Paint asks for, how far Raise and
		// Lower move the ground
		float BrushRadius = 60.0f;
		Nyx::ESdfTerrainLandform BrushLandform = Nyx::ESdfTerrainLandform::Roughland;
		float BrushHeight = 15.0f;
		// The place what can be reached is shown from, by its index; none: no reach shown
		std::optional<size_t> ReachFrom;

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
		// The stroke being drawn, in the terrain's space; the override the menu is for
		std::vector<glm::vec2> Stroke;
		std::optional<size_t> MenuOverride;
		// The reach's picture, a pixel a column, and the graph and place it was drawn from
		uint64_t ReachImage = 0;
		std::shared_ptr<const Nyx::TerrainTraversal> ReachDrawn;
		size_t ReachDrawnFrom = 0;
	};
}
