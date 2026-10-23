#include "InputFilePrinter.h"

#include "BinaryArchive.h"
#include "InputAsset.h"
#include "ReflectedArchivePrinter.h"

namespace Nyx
{
	bool PrintInputFile(const std::filesystem::path& path, std::string& outText)
	{
		BinaryReader reader;
		uint32_t magic = 0;
		uint32_t version = 0;
		if (!reader.LoadFromFile(path) || !reader.ReadUInt32(magic) || !reader.ReadUInt32(version))
		{
			outText += "Can't read the file\n";
			return false;
		}

		const Reflection::TypeMetadata* type = nullptr;
		if (magic == Input::InputAssetFileMagic)
		{
			type = &Reflection::GetTypeMetadata<Input::InputAssetData>();
			outText += "Input asset";
		}
		else if (magic == Input::InputPlayerSettingsFileMagic)
		{
			type = &Reflection::GetTypeMetadata<Input::InputPlayerSettingsData>();
			outText += "Player input settings";
		}
		else
		{
			outText += "Not an input asset or player input settings file\n";
			return false;
		}

		outText += ", version " + std::to_string(version) + "\n";
		if (!ReflectedArchivePrinter::PrintObject(reader, type, 1, outText))
		{
			outText += "The file is cut off or damaged\n";
			return false;
		}
		return true;
	}
}
