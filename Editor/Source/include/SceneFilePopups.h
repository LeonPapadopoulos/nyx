#pragma once

#include <array>
#include <filesystem>
#include <optional>

namespace Nyx::Editor
{
	class AssetDatabase;

	// What the user chose in the scene file popups this frame
	struct SceneFileRequest
	{
		std::optional<std::filesystem::path> Load;
		std::optional<std::filesystem::path> SaveAs;
	};

	// The "Load Scene" and "Save Scene As" popups: a list of the scenes in Assets/Scenes, and a
	// file name for the scene to save there. The editor loads or saves what they return.
	class SceneFilePopups
	{
	public:
		void RequestLoad();

		// Opens with "UntitledScene" as the file name
		void RequestSaveAs();

		SceneFileRequest Draw(const AssetDatabase& assets);

	private:
		bool bOpenLoadPopup = false;
		bool bOpenSaveAsPopup = false;
		std::array<char, 256> SaveAsBuffer{};
	};
}
