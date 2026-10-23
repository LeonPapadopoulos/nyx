#include "NyxPCH.h"
#include "LiveInput.h"

#include "InputSystem.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"

namespace Nyx
{
	SetInputAssetMessage MakeSetInputAssetMessage(const Nyx::Input::InputAssetData& asset)
	{
		BinaryWriter writer;
		ReflectedArchiveSerializer::SerializeObject(writer, &asset, Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>());
		return SetInputAssetMessage{ writer.GetBytes() };
	}

	ELiveInputResult ApplyLiveInput(
		const Net::Message& message,
		Nyx::Input::InputSystem& input,
		const Nyx::Input::InputPlayerSettingsData* player,
		std::vector<Nyx::Input::InputAssetProblem>* outProblems)
	{
		switch (static_cast<EEditorLinkMessage>(message.Type))
		{
		case EEditorLinkMessage::SetInputAsset:
		{
			SetInputAssetMessage setInputAsset;
			Nyx::Input::InputAssetData asset;
			ReadWarnings warnings;
			BinaryReader reader;
			if (ReadNetMessage(message, setInputAsset))
			{
				reader.LoadFromMemory(setInputAsset.Asset);
			}
			if (setInputAsset.Asset.empty() ||
				!ReflectedArchiveSerializer::DeserializeObject(reader, &asset, Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>(), warnings))
			{
				LOG_ERROR("Live input: a SetInputAsset message can't be read");
				return ELiveInputResult::Unreadable;
			}
			warnings.Log("SetInputAsset");

			std::vector<Nyx::Input::InputAssetProblem> problems = Nyx::Input::ApplyInputAsset(input, asset, player);
			if (outProblems)
			{
				*outProblems = std::move(problems);
			}
			return ELiveInputResult::Applied;
		}

		case EEditorLinkMessage::SetInputBackground:
		{
			SetInputBackgroundMessage background;
			if (!ReadNetMessage(message, background))
			{
				LOG_ERROR("Live input: a SetInputBackground message can't be read");
				return ELiveInputResult::Unreadable;
			}
			input.SetGamepadsInBackground(background.bGamepadsInBackground);
			return ELiveInputResult::Applied;
		}

		default:
			return ELiveInputResult::NotLiveInput;
		}
	}
}
