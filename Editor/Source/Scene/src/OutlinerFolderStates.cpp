#include "OutlinerFolderStates.h"

#include "Log.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <string_view>
#include <system_error>

namespace Nyx::Editor
{
	void OutlinerFolderStates::SetFile(std::filesystem::path file)
	{
		File = std::move(file);

		std::ifstream input(File);
		std::string line;
		while (std::getline(input, line))
		{
			const size_t bar = line.rfind('|');
			if (bar == std::string::npos || bar == 0)
			{
				continue;
			}

			std::set<uint64_t>& closed = Closed[line.substr(0, bar)];
			std::string_view ids = std::string_view(line).substr(bar + 1);
			while (!ids.empty())
			{
				const size_t comma = std::min(ids.find(','), ids.size());
				uint64_t id = 0;
				const std::string_view text = ids.substr(0, comma);
				if (std::from_chars(text.data(), text.data() + text.size(), id, 16).ec == std::errc{} && id != 0)
				{
					closed.insert(id);
				}
				ids.remove_prefix(std::min(comma + 1, ids.size()));
			}
		}
	}

	void OutlinerFolderStates::SetScene(const std::filesystem::path& scene)
	{
		Scene = MakeKey(scene);
		// A new scene starts with its folders open
		if (Scene.empty())
		{
			Closed.erase(Scene);
		}
	}

	void OutlinerFolderStates::MoveScene(const std::filesystem::path& scene)
	{
		const std::string key = MakeKey(scene);
		if (key == Scene)
		{
			return;
		}

		std::set<uint64_t> closed = std::move(Closed[Scene]);
		Closed.erase(Scene);
		Closed[key] = std::move(closed);
		Scene = key;
		Save();
	}

	bool OutlinerFolderStates::IsOpen(uint64_t folder) const
	{
		const auto found = Closed.find(Scene);
		return found == Closed.end() || !found->second.contains(folder);
	}

	void OutlinerFolderStates::SetOpen(uint64_t folder, bool bOpen)
	{
		if (IsOpen(folder) == bOpen)
		{
			return;
		}

		if (bOpen)
		{
			Closed[Scene].erase(folder);
		}
		else
		{
			Closed[Scene].insert(folder);
		}
		Save();
	}

	std::string OutlinerFolderStates::MakeKey(const std::filesystem::path& scene)
	{
		if (scene.empty())
		{
			return {};
		}

		// The same file however its path was written
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(scene, error);
		return (error ? scene : absolute).lexically_normal().generic_string();
	}

	void OutlinerFolderStates::Save() const
	{
		if (File.empty() || Scene.empty())
		{
			return;
		}

		std::error_code error;
		std::filesystem::create_directories(File.parent_path(), error);
		std::ofstream output(File, std::ios::trunc);
		for (const auto& [scene, closed] : Closed)
		{
			if (scene.empty() || closed.empty())
			{
				continue;
			}

			output << scene << '|';
			bool bFirst = true;
			for (const uint64_t id : closed)
			{
				char text[17] = {};
				const auto end = std::to_chars(text, text + 16, id, 16).ptr;
				output << (bFirst ? "" : ",") << std::string_view(text, static_cast<size_t>(end - text));
				bFirst = false;
			}
			output << '\n';
		}

		if (!output)
		{
			LOG_WARNING("Outliner: could not save which folders are closed to '{0}'", File.string());
		}
	}
}
