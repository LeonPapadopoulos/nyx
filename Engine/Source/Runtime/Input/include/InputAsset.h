#pragma once

#include "ReflectionMacros.h"

#include "InputPipeline.h"
#include "InputSystem.h"
#include "InputTriggers.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace Nyx
{
	class ReadWarnings;
}

// Input assets (.nyxinput): actions, contexts and their bindings, and how pad models' profiles
// tune them, as data the reflection system saves and the editor edits. Bindings name their
// controls by path ("Gamepad/LeftStick") and their actions by name; ApplyInputAsset() turns them
// into the input system's contexts.
//
// Tuning comes in layers; the later wins:
// 1. the asset: the bindings as written;
// 2. a pad model's profile, for the bindings while that model drives them (e.g. a larger inner
//    dead zone for a model whose sticks drift more);
// 3. the player's settings, in a file of their own (PlayerInput.nyxinput), never in the asset.
// A layer is a list of overrides: one tuning value each, by its path in InputBindingData.
namespace Nyx::Input
{
	NYX_REFLECT()
	struct InputActionData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		EActionValue ValueType = EActionValue::Button;
	};

	// Controls making up an axis, by their paths ("Keyboard/W"); empty ones add nothing. x is Right
	// minus Left, y Up minus Down, z Forward minus Back.
	NYX_REFLECT()
	struct CompositeKeysData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Right;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Left;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Up;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Down;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Forward;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Back;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bNormalize = true;
	};

	NYX_REFLECT()
	struct InputBindingData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Action;

		// One control by its path, or empty for the composite
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Control;

		// Which of the control's axes go to x, y and z (0, 1, 2), or -1 for 0
		NYX_PROPERTY(Edit, Undo, Serialize)
		int32_t SwizzleX = 0;
		NYX_PROPERTY(Edit, Undo, Serialize)
		int32_t SwizzleY = 1;
		NYX_PROPERTY(Edit, Undo, Serialize)
		int32_t SwizzleZ = 2;

		NYX_PROPERTY(Edit, Undo, Serialize)
		CompositeKeysData Composite;

		// Keys that must be held too
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bCtrl = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bShift = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bAlt = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bSuper = false;

		NYX_PROPERTY(Edit, Undo, Serialize)
		ValuePipeline Pipeline;

		NYX_PROPERTY(Edit, Undo, Serialize)
		TriggerSettings Trigger;

		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bTakesControls = true;
	};

	// What a context does to the cursor while it is on
	NYX_ENUM()
	enum class EContextCursor : uint8_t
	{
		Unchanged,
		Free,
		Hidden,
		Captured,
	};

	NYX_REFLECT()
	struct InputContextData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Name;

		NYX_PROPERTY(Edit, Undo, Serialize)
		int32_t Priority = 0;

		NYX_PROPERTY(Edit, Undo, Serialize)
		EContextCursor Cursor = EContextCursor::Unchanged;

		// Devices it takes as a whole while on
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bTakesKeyboard = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bTakesMouse = false;
		NYX_PROPERTY(Edit, Undo, Serialize)
		bool bTakesGamepad = false;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<InputBindingData> Bindings;
	};

	// One tuning value a layer changes: the field at Field's path in InputBindingData (e.g.
	// "Pipeline.DeadZone.Inner"), in every binding of the context, action and control given; empty
	// ones match any. Bools take 0 or 1, enums their number.
	NYX_REFLECT()
	struct TuningOverrideData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Context;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Action;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Control;
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::string Field;
		NYX_PROPERTY(Edit, Undo, Serialize)
		float Value = 0.0f;
	};

	// How a pad model's bindings differ, while that model drives them
	NYX_REFLECT()
	struct PadProfileData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		EGamepadModel Model = EGamepadModel::Unknown;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<TuningOverrideData> Overrides;
	};

	NYX_REFLECT()
	struct InputAssetData
	{
		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<InputActionData> Actions;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<InputContextData> Contexts;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<PadProfileData> PadProfiles;
	};

	// The player's own settings: what they changed, over the asset and the profiles
	NYX_REFLECT()
	struct InputPlayerSettingsData
	{
		// Scales rumble and trigger rumble; 0 turns them off
		NYX_PROPERTY(Edit, Undo, Serialize)
		float HapticsStrength = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize)
		std::vector<TuningOverrideData> Overrides;
	};

	enum class ETuningLayer : uint8_t
	{
		Asset,
		PadProfile,
		Player,
	};

	const char* GetTuningLayerName(ETuningLayer layer);

	// Something in an asset that can't be used, and where
	struct InputAssetProblem
	{
		// E.g. "EditorFlyCamera, binding 2"
		std::string Where;
		std::string What;
	};

	// Files: magic and version, then one property block (ReflectedArchiveSerializer). Saving replaces
	// the file only once all of it is written.
	inline constexpr uint32_t InputAssetFileMagic = 0x4E504E49;          // 'INPN'
	inline constexpr uint32_t InputPlayerSettingsFileMagic = 0x53504E49; // 'INPS'

	bool SaveInputAsset(const InputAssetData& asset, const std::filesystem::path& path);
	std::optional<InputAssetData> LoadInputAsset(const std::filesystem::path& path, Nyx::ReadWarnings& warnings);
	bool SavePlayerSettings(const InputPlayerSettingsData& settings, const std::filesystem::path& path);
	std::optional<InputPlayerSettingsData> LoadPlayerSettings(const std::filesystem::path& path, Nyx::ReadWarnings& warnings);

	// Input/Default.nyxinput in the first asset folder that has it, the project's first
	// (Paths::FindAsset()); in the project's assets if none has it
	std::filesystem::path GetDefaultInputAssetPath();

	// %LOCALAPPDATA%\Nyx\PlayerInput.nyxinput
	std::filesystem::path GetPlayerInputSettingsPath();

	// Whether the override is for this binding of this context
	bool MatchesOverride(const TuningOverrideData& override, const InputContextData& context, const InputBindingData& binding);

	// Sets the field the override names. False, with the reason, if the field isn't a number, bool
	// or enum of InputBindingData.
	bool ApplyOverride(InputBindingData& binding, const TuningOverrideData& override, std::string* outProblem = nullptr);

	// The asset as it is with its layers for a pad model: the model's profile (none for Unknown),
	// then the player's settings (if any)
	InputAssetData ResolveLayers(
		const InputAssetData& asset,
		const InputPlayerSettingsData* player,
		EGamepadModel model,
		std::vector<InputAssetProblem>* outProblems = nullptr);

	// A tuning value of one binding after the layers, and the layer it came from
	struct LayeredValue
	{
		float Value = 0.0f;
		ETuningLayer Layer = ETuningLayer::Asset;
	};

	std::optional<LayeredValue> FindTuning(
		const InputAssetData& asset,
		const InputPlayerSettingsData* player,
		size_t contextIndex,
		size_t bindingIndex,
		const std::string& field,
		EGamepadModel model);

	// A binding of the asset as the input system's; unset, with the reason, if its action or a
	// control isn't known
	std::optional<InputBinding> MakeBinding(const InputSystem& input, const InputBindingData& data, std::string* outProblem = nullptr);

	// Puts the asset's actions and contexts into the input system, with its layers: actions and
	// contexts are matched by name, so those already there keep their handles and contexts stay on
	// or off. Each context gets a variant of its bindings for every pad model with a profile. The
	// player's haptics strength goes to the user. Bindings that can't be used are left out and
	// reported.
	std::vector<InputAssetProblem> ApplyInputAsset(
		InputSystem& input,
		const InputAssetData& asset,
		const InputPlayerSettingsData* player = nullptr);

	// What the engine's own cameras use, the editor's scene views and the game's fly camera.
	// Assets/Input/Default.nyxinput is made from it when it isn't there.
	InputAssetData MakeDefaultInputAsset();

	// Puts Assets/Input/Default.nyxinput into the input system, made from MakeDefaultInputAsset()
	// first if it isn't there and bCreateIfMissing; with the player's settings if
	// bWithPlayerSettings and they have any. Logs what can't be read or used. False if there is no
	// asset to read.
	bool LoadDefaultInput(InputSystem& input, bool bCreateIfMissing, bool bWithPlayerSettings);
}

#include "Generated/Runtime/InputAsset.reflect.h"
