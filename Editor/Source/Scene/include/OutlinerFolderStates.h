#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>

namespace Nyx::Editor
{
	// Which of the outliner's folders are closed, per scene. The editor's state, not the scene's:
	// opening a folder changes nothing to undo or save in the scene. A folder is open until it is
	// closed. Kept between runs in a text file of a line per scene, its path, then "|", then its
	// closed folders' ids in hex, separated by commas.
	class OutlinerFolderStates
	{
	public:
		// Where they are kept, e.g. Saved/OutlinerFolders.txt; read now. Without a file, they are
		// kept while the editor runs.
		void SetFile(std::filesystem::path file);

		// The scene the outliner shows; empty for one that wasn't saved yet
		void SetScene(const std::filesystem::path& scene);

		// The scene was saved as another file: its folders stay as they are, under its new path
		void MoveScene(const std::filesystem::path& scene);

		bool IsOpen(uint64_t folder) const;
		void SetOpen(uint64_t folder, bool bOpen);

	private:
		static std::string MakeKey(const std::filesystem::path& scene);
		void Save() const;

		std::filesystem::path File;
		std::string Scene;
		// The closed folders of each scene by its key (MakeKey()); "" for the one not saved yet,
		// which is never written to the file
		std::map<std::string, std::set<uint64_t>> Closed;
	};
}
