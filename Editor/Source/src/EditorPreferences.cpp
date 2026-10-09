#include "EditorPreferences.h"
#include "BinaryArchive.h"

#include <Windows.h>
#include <ShlObj.h>
#include <fstream>
#include <string>

namespace Nyx::Editor
{
	std::filesystem::path EditorPreferences::GetUserFile()
	{
		PWSTR directory = nullptr;
		if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &directory)))
		{
			return {};
		}
		const auto path = std::filesystem::path(directory) / L"Nyx" / L"Editor.ini";
		CoTaskMemFree(directory);
		return path;
	}

	bool EditorPreferences::Load(const std::filesystem::path& path)
	{
		SourceEditor = ESourceEditor::VisualStudio;
		std::ifstream file(path);
		if (!file)
		{
			std::error_code error;
			return !path.empty() && !std::filesystem::exists(path, error) && !error;
		}
		std::string line;
		while (std::getline(file, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			if (line == "SourceEditor=VisualStudioCode") SourceEditor = ESourceEditor::VisualStudioCode;
			else if (line == "SourceEditor=VisualStudio") SourceEditor = ESourceEditor::VisualStudio;
		}
		return !file.bad();
	}

	bool EditorPreferences::Save(const std::filesystem::path& path) const
	{
		if (path.empty()) return false;
		std::error_code error;
		if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
		if (error) return false;

		const std::string text = std::string("# Nyx editor user preferences\nSourceEditor=") +
			(SourceEditor == ESourceEditor::VisualStudioCode ? "VisualStudioCode\n" : "VisualStudio\n");
		Nyx::Engine::BinaryWriter writer;
		writer.WriteBytes(text.data(), text.size());
		return writer.SaveToFile(path);
	}
}
