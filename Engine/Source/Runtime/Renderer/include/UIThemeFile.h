#pragma once

#include "UITheme.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The theme's values by name, for the theme editor, and the theme file that holds them. Every
// member of Theme is one value here; NyxUIThemeTests fails when a member is missing.
//
// The theme file is text, one value per line:
//
//     # Comments start with '#' at the start of a line; [Group] lines are comments too
//     Accent = #2472FB
//     Card = #1B232CD9
//     FrameRounding = 6
//     CardShadow = #00000073 0 2 10
//
// - Colours are #RRGGBB, or #RRGGBBAA with alpha, as design tools give them; four numbers from
//   0 to 1 work too.
// - Sizes are one number.
// - Shadows are a colour, the offset (x and y) and the blur; glows a colour, the distance and the
//   strength; glass the refraction, the edge width and the rim's strength.
//
// A file needn't list every value: those it leaves out keep what they had.

namespace Nyx::UI
{
	enum class EThemeValueKind
	{
		Color,
		Float,
		Shadow,
		Glow,
		Glass,
	};

	// One value of the Theme: its group and name, its kind, where it is, and for sizes their range
	// and how fast dragging changes them (shadows, glows and glass: their sizes')
	struct ThemeValue
	{
		const char* Group = "";
		const char* Name = "";
		EThemeValueKind Kind = EThemeValueKind::Float;
		size_t Offset = 0;
		float Min = 0.0f;
		float Max = 1.0f;
		float Speed = 0.01f;

		// The value in `theme`, as its type; the kind must match
		template<typename T>
		T& In(Theme& theme) const
		{
			return *reinterpret_cast<T*>(reinterpret_cast<std::byte*>(&theme) + Offset);
		}

		template<typename T>
		const T& In(const Theme& theme) const
		{
			return *reinterpret_cast<const T*>(reinterpret_cast<const std::byte*>(&theme) + Offset);
		}
	};

	// Every value of the Theme, in the order of its members, grouped as in UITheme.h
	std::span<const ThemeValue> GetThemeValues();

	// The bytes a value of this kind takes in the Theme
	size_t GetThemeValueSize(EThemeValueKind kind);

	// Whether two themes hold the same values
	bool AreThemesEqual(const Theme& a, const Theme& b);

	// The theme as the file's text: every value, under a [Group] line per group
	std::string WriteThemeText(const Theme& theme);

	// Reads values from the file's text into `theme`. Returns what was wrong, one line each, with
	// its line number (unknown names, values that don't parse); the lines that were right are read
	// anyway.
	std::vector<std::string> ReadThemeText(std::string_view text, Theme& theme);

	// A colour as #RRGGBB, or #RRGGBBAA unless it is opaque, and back. Parsing takes the hex forms
	// with or without '#' (6 or 8 digits).
	std::string FormatThemeColor(const glm::vec4& color);
	std::optional<glm::vec4> ParseThemeColor(std::string_view text);

	// Writes and reads the file. Saving returns an error message on failure; loading returns
	// nothing if the file can't be read, else the problems ReadThemeText() found.
	std::optional<std::string> SaveThemeFile(const std::filesystem::path& path, const Theme& theme);
	std::optional<std::vector<std::string>> LoadThemeFile(const std::filesystem::path& path, Theme& theme);
}
