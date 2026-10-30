#include "TerrainBrushInput.h"

#include "InputAsset.h"

namespace Nyx::Input
{
	std::vector<InputActionData> TerrainBrushInput::MakeActionsData()
	{
		// A brush points with the mouse
		return {
			{ "ToggleTerrainBrush", EActionValue::Button, EActionDevices::KeyboardMouse },
			{ "TerrainBrushDig", EActionValue::Button, EActionDevices::KeyboardMouse },
			{ "TerrainBrushHeap", EActionValue::Button, EActionDevices::KeyboardMouse },
			{ "TerrainBrushSize", EActionValue::Axis1D, EActionDevices::KeyboardMouse },
		};
	}

	std::vector<InputContextData> TerrainBrushInput::MakeContextsData(const Settings& settings)
	{
		InputContextData brush;
		brush.Name = settings.ContextName;
		brush.Priority = settings.Priority;
		InputBindingData toggle;
		toggle.Action = "ToggleTerrainBrush";
		toggle.Control = "Keyboard/B";
		toggle.Trigger.Kind = ETriggerKind::Pressed;
		brush.Bindings.push_back(toggle);

		InputContextData stroke;
		stroke.Name = settings.StrokeContextName;
		stroke.Priority = settings.StrokePriority;
		InputBindingData dig;
		dig.Action = "TerrainBrushDig";
		dig.Control = "Mouse/Left";
		stroke.Bindings.push_back(dig);
		// Shift and the left: the right button stays the cameras' to look with
		InputBindingData heap;
		heap.Action = "TerrainBrushHeap";
		heap.Control = "Mouse/Left";
		heap.bShift = true;
		stroke.Bindings.push_back(heap);
		// The wheel's notches away from the user make it bigger
		InputBindingData size;
		size.Action = "TerrainBrushSize";
		size.Control = "Mouse/Wheel";
		size.SwizzleX = 1;
		size.SwizzleY = -1;
		size.SwizzleZ = -1;
		stroke.Bindings.push_back(size);

		return { brush, stroke };
	}

	TerrainBrushInput::TerrainBrushInput(InputSystem& input, Settings settings)
		: Input(input)
		, BrushSettings(std::move(settings))
	{
		// Without an input asset that has them, the defaults
		if (!Input.FindContext(BrushSettings.ContextName).IsValid() || !Input.FindContext(BrushSettings.StrokeContextName).IsValid())
		{
			InputAssetData defaults;
			defaults.Actions = MakeActionsData();
			defaults.Contexts = MakeContextsData(BrushSettings);
			ApplyInputAsset(Input, defaults);
		}

		BrushActions.Toggle = Input.FindAction("ToggleTerrainBrush");
		BrushActions.Dig = Input.FindAction("TerrainBrushDig");
		BrushActions.Heap = Input.FindAction("TerrainBrushHeap");
		BrushActions.Size = Input.FindAction("TerrainBrushSize");
		Context = Input.FindContext(BrushSettings.ContextName);
		StrokeContext = Input.FindContext(BrushSettings.StrokeContextName);
	}

	void TerrainBrushInput::SetActive(bool bActive, std::string reason)
	{
		Input.GetUser().SetContextActive(Context, bActive, { .Reason = bActive ? std::move(reason) : std::string() });
	}

	void TerrainBrushInput::SetStrokeActive(bool bActive, std::string reason)
	{
		Input.GetUser().SetContextActive(StrokeContext, bActive, { .Reason = bActive ? std::move(reason) : std::string() });
	}
}
