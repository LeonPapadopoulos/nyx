// The theme's values by name and the theme file (UIThemeFile.h): every member of Theme is listed,
// the file's text reads back as it was written, colours parse in their forms, and lines that are
// wrong are reported with their line numbers while the others are read.
#include "Paths.h"
#include "UIThemeFile.h"
#include "TestSupport.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::UI;

	using Nyx::Tests::Require;

	bool Near(const glm::vec4& a, const glm::vec4& b, float tolerance)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec4(tolerance)));
	}

	// Every byte of Theme belongs to exactly one value, so a member added to Theme without a
	// value fails here, as does a value that points at the wrong place or the wrong kind.
	void TestEveryMemberIsAValue()
	{
		std::vector<int> owners(sizeof(Theme), 0);
		for (const ThemeValue& value : GetThemeValues())
		{
			const size_t size = GetThemeValueSize(value.Kind);
			Require(value.Offset + size <= sizeof(Theme), std::string("Theme value ") + value.Name + " reaches past the Theme");
			for (size_t byte = value.Offset; byte < value.Offset + size; ++byte)
			{
				++owners[byte];
			}
			Require(value.Min <= value.Max, std::string("Theme value ") + value.Name + " has a range from more to less");
		}
		const auto unowned = std::find(owners.begin(), owners.end(), 0);
		Require(unowned == owners.end(), "Byte " + std::to_string(unowned - owners.begin()) + " of Theme belongs to no theme value: add the member to GetThemeValues()");
		Require(std::all_of(owners.begin(), owners.end(), [](int count) { return count == 1; }), "Two theme values overlap");

		for (const ThemeValue& value : GetThemeValues())
		{
			const size_t sameName = std::count_if(GetThemeValues().begin(), GetThemeValues().end(), [&](const ThemeValue& other)
				{
					return std::strcmp(other.Name, value.Name) == 0;
				});
			Require(sameName == 1, std::string("Two theme values are called ") + value.Name);
		}
	}

	void TestColors()
	{
		Require(Near(*ParseThemeColor("#2472FB"), glm::vec4(36.0f, 114.0f, 251.0f, 255.0f) / 255.0f, 1e-6f), "#RRGGBB should parse, opaque");
		Require(Near(*ParseThemeColor("1b232cd9"), glm::vec4(27.0f, 35.0f, 44.0f, 217.0f) / 255.0f, 1e-6f), "RRGGBBAA without '#' and in lower case should parse");
		Require(!ParseThemeColor("#2472F") && !ParseThemeColor("#2472FG") && !ParseThemeColor(""), "Colours of the wrong length or with other letters shouldn't parse");
		Require(FormatThemeColor(glm::vec4(36.0f, 114.0f, 251.0f, 255.0f) / 255.0f) == "#2472FB", "An opaque colour should be written without alpha");
		Require(FormatThemeColor(glm::vec4(0.0f, 0.0f, 0.0f, 0.45f)) == "#00000073", "A colour with alpha should be written with it");
	}

	// The defaults, and a theme with every kind changed, are read back as they were written. Colours
	// are written in 8 bits a channel, so they come back to within half a step.
	void TestRoundTrip()
	{
		Theme changed;
		changed.Accent = glm::vec4(0.2f, 0.4f, 0.6f, 0.8f);
		changed.FrameRounding = 3.25f;
		changed.CardShadow = Nyx::Engine::PainterShadow{ { 0.0f, 0.0f, 0.0f, 0.5f }, { 1.5f, -2.0f }, 12.0f };
		changed.FocusGlow = Nyx::Engine::PainterGlow{ { 1.0f, 0.0f, 0.0f, 1.0f }, 7.5f, 0.25f };
		changed.Glass = Nyx::Engine::PainterGlass{ 4.0f, 10.0f, 0.3f };

		for (const Theme* original : { &changed, &GetTheme() })
		{
			const std::string text = WriteThemeText(*original);
			Theme read;
			read.Accent = glm::vec4(1.0f);
			read.FrameRounding = 99.0f;
			const std::vector<std::string> problems = ReadThemeText(text, read);
			Require(problems.empty(), "The written theme should read without problems: " + (problems.empty() ? std::string() : problems.front()));
			Require(WriteThemeText(read) == text, "The theme read back should write the same text");
			for (const ThemeValue& value : GetThemeValues())
			{
				if (value.Kind == EThemeValueKind::Color)
				{
					Require(Near(value.In<glm::vec4>(read), value.In<glm::vec4>(*original), 0.5f / 255.0f + 1e-6f), std::string("Colour ") + value.Name + " should read back");
				}
				else if (value.Kind == EThemeValueKind::Float)
				{
					Require(std::abs(value.In<float>(read) - value.In<float>(*original)) < 1e-4f, std::string("Size ") + value.Name + " should read back");
				}
			}
		}

		Theme read;
		ReadThemeText(WriteThemeText(changed), read);
		Require(read.CardShadow.Offset == glm::vec2(1.5f, -2.0f) && read.CardShadow.Blur == 12.0f && std::abs(read.CardShadow.Color.a - 0.5f) < 0.003f, "A shadow should read back");
		Require(read.FocusGlow.Distance == 7.5f && read.FocusGlow.Strength == 0.25f, "A glow should read back");
		Require(read.Glass.Refraction == 4.0f && read.Glass.EdgeWidth == 10.0f && std::abs(read.Glass.RimStrength - 0.3f) < 1e-6f, "Glass should read back");
	}

	// Comments, groups and blank lines are skipped; values a file leaves out keep theirs; a wrong
	// line is reported with its number, and the right lines around it are read.
	void TestReading()
	{
		Theme theme;
		const glm::vec4 card = theme.Card;
		const std::vector<std::string> problems = ReadThemeText(
			"# a comment\n"
			"\n"
			"[Accent]\n"
			"  Accent = #FF0000  \r\n"
			"Frame = 0.1, 0.2, 0.3\n"
			"Nonsense = 4\n"
			"FrameRounding = round\n"
			"CardShadow = 0 0 0 0.5 1 2 3\n"
			"FocusGlow = #00FF00 5\n"
			"just text\n"
			"CardGap = 9\n",
			theme);
		Require(theme.Accent == glm::vec4(1.0f, 0.0f, 0.0f, 1.0f), "A colour with spaces and \\r around it should be read");
		Require(Near(theme.Frame, glm::vec4(0.1f, 0.2f, 0.3f, 1.0f), 1e-6f), "Three numbers with commas should be read as an opaque colour");
		Require(theme.CardShadow.Color == glm::vec4(0.0f, 0.0f, 0.0f, 0.5f) && theme.CardShadow.Offset == glm::vec2(1.0f, 2.0f) && theme.CardShadow.Blur == 3.0f,
			"A shadow's colour may be four numbers");
		Require(theme.CardGap == 9.0f, "Lines after wrong ones should still be read");
		Require(theme.Card == card, "Values a file leaves out should keep theirs");
		Require(theme.FrameRounding == Theme{}.FrameRounding && theme.FocusGlow.Distance == Theme{}.FocusGlow.Distance, "Wrong values should leave what was there");

		Require(problems.size() == 4, "Four lines are wrong, found " + std::to_string(problems.size()));
		Require(problems[0].starts_with("Line 6:") && problems[0].find("Nonsense") != std::string::npos, "An unknown name should be reported with its line: " + problems[0]);
		Require(problems[1].starts_with("Line 7:") && problems[1].find("FrameRounding") != std::string::npos, "A value that isn't a number should be reported: " + problems[1]);
		Require(problems[2].starts_with("Line 9:") && problems[2].find("FocusGlow") != std::string::npos, "A glow without its strength should be reported: " + problems[2]);
		Require(problems[3].starts_with("Line 10:"), "A line without '=' should be reported: " + problems[3]);
	}

	// Some editors start UTF-8 files with a byte order mark; the first line is read anyway
	void TestByteOrderMark()
	{
		Theme theme;
		const std::vector<std::string> problems = ReadThemeText("\xEF\xBB\xBF" "Accent = #FF8800\n", theme);
		Require(problems.empty() && theme.Accent == *ParseThemeColor("#FF8800"), "A byte order mark shouldn't spoil the first line");
	}

	// Each group's values follow each other, so the theme editor shows each group once
	void TestGroupsStayTogether()
	{
		const std::span<const ThemeValue> values = GetThemeValues();
		for (size_t index = 1; index < values.size(); ++index)
		{
			if (std::strcmp(values[index].Group, values[index - 1].Group) == 0)
			{
				continue;
			}
			const bool bSeenBefore = std::any_of(values.begin(), values.begin() + static_cast<std::ptrdiff_t>(index), [&](const ThemeValue& earlier)
				{
					return std::strcmp(earlier.Group, values[index].Group) == 0;
				});
			Require(!bSeenBefore, std::string("Theme group ") + values[index].Group + " is split; keep its values together in GetThemeValues()");
		}
	}

	void TestFile()
	{
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "NyxUIThemeTests" / "Test.nyxtheme";
		Theme theme;
		theme.Accent = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
		Require(!SaveThemeFile(path, theme), "Saving a theme file should work");
		Theme read;
		const std::optional<std::vector<std::string>> problems = LoadThemeFile(path, read);
		Require(problems && problems->empty() && read.Accent == theme.Accent, "A saved theme file should load");
		Require(AreThemesEqual(read, theme) || !AreThemesEqual(Theme{}, theme), "Comparing themes should see the change");
		Require(!LoadThemeFile(path.parent_path() / "Missing.nyxtheme", read), "A missing file shouldn't load");
		std::error_code ignored;
		std::filesystem::remove_all(path.parent_path(), ignored);
	}

	// The themes in Editor/Themes read without problems and set every value, so none of them
	// leans on the defaults by mistake
	void TestShippedThemes()
	{
		const std::filesystem::path folder = Nyx::Paths::GetProjectRoot() / "Editor" / "Themes";
		size_t count = 0;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(folder))
		{
			if (entry.path().extension() != ".nyxtheme")
			{
				continue;
			}
			++count;
			Theme theme;
			const std::optional<std::vector<std::string>> problems = LoadThemeFile(entry.path(), theme);
			Require(problems.has_value(), "Couldn't read " + entry.path().filename().string());
			Require(problems->empty(), entry.path().filename().string() + ": " + (problems->empty() ? std::string() : problems->front()));

			// Every value is written in the file, by name
			std::ifstream file(entry.path());
			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			for (const ThemeValue& value : GetThemeValues())
			{
				Require(text.find("\n" + std::string(value.Name) + " ") != std::string::npos, entry.path().filename().string() + " leaves out " + value.Name);
			}
		}
		Require(count >= 1, "Editor/Themes should hold at least the default theme");
	}

	// The defaults in UITheme.h are the default theme, Default.nyxtheme: its file holds the same values
	void TestDefaultsAreTheDefaultTheme()
	{
		Theme fromFile;
		const std::optional<std::vector<std::string>> problems = LoadThemeFile(Nyx::Paths::GetProjectRoot() / "Editor" / "Themes" / "Default.nyxtheme", fromFile);
		Require(problems && problems->empty(), "The default theme's file should read");
		const Theme defaults;
		for (const ThemeValue& value : GetThemeValues())
		{
			Theme one = defaults;
			std::memcpy(reinterpret_cast<std::byte*>(&one) + value.Offset, reinterpret_cast<const std::byte*>(&fromFile) + value.Offset, GetThemeValueSize(value.Kind));
			Require(WriteThemeText(one) == WriteThemeText(defaults), std::string("UITheme.h's ") + value.Name + " differs from the default theme's file");
		}
	}

	void TestEquality()
	{
		Theme a;
		Theme b;
		Require(AreThemesEqual(a, b), "Two default themes should be equal");
		b.CardShadow.Blur += 1.0f;
		Require(!AreThemesEqual(a, b), "A changed shadow should make themes differ");
	}
}

int main()
{
	try
	{
		TestEveryMemberIsAValue();
		TestColors();
		TestRoundTrip();
		TestReading();
		TestByteOrderMark();
		TestGroupsStayTogether();
		TestFile();
		TestEquality();
		TestShippedThemes();
		TestDefaultsAreTheDefaultTheme();
	}
	catch (const std::exception& error)
	{
		std::cerr << "UI theme test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All UI theme tests passed.\n";
	return 0;
}
