#pragma once

#include "InputAsset.h"
#include "InputChecks.h"
#include "InputRecipes.h"
#include "InputTrace.h"
#include "InspectorDrawContext.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace Nyx::Input
{
	class InputSystem;
}

namespace Nyx::Editor
{
	class InputAssetDocument;
	class TransactionSystem;

	// Window > Input's Tuning and Checks tabs, for the input asset the editor edits.
	// - Tuning: the asset's actions, contexts and bindings on the left; on the right, what is
	//   selected in the details table, where edits are undoable and felt at once. For a binding:
	//   what the checks find about it, with their fixes; what its tuning means, in units, with the
	//   layer each value comes from (the asset, a pad model's profile, the player's settings); its
	//   response curve with the stick's place on it now and the curve before the last edit; its
	//   trigger as a timeline with the last press over it. Add Action asks what the action is
	//   for and writes a recipe's bindings.
	// - Checks: every finding with where it is and its fix, what the asset has that can't be
	//   used, and Coverage: which devices can do each action.
	class InputTuningView
	{
	public:
		void DrawTuning(InputAssetDocument& document, TransactionSystem& transactions, Nyx::Input::InputSystem& input);
		void DrawChecks(InputAssetDocument& document, TransactionSystem& transactions, Nyx::Input::InputSystem& input);

		// Once per frame, after drawing: shows a value being dragged in the input system
		void PreviewPendingEdits()
		{
			DrawContext.PreviewPendingEdits();
		}

		// Selects a binding in the Tuning tab (the Checks tab's Where, tests)
		void SelectBinding(size_t context, size_t binding);

		// The Checks tab asked to show the Tuning tab: true once
		bool TakeShowTuningRequest()
		{
			return std::exchange(bShowTuningRequested, false);
		}

	private:
		enum class ESelection : uint8_t
		{
			Actions,
			Action,
			Contexts,
			Context,
			Binding,
			PadProfiles,
		};

		void DrawToolbar(InputAssetDocument& document, TransactionSystem& transactions, Nyx::Input::InputSystem& input);
		void DrawTree(InputAssetDocument& document);
		void DrawSelection(InputAssetDocument& document, TransactionSystem& transactions, Nyx::Input::InputSystem& input);
		void DrawBinding(InputAssetDocument& document, TransactionSystem& transactions, Nyx::Input::InputSystem& input);
		void DrawAddAction(InputAssetDocument& document, TransactionSystem& transactions);
		void DrawFix(InputAssetDocument& document, TransactionSystem& transactions, const Nyx::Input::InputCheckFinding& finding, size_t id);
		void DrawResponseCurve(const Nyx::Input::ValuePipeline& pipeline, std::optional<float> liveIn);
		void DrawTriggerTimeline(const Nyx::Input::TriggerSettings& trigger, const Nyx::Input::BindingTrace* trace);

		// The pad model whose profile the values are shown with
		Nyx::Input::EGamepadModel GetShownModel(const Nyx::Input::InputSystem& input) const;

		ESelection Selected = ESelection::Contexts;
		size_t SelectedAction = 0;
		size_t SelectedContext = 0;
		size_t SelectedBinding = 0;

		// The details table's edits in progress; its target is the asset
		InspectorDrawContext DrawContext;

		// "Layers for": 0 the pad in hand, then None, Xbox One, DualShock 4, DualSense
		int LayerModel = 0;

		// Add Action
		std::string NewActionName;
		int NewActionContext = 0;
		int NewActionRecipe = 0;

		// The response curve before the last edit, drawn as a ghost
		Nyx::Input::ValuePipeline StablePipeline;
		std::optional<Nyx::Input::ValuePipeline> GhostPipeline;
		size_t CurveOf = SIZE_MAX;

		// The trigger timeline's last press: how long it was held, and whether it fired
		float WaitedSeconds = 0.0f;
		bool bWaiting = false;
		std::optional<float> LastPressSeconds;
		bool bLastPressFired = false;
		bool bFiredDuringPress = false;

		bool bShowTuningRequested = false;
	};
}
