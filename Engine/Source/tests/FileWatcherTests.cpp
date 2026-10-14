// FileWatcher on a real folder: files written, created in a folder inside, saved the way editors do
// (to a temporary file, then renamed), and deleted are reported; nothing is when nothing happens.
#include "FileWatcher.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using Clock = std::chrono::steady_clock;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	}

	// A folder of its own for the test, removed afterwards
	struct TestFolder
	{
		std::filesystem::path Path = std::filesystem::temp_directory_path() / ("NyxFileWatcherTests-" + std::to_string(Clock::now().time_since_epoch().count()));

		TestFolder()
		{
			std::filesystem::create_directories(Path);
		}

		~TestFolder()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}
	};

	bool Contains(const std::vector<std::filesystem::path>& changes, const std::filesystem::path& file)
	{
		const std::filesystem::path wanted = std::filesystem::absolute(file).lexically_normal();
		return std::any_of(changes.begin(), changes.end(), [&wanted](const std::filesystem::path& change)
			{
				return change == wanted;
			});
	}

	// Takes changes until `done` says enough arrived, for at most two seconds (the system reports
	// changes a little later); returns everything taken
	std::vector<std::filesystem::path> TakeUntil(Nyx::FileWatcher& watcher, const std::function<bool(const std::vector<std::filesystem::path>&)>& done)
	{
		std::vector<std::filesystem::path> taken;
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds(2);
		while (Clock::now() < deadline)
		{
			for (const std::filesystem::path& change : watcher.TakeChanges())
			{
				taken.push_back(change);
			}
			if (done(taken))
			{
				break;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		return taken;
	}

	void TestAFolderThatIsntThereIsReported()
	{
		Nyx::FileWatcher watcher;
		Require(!watcher.Watch(std::filesystem::temp_directory_path() / "NyxFileWatcherTests-no-such-folder"), "watching a missing folder succeeded");
		Require(!watcher.GetProblem().empty(), "no problem given for a missing folder");
	}

	void TestChangesAreReported()
	{
		TestFolder folder;
		const std::filesystem::path file = folder.Path / "Grid.frag";
		WriteFile(file, "first");
		std::this_thread::sleep_for(std::chrono::milliseconds(50));

		Nyx::FileWatcher watcher;
		Require(watcher.Watch(folder.Path), "can't watch: " + watcher.GetProblem());
		Require(watcher.TakeChanges().empty(), "changes before anything changed");

		// Written
		WriteFile(file, "second");
		std::vector<std::filesystem::path> changes = TakeUntil(watcher, [&](const auto& taken)
			{
				return Contains(taken, file);
			});
		Require(Contains(changes, file), "a written file isn't reported");

		// Created in a folder inside
		const std::filesystem::path inner = folder.Path / "Library" / "nyx" / "surface.slang";
		std::filesystem::create_directories(inner.parent_path());
		WriteFile(inner, "module");
		changes = TakeUntil(watcher, [&](const auto& taken)
			{
				return Contains(taken, inner);
			});
		Require(Contains(changes, inner), "a file created in a folder inside isn't reported");

		// Saved the way editors do: a temporary file, renamed over the real one
		const std::filesystem::path temporary = folder.Path / "Grid.frag.tmp~";
		WriteFile(temporary, "third");
		std::filesystem::rename(temporary, file);
		changes = TakeUntil(watcher, [&](const auto& taken)
			{
				return Contains(taken, file);
			});
		Require(Contains(changes, file), "a file replaced by renaming isn't reported");

		// Deleted
		std::filesystem::remove(file);
		changes = TakeUntil(watcher, [&](const auto& taken)
			{
				return Contains(taken, file);
			});
		Require(Contains(changes, file), "a deleted file isn't reported");

		// Nothing happens: nothing is reported
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		watcher.TakeChanges();
		std::this_thread::sleep_for(std::chrono::milliseconds(100));
		Require(watcher.TakeChanges().empty(), "changes reported while nothing changed");
	}

	// Many changes at once: each file is reported, or the folder itself when the system lost track
	void TestManyChanges()
	{
		TestFolder folder;
		Nyx::FileWatcher watcher;
		Require(watcher.Watch(folder.Path), "can't watch: " + watcher.GetProblem());

		std::vector<std::filesystem::path> files;
		for (int index = 0; index < 500; ++index)
		{
			files.push_back(folder.Path / ("Shader" + std::to_string(index) + ".frag"));
			WriteFile(files.back(), "x");
		}
		const auto allSeen = [&](const std::vector<std::filesystem::path>& taken)
		{
			return Contains(taken, folder.Path) || std::all_of(files.begin(), files.end(), [&taken](const auto& file)
													   {
														   return Contains(taken, file);
													   });
		};
		Require(allSeen(TakeUntil(watcher, allSeen)), "of 500 new files, some weren't reported, and neither was the folder");
	}
}

int main()
{
	try
	{
		TestAFolderThatIsntThereIsReported();
		TestChangesAreReported();
		TestManyChanges();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All file watcher tests passed\n";
	return 0;
}
