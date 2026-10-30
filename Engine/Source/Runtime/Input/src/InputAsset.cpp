#include "InputAsset.h"

#include "BinaryArchive.h"
#include "FlyCameraInput.h"
#include "InputChecks.h"
#include "Log.h"
#include "OrbitCameraInput.h"
#include "Paths.h"
#include "PropertyValueAccess.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionUtils.h"
#include "TerrainBrushInput.h"
#include "ThirdPersonInput.h"

#include <algorithm>
#include <cstdlib>
#include <set>

namespace Nyx::Input
{
	namespace
	{
		constexpr uint32_t FileVersion = 1;

		template<typename T>
		bool SaveFile(const T& data, uint32_t magic, const std::filesystem::path& path)
		{
			Nyx::BinaryWriter writer;
			writer.WriteUInt32(magic);
			writer.WriteUInt32(FileVersion);
			if (!Nyx::ReflectedArchiveSerializer::SerializeObject(writer, &data, Reflection::GetTypeMetadata<T>()))
			{
				return false;
			}

			std::error_code error;
			std::filesystem::create_directories(path.parent_path(), error);
			return writer.SaveToFile(path);
		}

		template<typename T>
		std::optional<T> LoadFile(uint32_t magic, const std::filesystem::path& path, Nyx::ReadWarnings& warnings)
		{
			Nyx::BinaryReader reader;
			uint32_t fileMagic = 0;
			uint32_t version = 0;
			if (!reader.LoadFromFile(path) || !reader.ReadUInt32(fileMagic) || fileMagic != magic || !reader.ReadUInt32(version) ||
				version > FileVersion)
			{
				return std::nullopt;
			}

			T data;
			if (!Nyx::ReflectedArchiveSerializer::DeserializeObject(reader, &data, Reflection::GetTypeMetadata<T>(), warnings))
			{
				return std::nullopt;
			}
			return data;
		}

		bool OverrideMatchesControl(const std::string& control, const InputBindingData& binding)
		{
			if (control.empty() || binding.Control == control)
			{
				return true;
			}
			const CompositeKeysData& keys = binding.Composite;
			return keys.Right == control || keys.Left == control || keys.Up == control || keys.Down == control || keys.Forward == control ||
				keys.Back == control;
		}

		std::string Where(const InputContextData& context, size_t bindingIndex)
		{
			return context.Name + ", binding " + std::to_string(bindingIndex);
		}

		std::optional<ControlId> ParseOptionalPath(const std::string& path, bool& bOk)
		{
			if (path.empty())
			{
				return std::nullopt;
			}
			const std::optional<ControlId> control = ParseControlPath(path);
			bOk = bOk && control.has_value();
			return control;
		}

		// The models that have a profile, each once
		std::vector<EGamepadModel> ProfiledModels(const InputAssetData& asset)
		{
			std::set<EGamepadModel> models;
			for (const PadProfileData& profile : asset.PadProfiles)
			{
				if (profile.Model != EGamepadModel::Unknown)
				{
					models.insert(profile.Model);
				}
			}
			return std::vector<EGamepadModel>(models.begin(), models.end());
		}
	}

	const char* GetTuningLayerName(ETuningLayer layer)
	{
		switch (layer)
		{
		case ETuningLayer::Asset:      return "asset";
		case ETuningLayer::PadProfile: return "pad profile";
		case ETuningLayer::Player:     return "player";
		}
		return "unknown";
	}

	bool SaveInputAsset(const InputAssetData& asset, const std::filesystem::path& path)
	{
		return SaveFile(asset, InputAssetFileMagic, path);
	}

	std::optional<InputAssetData> LoadInputAsset(const std::filesystem::path& path, Nyx::ReadWarnings& warnings)
	{
		return LoadFile<InputAssetData>(InputAssetFileMagic, path, warnings);
	}

	bool SavePlayerSettings(const InputPlayerSettingsData& settings, const std::filesystem::path& path)
	{
		return SaveFile(settings, InputPlayerSettingsFileMagic, path);
	}

	std::optional<InputPlayerSettingsData> LoadPlayerSettings(const std::filesystem::path& path, Nyx::ReadWarnings& warnings)
	{
		return LoadFile<InputPlayerSettingsData>(InputPlayerSettingsFileMagic, path, warnings);
	}

	std::filesystem::path GetDefaultInputAssetPath()
	{
		return Paths::FindAsset(std::filesystem::path("Input") / "Default.nyxinput");
	}

	std::filesystem::path GetPlayerInputSettingsPath()
	{
		const wchar_t* localAppData = _wgetenv(L"LOCALAPPDATA");
		return localAppData ? std::filesystem::path(localAppData) / L"Nyx" / L"PlayerInput.nyxinput" : std::filesystem::path();
	}

	bool MatchesOverride(const TuningOverrideData& override, const InputContextData& context, const InputBindingData& binding)
	{
		return (override.Context.empty() || override.Context == context.Name) && (override.Action.empty() || override.Action == binding.Action) &&
			OverrideMatchesControl(override.Control, binding);
	}

	bool ApplyOverride(InputBindingData& binding, const TuningOverrideData& override, std::string* outProblem)
	{
		using namespace Nyx::Reflection;

		// Walk the path's structs down to its last property
		void* object = &binding;
		const TypeMetadata* type = &GetTypeMetadata<InputBindingData>();
		const PropertyMetadata* property = nullptr;
		size_t start = 0;
		while (true)
		{
			const size_t dot = override.Field.find('.', start);
			const std::string name = override.Field.substr(start, dot == std::string::npos ? std::string::npos : dot - start);
			property = FindPropertyByName(*type, name);
			if (!property)
			{
				if (outProblem)
				{
					*outProblem = "\"" + override.Field + "\" isn't a field of a binding";
				}
				return false;
			}
			if (dot == std::string::npos)
			{
				break;
			}

			const TypeMetadata* nested = TryGetNestedType(*property);
			if (!nested)
			{
				if (outProblem)
				{
					*outProblem = "\"" + override.Field + "\": " + name + " has no fields";
				}
				return false;
			}
			object = GetPropertyAddress(object, *property);
			type = nested;
			start = dot + 1;
		}

		switch (property->Kind)
		{
		case EPropertyKind::Float: SetPropertyValue(object, *property, override.Value); return true;
		// Every component, e.g. a sensitivity's scale
		case EPropertyKind::Vec2:   SetPropertyValue(object, *property, glm::vec2(override.Value)); return true;
		case EPropertyKind::Vec3:   SetPropertyValue(object, *property, glm::vec3(override.Value)); return true;
		case EPropertyKind::Vec4:   SetPropertyValue(object, *property, glm::vec4(override.Value)); return true;
		case EPropertyKind::Bool:   SetPropertyValue(object, *property, override.Value != 0.0f); return true;
		case EPropertyKind::Int32:  SetPropertyValue(object, *property, static_cast<int32_t>(override.Value)); return true;
		case EPropertyKind::UInt32: SetPropertyValue(object, *property, static_cast<uint32_t>(std::max(override.Value, 0.0f))); return true;
		case EPropertyKind::Enum:
		{
			const EnumMetadata* enumMetadata = TryGetEnum(*property);
			if (enumMetadata && FindEnumEntry(*enumMetadata, static_cast<int64_t>(override.Value)))
			{
				SetPropertyValue(object, *property, static_cast<int32_t>(override.Value));
				return true;
			}
			if (outProblem)
			{
				*outProblem = "\"" + override.Field + "\": no enumerator has the number " + std::to_string(static_cast<int64_t>(override.Value));
			}
			return false;
		}
		default:
			if (outProblem)
			{
				*outProblem = "\"" + override.Field + "\" is a " + GetPropertyKindName(property->Kind) + ", not a number, bool, enum or vector";
			}
			return false;
		}
	}

	InputAssetData ResolveLayers(const InputAssetData& asset, const InputPlayerSettingsData* player, EGamepadModel model, std::vector<InputAssetProblem>* outProblems)
	{
		InputAssetData resolved = asset;

		const auto applyLayer = [&](const std::vector<TuningOverrideData>& overrides, const std::string& layerName)
		{
			for (size_t overrideIndex = 0; overrideIndex < overrides.size(); ++overrideIndex)
			{
				// A field that doesn't fit fails for every binding the same way: reported once
				const TuningOverrideData& override = overrides[overrideIndex];
				bool bReported = false;
				for (InputContextData& context : resolved.Contexts)
				{
					for (InputBindingData& binding : context.Bindings)
					{
						std::string problem;
						if (MatchesOverride(override, context, binding) && !ApplyOverride(binding, override, &problem) && outProblems && !bReported)
						{
							outProblems->push_back({ layerName + ", override " + std::to_string(overrideIndex), problem });
							bReported = true;
						}
					}
				}
			}
		};

		for (const PadProfileData& profile : asset.PadProfiles)
		{
			if (profile.Model == model && model != EGamepadModel::Unknown)
			{
				applyLayer(profile.Overrides, std::string(GetGamepadModelName(model)) + " profile");
			}
		}
		if (player)
		{
			applyLayer(player->Overrides, "Player settings");
		}
		return resolved;
	}

	std::optional<LayeredValue> FindTuning(
		const InputAssetData& asset,
		const InputPlayerSettingsData* player,
		size_t contextIndex,
		size_t bindingIndex,
		const std::string& field,
		EGamepadModel model)
	{
		if (contextIndex >= asset.Contexts.size() || bindingIndex >= asset.Contexts[contextIndex].Bindings.size())
		{
			return std::nullopt;
		}

		const InputContextData& context = asset.Contexts[contextIndex];
		InputBindingData binding = context.Bindings[bindingIndex];

		// The asset's value, as the override would set it: read through an override of the same field
		const auto read = [&field](const InputBindingData& from) -> std::optional<float>
		{
			using namespace Nyx::Reflection;
			const void* object = &from;
			const TypeMetadata* type = &GetTypeMetadata<InputBindingData>();
			size_t start = 0;
			while (true)
			{
				const size_t dot = field.find('.', start);
				const PropertyMetadata* property = FindPropertyByName(*type, field.substr(start, dot == std::string::npos ? std::string::npos : dot - start));
				if (!property)
				{
					return std::nullopt;
				}
				if (dot == std::string::npos)
				{
					const PropertyValue value = GetPropertyValue(object, *property);
					if (const float* asFloat = std::get_if<float>(&value))
					{
						return *asFloat;
					}
					if (const bool* asBool = std::get_if<bool>(&value))
					{
						return *asBool ? 1.0f : 0.0f;
					}
					if (const int32_t* asInt = std::get_if<int32_t>(&value))
					{
						return static_cast<float>(*asInt);
					}
					if (const uint32_t* asUInt = std::get_if<uint32_t>(&value))
					{
						return static_cast<float>(*asUInt);
					}
					// A vector by its x, as an override sets all its components alike
					if (const glm::vec2* asVec2 = std::get_if<glm::vec2>(&value))
					{
						return asVec2->x;
					}
					if (const glm::vec3* asVec3 = std::get_if<glm::vec3>(&value))
					{
						return asVec3->x;
					}
					if (const glm::vec4* asVec4 = std::get_if<glm::vec4>(&value))
					{
						return asVec4->x;
					}
					return std::nullopt;
				}
				const TypeMetadata* nested = TryGetNestedType(*property);
				if (!nested)
				{
					return std::nullopt;
				}
				object = GetPropertyAddress(object, *property);
				type = nested;
				start = dot + 1;
			}
		};

		const std::optional<float> assetValue = read(binding);
		if (!assetValue)
		{
			return std::nullopt;
		}

		LayeredValue result{ *assetValue, ETuningLayer::Asset };
		const auto applyLayer = [&](const std::vector<TuningOverrideData>& overrides, ETuningLayer layer)
		{
			for (const TuningOverrideData& override : overrides)
			{
				if (override.Field == field && MatchesOverride(override, context, binding) && ApplyOverride(binding, override))
				{
					result = { read(binding).value_or(result.Value), layer };
				}
			}
		};

		for (const PadProfileData& profile : asset.PadProfiles)
		{
			if (profile.Model == model && model != EGamepadModel::Unknown)
			{
				applyLayer(profile.Overrides, ETuningLayer::PadProfile);
			}
		}
		if (player)
		{
			applyLayer(player->Overrides, ETuningLayer::Player);
		}
		return result;
	}

	std::optional<InputBinding> MakeBinding(const InputSystem& input, const InputBindingData& data, std::string* outProblem)
	{
		const auto fail = [outProblem](std::string problem) -> std::optional<InputBinding>
		{
			if (outProblem)
			{
				*outProblem = std::move(problem);
			}
			return std::nullopt;
		};

		InputBinding binding;
		binding.Action = input.FindAction(data.Action);
		if (!binding.Action.IsValid())
		{
			return fail("no action is called \"" + data.Action + "\"");
		}

		if (!data.Control.empty())
		{
			binding.Control = ParseControlPath(data.Control);
			if (!binding.Control)
			{
				return fail("\"" + data.Control + "\" isn't a control");
			}
		}
		else
		{
			bool bOk = true;
			const CompositeKeysData& keys = data.Composite;
			CompositeKeys composite;
			composite.Right = ParseOptionalPath(keys.Right, bOk);
			composite.Left = ParseOptionalPath(keys.Left, bOk);
			composite.Up = ParseOptionalPath(keys.Up, bOk);
			composite.Down = ParseOptionalPath(keys.Down, bOk);
			composite.Forward = ParseOptionalPath(keys.Forward, bOk);
			composite.Back = ParseOptionalPath(keys.Back, bOk);
			composite.bNormalize = keys.bNormalize;
			if (!bOk)
			{
				return fail("one of the composite's controls isn't a control");
			}
			if (!composite.Right && !composite.Left && !composite.Up && !composite.Down && !composite.Forward && !composite.Back)
			{
				return fail("no control: neither Control nor any of the composite's");
			}
			binding.Composite = composite;
		}

		binding.Swizzle = glm::ivec3(data.SwizzleX, data.SwizzleY, data.SwizzleZ);
		binding.Modifiers = EModifierKeys::None;
		binding.Modifiers = data.bCtrl ? binding.Modifiers | EModifierKeys::Ctrl : binding.Modifiers;
		binding.Modifiers = data.bShift ? binding.Modifiers | EModifierKeys::Shift : binding.Modifiers;
		binding.Modifiers = data.bAlt ? binding.Modifiers | EModifierKeys::Alt : binding.Modifiers;
		binding.Modifiers = data.bSuper ? binding.Modifiers | EModifierKeys::Super : binding.Modifiers;
		binding.Pipeline = data.Pipeline;
		binding.Trigger = data.Trigger;
		binding.bTakesControls = data.bTakesControls;

		if (binding.Trigger.Kind == ETriggerKind::Chord)
		{
			binding.Trigger.ChordAction = input.FindAction(binding.Trigger.ChordActionName);
			if (!binding.Trigger.ChordAction.IsValid())
			{
				return fail("the chord's action \"" + binding.Trigger.ChordActionName + "\" isn't an action");
			}
		}
		return binding;
	}

	std::vector<InputAssetProblem> ApplyInputAsset(InputSystem& input, const InputAssetData& asset, const InputPlayerSettingsData* player)
	{
		std::vector<InputAssetProblem> problems;

		for (const InputActionData& action : asset.Actions)
		{
			const InputActionDef def{ action.Name, action.ValueType };
			const ActionHandle existing = input.FindAction(action.Name);
			if (existing.IsValid())
			{
				input.ReplaceAction(existing, def);
			}
			else
			{
				input.AddAction(def);
			}
		}

		// The asset with the player's settings, and once more for each pad model with a profile.
		// The player's problems are the same each time: only the first time's count.
		const InputAssetData base = ResolveLayers(asset, player, EGamepadModel::Unknown, &problems);
		std::vector<std::pair<EGamepadModel, InputAssetData>> tuned;
		for (const EGamepadModel model : ProfiledModels(asset))
		{
			std::vector<InputAssetProblem> modelProblems;
			tuned.push_back({ model, ResolveLayers(asset, player, model, &modelProblems) });
			for (InputAssetProblem& problem : modelProblems)
			{
				const bool bKnown = std::any_of(problems.begin(), problems.end(),
					[&problem](const InputAssetProblem& known)
					{
						return known.Where == problem.Where && known.What == problem.What;
					});
				if (!bKnown)
				{
					problems.push_back(std::move(problem));
				}
			}
		}

		for (size_t contextIndex = 0; contextIndex < base.Contexts.size(); ++contextIndex)
		{
			const InputContextData& data = base.Contexts[contextIndex];
			InputContextDef def;
			def.Name = data.Name;
			def.Priority = data.Priority;
			switch (data.Cursor)
			{
			case EContextCursor::Unchanged: break;
			case EContextCursor::Free:      def.Cursor = ECursorMode::Free; break;
			case EContextCursor::Hidden:    def.Cursor = ECursorMode::Hidden; break;
			case EContextCursor::Captured:  def.Cursor = ECursorMode::Captured; break;
			}
			def.TakesDevices = (data.bTakesKeyboard ? DeviceBit(EDeviceKind::Keyboard) : 0u) | (data.bTakesMouse ? DeviceBit(EDeviceKind::Mouse) : 0u) |
				(data.bTakesGamepad ? DeviceBit(EDeviceKind::Gamepad) : 0u);
			for (const auto& [model, assetForModel] : tuned)
			{
				def.ModelVariants.push_back({ model, {} });
			}

			// Bindings that can't be used are left out of every variant alike, so they stay in step
			for (size_t bindingIndex = 0; bindingIndex < data.Bindings.size(); ++bindingIndex)
			{
				std::string problem;
				std::optional<InputBinding> binding = MakeBinding(input, data.Bindings[bindingIndex], &problem);
				if (!binding)
				{
					problems.push_back({ Where(data, bindingIndex), problem });
					continue;
				}
				def.Bindings.push_back(std::move(*binding));

				for (size_t variant = 0; variant < tuned.size(); ++variant)
				{
					const InputBindingData& variantData = tuned[variant].second.Contexts[contextIndex].Bindings[bindingIndex];
					std::optional<InputBinding> variantBinding = MakeBinding(input, variantData);
					def.ModelVariants[variant].Bindings.push_back(variantBinding ? std::move(*variantBinding) : def.Bindings.back());
				}
			}

			const ContextHandle existing = input.FindContext(data.Name);
			if (existing.IsValid())
			{
				input.ReplaceContext(existing, std::move(def));
			}
			else
			{
				input.AddContext(std::move(def));
			}
		}

		if (player)
		{
			input.GetUser().SetHapticsStrength(player->HapticsStrength);
		}
		return problems;
	}

	InputAssetData MakeDefaultInputAsset()
	{
		InputAssetData asset;
		asset.Actions = FlyCameraInput::MakeActionsData();

		// The editor's scene views, and the game's camera
		for (const FlyCameraInput::Settings& settings :
			{ FlyCameraInput::Settings{ .FlyContextName = "EditorFlyCamera", .MouseLookContextName = "EditorMouseLook" }, FlyCameraInput::Settings{} })
		{
			for (InputContextData& context : FlyCameraInput::MakeContextsData(settings))
			{
				asset.Contexts.push_back(std::move(context));
			}
		}

		// The game's orbit cameras
		for (InputActionData& action : OrbitCameraInput::MakeActionsData())
		{
			asset.Actions.push_back(std::move(action));
		}
		for (InputContextData& context : OrbitCameraInput::MakeContextsData(OrbitCameraInput::Settings{}))
		{
			asset.Contexts.push_back(std::move(context));
		}

		// The game's built-in player, walking a character
		for (InputActionData& action : ThirdPersonInput::MakeActionsData())
		{
			asset.Actions.push_back(std::move(action));
		}
		for (InputContextData& context : ThirdPersonInput::MakeContextsData(ThirdPersonInput::Settings{}))
		{
			asset.Contexts.push_back(std::move(context));
		}

		// Its terrain brush
		for (InputActionData& action : TerrainBrushInput::MakeActionsData())
		{
			asset.Actions.push_back(std::move(action));
		}
		for (InputContextData& context : TerrainBrushInput::MakeContextsData(TerrainBrushInput::Settings{}))
		{
			asset.Contexts.push_back(std::move(context));
		}
		return asset;
	}

	bool LoadDefaultInput(InputSystem& input, bool bCreateIfMissing, bool bWithPlayerSettings)
	{
		const std::filesystem::path assetPath = GetDefaultInputAssetPath();
		std::error_code error;
		if (bCreateIfMissing && !std::filesystem::exists(assetPath, error))
		{
			if (SaveInputAsset(MakeDefaultInputAsset(), assetPath))
			{
				LOG_INFO("Input: made {0} from the defaults", assetPath.string());
			}
			else
			{
				LOG_WARNING("Input: could not write {0}", assetPath.string());
			}
		}

		Nyx::ReadWarnings warnings;
		const std::optional<InputAssetData> asset = LoadInputAsset(assetPath, warnings);
		warnings.Log(assetPath.string());
		if (!asset)
		{
			LOG_WARNING("Input: could not read {0}; using the built-in bindings", assetPath.string());
			return false;
		}

		std::optional<InputPlayerSettingsData> player;
		const std::filesystem::path playerPath = GetPlayerInputSettingsPath();
		if (bWithPlayerSettings && !playerPath.empty() && std::filesystem::exists(playerPath, error))
		{
			Nyx::ReadWarnings playerWarnings;
			player = LoadPlayerSettings(playerPath, playerWarnings);
			playerWarnings.Log(playerPath.string());
			if (!player)
			{
				LOG_WARNING("Input: could not read the player's settings, {0}", playerPath.string());
			}
		}

		for (const InputAssetProblem& problem : ApplyInputAsset(input, *asset, player ? &*player : nullptr))
		{
			LOG_WARNING("Input: {0}: {1}: {2}", assetPath.filename().string(), problem.Where, problem.What);
		}
		for (const InputCheckFinding& finding : RunInputChecks(*asset))
		{
			LOG_WARNING("Input: {0}: {1}: {2}: {3}", assetPath.filename().string(), GetInputCheckName(finding.Check), DescribeWhere(*asset, finding),
				finding.Message);
		}
		return true;
	}
}
