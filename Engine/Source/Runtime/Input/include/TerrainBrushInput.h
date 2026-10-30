#pragma once

#include "InputSystem.h"

#include <string>
#include <vector>

namespace Nyx::Input
{
	struct InputActionData;
	struct InputContextData;

	// The actions and contexts of a player's terrain brush (TerrainBrush.h). Two contexts:
	// - the brush context: ToggleTerrainBrush (B), on while the world has an SDF terrain;
	// - the stroke context, above the cameras', on while the brush is: TerrainBrushDig (the left
	//   mouse button), TerrainBrushHeap (Shift and the left, so the right stays the cameras' to look
	//   with) and TerrainBrushSize (the wheel), which it takes from the cameras meanwhile.
	// The player that uses it turns them on and off.
	class TerrainBrushInput
	{
	public:
		// The names its contexts are found by, and their priorities
		struct Settings
		{
			std::string ContextName = "TerrainBrush";
			std::string StrokeContextName = "TerrainBrushStroke";
			int Priority = 100;
			int StrokePriority = 150;
		};

		struct Actions
		{
			ActionHandle Toggle; // Button, pressed
			ActionHandle Dig;    // Button, down
			ActionHandle Heap;   // Button, down
			ActionHandle Size;   // Axis1D: wheel notches, away from the user positive
		};

		// Uses the actions and contexts of its names in the input system, e.g. from an input asset;
		// adds them as MakeContextsData() has them if they aren't there. The input system must
		// outlive this.
		TerrainBrushInput(InputSystem& input, Settings settings);

		// Its actions and contexts as data, as the default input asset has them
		static std::vector<InputActionData> MakeActionsData();
		static std::vector<InputContextData> MakeContextsData(const Settings& settings);

		// Count from the next frame, as all context changes do
		void SetActive(bool bActive, std::string reason);
		void SetStrokeActive(bool bActive, std::string reason);

		const Actions& GetActions() const
		{
			return BrushActions;
		}

	private:
		InputSystem& Input;
		Settings BrushSettings;
		Actions BrushActions;
		ContextHandle Context;
		ContextHandle StrokeContext;
	};
}
