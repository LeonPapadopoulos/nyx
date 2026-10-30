#pragma once

#include "PlayerController.h"
#include "TerrainBrush.h"
#include "TerrainBrushInput.h"

#include <optional>

namespace Nyx
{
	// A player's terrain brush, for a player controller to carry: B turns it on and off while the
	// world has an SDF terrain; while on, the left mouse button digs and with Shift heaps up where the
	// aim meets the terrain, a stroke as long as it is held (TerrainBrushStroke), each edit an
	// SdfTerrainEditCommand, and the wheel makes it bigger or smaller. It aims along the ray the
	// player gives it each frame: the picture's middle while looking around, the cursor otherwise.
	// Its UI is a ring where it would land and a line saying what it does.
	class PlayerTerrainBrush
	{
	public:
		explicit PlayerTerrainBrush(Input::InputSystem& input);

		void Update(const PlayerFrame& frame, const std::optional<Ray>& aim);
		void DrawUI(const PlayerView& view) const;

		bool IsOn() const
		{
			return bOn;
		}

		TerrainBrushSettings Settings;
		// How much each wheel notch changes the radius, as a factor, and the radius's range
		float SizeStep = 1.15f;
		float MinRadius = 0.25f;
		float MaxRadius = 12.0f;

	private:
		Input::TerrainBrushInput BrushInput;
		bool bOn = false;
		std::optional<TerrainBrushHit> Hit;
		std::optional<TerrainBrushStroke> Stroke;
		ESdfTerrainEditMode StrokeMode = ESdfTerrainEditMode::Subtract;
	};
}
