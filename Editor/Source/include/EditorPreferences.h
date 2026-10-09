#pragma once

#include <filesystem>

namespace Nyx::Editor
{
	enum class ESourceEditor
	{
		VisualStudio,
		VisualStudioCode
	};

	struct EditorPreferences
	{
		ESourceEditor SourceEditor = ESourceEditor::VisualStudio;

		static std::filesystem::path GetUserFile();
		bool Load(const std::filesystem::path& path);
		bool Save(const std::filesystem::path& path) const;
	};
}
