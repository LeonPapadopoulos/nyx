#pragma once

#include "InputHistory.h"
#include "InputInspection.h"
#include "InputSystem.h"

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Nyx::Input
{
	// What the debug window shows of an input system, as tabs, drawn into the current ImGui window
	// with the painter's widgets. The editor shows it as Window > Input, the game as its overlay
	// (Ctrl+Alt+Shift+I in Debug builds).
	// - Devices: each device with what it is and can do, a pad's controls painted live with its
	//   sticks' dead zones and the value after the pipeline, the stick plot, calibration, and
	//   buttons that try its output.
	// - Actions: every action's value and phase and the binding that decided it; for the one
	//   selected, every binding's trace: where it stopped and why.
	// - Contexts: the stack, with why each context is on, what it takes, and who decides the cursor.
	// - Control map: which action each control gets now, or with another stack (What if); Press to
	//   find lists every binding of the next control pressed.
	// - Events: every event and output, with pause, filter and copy.
	// - Timeline: chosen actions over the last seconds, with marks where they fired and where the
	//   stack changed; hovering a frame shows its traces.
	class InputDebugView
	{
	public:
		enum class ETab : uint8_t
		{
			Devices,
			Actions,
			Contexts,
			ControlMap,
			Events,
			Timeline,
			Count
		};

		static const char* GetTabName(ETab tab);

		// A tab of the owner's after the view's own, e.g. the editor's Tuning and Checks
		struct ExtraTab
		{
			const char* Name = "";
			std::function<void()> Draw;
		};

		// Draws the tabs. Asks the user for output when the Devices tab's buttons are clicked, and
		// runs calibration from the frames it is drawn in.
		void Draw(InputSystem& input, InputHistory& history, std::span<const ExtraTab> extraTabs = {});

		// Shows this tab from the next Draw() on
		void SelectTab(ETab tab)
		{
			TabToSelect = tab;
			ExtraTabToSelect.clear();
		}

		void SelectExtraTab(std::string name)
		{
			TabToSelect.reset();
			ExtraTabToSelect = std::move(name);
		}

		// The Timeline draws a second history over the drawn one, frame by frame, e.g. the same
		// recording replayed through B over A: the drawn one solid, this one faint. Null for none.
		void SetComparison(const InputHistory* other, std::string drawnLabel, std::string otherLabel)
		{
			Comparison = other;
			ComparisonLabels = { std::move(drawnLabel), std::move(otherLabel) };
		}

		// Whether the Devices tab's buttons can try the pads' output: not for a replica of another
		// program's input, whose pads are only pictures
		void SetCanSendOutput(bool bCan)
		{
			bCanSendOutput = bCan;
		}

	private:
		void DrawDevices(InputSystem& input, const InputHistory& history);
		void DrawGamepad(InputSystem& input, const InputHistory& history, const InputFrame::DeviceState& device);
		void DrawKeyboardAndMouse(const InputFrame::DeviceState& device, const InputFrame& frame);
		void DrawActions(const InputSystem& input, const InputHistory& history);
		void DrawContexts(const InputSystem& input);
		void DrawControlMap(const InputSystem& input);
		void DrawEvents(const InputSystem& input, InputHistory& history);
		void DrawTimeline(const InputSystem& input, InputHistory& history);

		std::optional<ETab> TabToSelect;
		std::string ExtraTabToSelect;
		bool bCanSendOutput = true;

		// Actions
		ActionHandle SelectedAction;

		// Control map
		int MapDevice = static_cast<int>(EDeviceKind::Gamepad);
		bool bWhatIf = false;
		std::vector<bool> WhatIfContexts;
		bool bPressToFind = false;
		std::optional<ControlId> FoundControl;

		// Events
		std::string EventFilter;
		bool bShowSensors = false;

		// Timeline: which actions it shows, and what it compares with
		std::vector<bool> TimelineActions;
		const InputHistory* Comparison = nullptr;
		std::array<std::string, 2> ComparisonLabels;

		// Devices
		StickCalibration Calibration;
		size_t NextTriggerEffect = 0;
		size_t NextLightColor = 0;
	};
}
