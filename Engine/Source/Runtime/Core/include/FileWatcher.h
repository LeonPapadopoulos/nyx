#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Nyx
{
	// Tells which files in some folders changed: created, written, renamed or deleted, in the
	// folders and all folders inside them. Nothing blocks and there are no threads: the owner calls
	// TakeChanges() once per frame. Windows only (ReadDirectoryChangesW).
	class FileWatcher
	{
	public:
		FileWatcher();
		~FileWatcher();

		FileWatcher(const FileWatcher&) = delete;
		FileWatcher& operator=(const FileWatcher&) = delete;

		// Starts watching the folder and everything inside it. False when it can't, with the
		// reason in GetProblem().
		bool Watch(const std::filesystem::path& folder);

		// The files that changed since the last call, each once, as absolute paths. When the system
		// had more changes than it could hold, the watched folder itself is among them: anything
		// in it may have changed.
		std::vector<std::filesystem::path> TakeChanges();

		const std::string& GetProblem() const
		{
			return Problem;
		}

	private:
		struct Folder;
		std::vector<std::unique_ptr<Folder>> Folders;
		std::string Problem;
	};
}
