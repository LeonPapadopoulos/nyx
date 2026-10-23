#pragma once

#include "EditorLinkMessages.h"
#include "InputAsset.h"
#include "NetConnection.h"

#include <optional>
#include <string>
#include <vector>

// Live tuning: the editor sends its input asset to the running games as it edits it
// (SetInputAsset), and whether their pads drive them in the background (SetInputBackground).
// Both ends use these functions, so the tests can run them in one program.
namespace Nyx
{
	// The sending side: the asset as it is now
	SetInputAssetMessage MakeSetInputAssetMessage(const Nyx::Input::InputAssetData& asset);

	enum class ELiveInputResult : uint8_t
	{
		Applied,
		NotLiveInput, // Another message type; nothing happened
		Unreadable,   // Cut off or damaged; logged as an error
	};

	// The receiving side. SetInputAsset: puts the asset into the input system with the player's
	// settings over it (ApplyInputAsset): held buttons stay held, binding states fit the new
	// tuning. What can't be used goes to outProblems. SetInputBackground: InputSystem::
	// SetGamepadsInBackground().
	ELiveInputResult ApplyLiveInput(
		const Net::Message& message,
		Nyx::Input::InputSystem& input,
		const Nyx::Input::InputPlayerSettingsData* player,
		std::vector<Nyx::Input::InputAssetProblem>* outProblems = nullptr);
}
