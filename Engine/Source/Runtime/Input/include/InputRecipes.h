#pragma once

#include "InputAsset.h"

#include <cstdint>
#include <string>
#include <vector>

// Help to find the right setup: recipes that turn what an action is for into plain bindings, and
// what tuning values mean, in the units someone thinks in.
namespace Nyx::Input
{
	// What an action is for. Each recipe writes ordinary bindings, for keyboard and mouse and for
	// pads, which are edited like any others afterwards; every recipe passes every check.
	enum class EInputRecipe : uint8_t
	{
		Press,      // Do it once per press (jump, interact)
		Hold,       // Do it while held (sprint, block)
		Charge,     // Charge, release to do it (charged shot)
		TapAndHold, // Tap for one thing, hold for another (tap: interact, hold: reload)
		Move,       // Move
		Look,       // Look or aim
		Amount,     // An analog amount (throttle, brake)
		MenuStep,   // Step through a menu
		Count
	};

	// "Do it once per press"
	const char* GetRecipeName(EInputRecipe recipe);
	// What it writes, e.g. "Space and the bottom face button, Pressed, kept 100 ms for code that asks later"
	const char* GetRecipeDescription(EInputRecipe recipe);

	// Adds the recipe's action (two for TapAndHold: name, and name + "Hold") and its bindings to
	// the context. Returns the names of the actions added; none if the context isn't there or an
	// action of that name is.
	std::vector<std::string> AddRecipe(InputAssetData& asset, size_t contextIndex, EInputRecipe recipe, const std::string& actionName);

	// What the binding's tuning means: one line per setting that is on, e.g. "Inner dead zone
	// 0.15: ignores the first 15 % of the stick's travel". The action, if known, tells what the
	// value is used for.
	struct TuningExplanation
	{
		// The field's path in InputBindingData, e.g. "Pipeline.DeadZone.Inner"
		std::string Field;
		std::string Text;
	};

	std::vector<TuningExplanation> ExplainBinding(const InputBindingData& binding, const InputActionData* action);

	// The response curve: how far a control pushed by `in` (0 to 1, straight along x) comes out
	// after the dead zone and the curve, for drawing input against output
	float SampleResponse(const ValuePipeline& pipeline, float in);
}
