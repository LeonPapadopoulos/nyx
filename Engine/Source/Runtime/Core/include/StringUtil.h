#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

// Text helpers. Case is ASCII only: other characters, e.g. of UTF-8 text, stay as they are.
namespace Nyx
{
	inline char ToLower(char c)
	{
		return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
	}

	inline std::string ToLower(std::string text)
	{
		for (char& c : text)
		{
			c = ToLower(c);
		}
		return text;
	}

	inline bool EqualsIgnoringCase(std::string_view a, std::string_view b)
	{
		return std::equal(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y)
			{
				return ToLower(x) == ToLower(y);
			});
	}

	// Whether `text` holds `part`, ignoring case
	inline bool ContainsIgnoringCase(std::string_view text, std::string_view part)
	{
		return std::search(text.begin(), text.end(), part.begin(), part.end(), [](char x, char y)
			{
				return ToLower(x) == ToLower(y);
			}) != text.end();
	}

	// Without the spaces, tabs and carriage returns at either end
	inline std::string_view Trim(std::string_view text)
	{
		const size_t first = text.find_first_not_of(" \t\r");
		if (first == std::string_view::npos)
		{
			return {};
		}
		const size_t last = text.find_last_not_of(" \t\r");
		return text.substr(first, last - first + 1);
	}

	// A path as UTF-8 text, and back: how paths are written to files and messages, whatever the
	// system's code page
	inline std::string PathText(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();
		return std::string(text.begin(), text.end());
	}

	inline std::filesystem::path TextPath(const std::string& text)
	{
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
	}
}
