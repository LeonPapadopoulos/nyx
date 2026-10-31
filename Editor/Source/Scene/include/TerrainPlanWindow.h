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
	class TerrainPlanWindow
	{
	public:
		void Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing);

		bool bOpen = false;
		Nyx::ETerrainPlanMapOverlay Overlay = Nyx::ETerrainPlanMapOverlay::Kinds;

	private:
		// The map's picture, and the plan and overlay it was drawn from
		uint64_t Image = 0;
		std::shared_ptr<const Nyx::TerrainWorldPlan> Drawn;
		Nyx::ETerrainPlanMapOverlay DrawnOverlay = Nyx::ETerrainPlanMapOverlay::Kinds;
		// The terrain's point at the map's middle, and how many pixels a metre is; 0 fits it
		glm::vec2 Center{ 0.0f };
		float PixelsPerMetre = 0.0f;
	};
}
