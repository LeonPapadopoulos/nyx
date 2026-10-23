#include "EditorPreferences.h"
#include "BinaryArchive.h"

#include <Windows.h>
#include <ShlObj.h>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace
{
	// The largest picture side a setup may have, as NyxGame's --window-size accepts
	constexpr uint32_t MaxPlaySetupSide = 16384;

	std::optional<uint32_t> ParseNumber(std::string_view text)
	{
		uint32_t number = 0;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size())
		{
			return std::nullopt;
		}

		return number;
	}

	std::optional<float> ParseFloat(std::string_view text)
	{
		float number = 0.0f;
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size() || !std::isfinite(number))
		{
			return std::nullopt;
		}

		return number;
	}

	// The shortest text that reads back as the same float
	std::string FormatFloat(float number)
	{
		char buffer[32];
		const auto [end, error] = std::to_chars(buffer, buffer + sizeof(buffer), number);
		return error == std::errc() ? std::string(buffer, end) : std::string("0");
	}

	// "<in Play All: 0 or 1>,<width>x<height>,<name>", e.g. "1,1280x800,Steam Deck". The name comes
	// last, so it may contain commas.
	std::optional<Nyx::Editor::PlaySetup> ParsePlaySetup(std::string_view text)
	{
		const size_t firstComma = text.find(',');
		const size_t secondComma = firstComma == std::string_view::npos ? std::string_view::npos : text.find(',', firstComma + 1);
		if (secondComma == std::string_view::npos)
		{
			return std::nullopt;
		}

		const std::string_view inPlayAll = text.substr(0, firstComma);
		const std::string_view size = text.substr(firstComma + 1, secondComma - firstComma - 1);
		const size_t x = size.find('x');
		if ((inPlayAll != "0" && inPlayAll != "1") || x == std::string_view::npos)
		{
			return std::nullopt;
		}

		const std::optional<uint32_t> width = ParseNumber(size.substr(0, x));
		const std::optional<uint32_t> height = ParseNumber(size.substr(x + 1));
		if (!width || !height || *width == 0 || *height == 0 || *width > MaxPlaySetupSide || *height > MaxPlaySetupSide)
		{
			return std::nullopt;
		}

		Nyx::Editor::PlaySetup setup;
		setup.Name = std::string(text.substr(secondComma + 1));
		setup.Width = *width;
		setup.Height = *height;
		setup.bInPlayAll = inPlayAll == "1";
		return setup;
	}
}

namespace Nyx::Editor
{
	std::vector<PlaySetup> GetDefaultPlaySetups()
	{
		return {
			PlaySetup{ .Name = "720p", .Width = 1280, .Height = 720, .bInPlayAll = true },
			PlaySetup{ .Name = "Steam Deck", .Width = 1280, .Height = 800, .bInPlayAll = true },
			PlaySetup{ .Name = "4:3", .Width = 1024, .Height = 768, .bInPlayAll = true },
			PlaySetup{ .Name = "Portrait", .Width = 450, .Height = 800, .bInPlayAll = true },
		};
	}

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
		*this = EditorPreferences{};
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			std::error_code error;
			return !path.empty() && !std::filesystem::exists(path, error) && !error;
		}

		std::stringstream text;
		text << file.rdbuf();
		FromText(text.str());
		return !file.bad();
	}

	bool EditorPreferences::Save(const std::filesystem::path& path) const
	{
		if (path.empty()) return false;
		std::error_code error;
		if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), error);
		if (error) return false;

		const std::string text = ToText();
		Nyx::BinaryWriter writer;
		writer.WriteBytes(text.data(), text.size());
		return writer.SaveToFile(path);
	}

	std::string EditorPreferences::ToText() const
	{
		std::string text = std::string("# Nyx editor user preferences\nSourceEditor=") +
			(SourceEditor == ESourceEditor::VisualStudioCode ? "VisualStudioCode\n" : "VisualStudio\n");

		text += std::string("ConsoleWindowsInPlayAll=") + (bConsoleWindowsInPlayAll ? "1\n" : "0\n");
		text += std::string("GamepadsDriveAllGames=") + (bGamepadsDriveAllGames ? "1\n" : "0\n");
		text += std::string("Ambilight=") + (bAmbilight ? "1\n" : "0\n");
		text += "AmbilightStrength=" + FormatFloat(AmbilightStrength) + "\n";
		text += "AmbilightReach=" + FormatFloat(AmbilightReach) + "\n";
		text += std::string("VSync=") + (bVSync ? "1\n" : "0\n");
		text += "FrameRateCap=" + std::to_string(FrameRateCap) + "\n";
		// A line break would end the setting; such a theme isn't saved
		if (!Theme.empty() && Theme.find_first_of("\r\n") == std::string::npos)
		{
			text += "Theme=" + Theme + "\n";
		}
		if (!Workspace.empty() && Workspace.find_first_of("\r\n") == std::string::npos)
		{
			text += "Workspace=" + Workspace + "\n";
		}

		// In order; Play uses the first
		text += "# PlaySetup=<in Play All: 0 or 1>,<width>x<height>,<name>\n";
		for (const PlaySetup& setup : PlaySetups)
		{
			// A line break would end the setting; the name loses it
			std::string name = setup.Name;
			for (char& c : name)
			{
				c = (c == '\n' || c == '\r') ? ' ' : c;
			}

			text += "PlaySetup=" + std::string(setup.bInPlayAll ? "1" : "0") + "," + std::to_string(setup.Width) + "x" +
				std::to_string(setup.Height) + "," + name + "\n";
		}

		return text;
	}

	void EditorPreferences::FromText(const std::string& text)
	{
		std::vector<PlaySetup> playSetups;

		std::istringstream lines(text);
		std::string line;
		while (std::getline(lines, line))
		{
			if (!line.empty() && line.back() == '\r') line.pop_back();
			const std::string_view setting = line;

			if (setting == "SourceEditor=VisualStudioCode") SourceEditor = ESourceEditor::VisualStudioCode;
			else if (setting == "SourceEditor=VisualStudio") SourceEditor = ESourceEditor::VisualStudio;
			else if (setting == "ConsoleWindowsInPlayAll=1") bConsoleWindowsInPlayAll = true;
			else if (setting == "ConsoleWindowsInPlayAll=0") bConsoleWindowsInPlayAll = false;
			else if (setting == "GamepadsDriveAllGames=1") bGamepadsDriveAllGames = true;
			else if (setting == "GamepadsDriveAllGames=0") bGamepadsDriveAllGames = false;
			else if (setting == "Ambilight=1") bAmbilight = true;
			else if (setting == "Ambilight=0") bAmbilight = false;
			else if (setting.starts_with("AmbilightStrength="))
			{
				if (const std::optional<float> strength = ParseFloat(setting.substr(std::string_view("AmbilightStrength=").size())))
				{
					AmbilightStrength = std::clamp(*strength, 0.0f, 1.0f);
				}
			}
			else if (setting.starts_with("AmbilightReach="))
			{
				if (const std::optional<float> reach = ParseFloat(setting.substr(std::string_view("AmbilightReach=").size())))
				{
					AmbilightReach = std::clamp(*reach, 0.0f, MaxAmbilightReach);
				}
			}
			else if (setting == "VSync=1") bVSync = true;
			else if (setting == "VSync=0") bVSync = false;
			else if (setting.starts_with("FrameRateCap="))
			{
				// None, or one of the caps offered; anything else keeps the cap there was
				const std::string_view value = setting.substr(std::string_view("FrameRateCap=").size());
				uint32_t cap = 0;
				const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), cap);
				if (error == std::errc() && end == value.data() + value.size() &&
					(cap == 0 || std::ranges::find(FrameRateCaps, cap) != std::end(FrameRateCaps)))
				{
					FrameRateCap = cap;
				}
			}
			else if (setting.starts_with("Theme="))
			{
				Theme = std::string(setting.substr(std::string_view("Theme=").size()));
			}
			else if (setting.starts_with("Workspace="))
			{
				Workspace = std::string(setting.substr(std::string_view("Workspace=").size()));
			}
			else if (setting.starts_with("PlaySetup="))
			{
				// A damaged line loses only its setup
				if (std::optional<PlaySetup> setup = ParsePlaySetup(setting.substr(std::string_view("PlaySetup=").size())))
				{
					playSetups.push_back(std::move(*setup));
				}
			}
		}

		// Without any, the defaults stay, so Play always has a setup
		if (!playSetups.empty())
		{
			PlaySetups = std::move(playSetups);
		}
	}
}
