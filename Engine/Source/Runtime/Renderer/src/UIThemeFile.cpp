#include "UIThemeFile.h"

#include "StringUtil.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <format>
#include <fstream>
#include <sstream>
#include <system_error>

namespace Nyx::UI
{
	namespace
	{
		// The value list's entries: a colour; a size with its range and drag speed; a shadow, glow
		// or glass with the range of its sizes
		constexpr ThemeValue ColorValue(const char* group, const char* name, size_t offset)
		{
			return ThemeValue{ group, name, EThemeValueKind::Color, offset, 0.0f, 1.0f, 0.005f };
		}

		constexpr ThemeValue SizeValue(const char* group, const char* name, size_t offset, float min, float max, float speed)
		{
			return ThemeValue{ group, name, EThemeValueKind::Float, offset, min, max, speed };
		}

		constexpr ThemeValue EffectValue(const char* group, const char* name, EThemeValueKind kind, size_t offset, float max)
		{
			return ThemeValue{ group, name, kind, offset, 0.0f, max, 0.1f };
		}

#define NYX_THEME_COLOR(group, member) ColorValue(group, #member, offsetof(Theme, member))
#define NYX_THEME_SIZE(group, member, min, max, speed) SizeValue(group, #member, offsetof(Theme, member), min, max, speed)
#define NYX_THEME_EFFECT(group, member, kind, max) EffectValue(group, #member, EThemeValueKind::kind, offsetof(Theme, member), max)

		// In the order of Theme's members, but for a group's values, which stay together. Pixel sizes
		// drag by a tenth of a pixel; fractions of the frame height by a hundredth.
		const ThemeValue ThemeValues[] = {
			NYX_THEME_COLOR("Text", Text),
			NYX_THEME_COLOR("Text", TextMuted),
			NYX_THEME_COLOR("Text", TextInfo),

			NYX_THEME_COLOR("Controls", Frame),
			NYX_THEME_COLOR("Controls", FrameHovered),
			NYX_THEME_COLOR("Controls", FramePressed),
			NYX_THEME_COLOR("Controls", FrameBorder),
			NYX_THEME_SIZE("Controls", FrameRounding, 0.0f, 16.0f, 0.1f),
			NYX_THEME_SIZE("Controls", FrameBorderWidth, 0.0f, 4.0f, 0.05f),

			NYX_THEME_COLOR("Buttons", ButtonGo),
			NYX_THEME_COLOR("Buttons", ButtonGoHovered),
			NYX_THEME_COLOR("Buttons", ButtonGoPressed),
			NYX_THEME_COLOR("Buttons", ButtonStop),
			NYX_THEME_COLOR("Buttons", ButtonStopHovered),
			NYX_THEME_COLOR("Buttons", ButtonStopPressed),

			NYX_THEME_COLOR("Accent", Accent),
			NYX_THEME_COLOR("Accent", AccentHovered),

			NYX_THEME_COLOR("Toggles and sliders", Track),
			NYX_THEME_COLOR("Toggles and sliders", TrackHovered),
			NYX_THEME_COLOR("Toggles and sliders", Knob),
			NYX_THEME_SIZE("Toggles and sliders", ToggleWidth, 0.5f, 3.0f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", ToggleHeight, 0.2f, 1.5f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", ToggleKnobRadius, 0.1f, 0.75f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", KnobPressedScale, 0.5f, 1.0f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", SliderKnobRadius, 0.1f, 0.75f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", SliderTrackHeight, 0.02f, 0.5f, 0.005f),
			NYX_THEME_SIZE("Toggles and sliders", KnobHoverGrowth, 0.0f, 0.5f, 0.01f),
			NYX_THEME_SIZE("Toggles and sliders", ValueGap, 0.0f, 32.0f, 0.1f),

			NYX_THEME_COLOR("Lines", Separator),

			NYX_THEME_EFFECT("Effects", ControlShadow, Shadow, 32.0f),
			NYX_THEME_SIZE("Effects", PressedShadow, 0.0f, 1.0f, 0.01f),
			NYX_THEME_SIZE("Effects", PressedTextOffset, 0.0f, 4.0f, 0.05f),
			NYX_THEME_EFFECT("Effects", FocusGlow, Glow, 32.0f),

			NYX_THEME_SIZE("Section headers", SectionBarWidth, 0.0f, 8.0f, 0.05f),
			NYX_THEME_SIZE("Section headers", SectionBarGap, 0.0f, 32.0f, 0.1f),
			NYX_THEME_EFFECT("Section headers", SectionBarGlow, Glow, 32.0f),
			NYX_THEME_SIZE("Section headers", SectionLineWidth, 0.0f, 4.0f, 0.05f),

			NYX_THEME_COLOR("Cards", Backdrop),
			NYX_THEME_COLOR("Cards", Card),
			NYX_THEME_COLOR("Cards", CardBorder),
			NYX_THEME_SIZE("Cards", CardRounding, 0.0f, 24.0f, 0.1f),
			NYX_THEME_SIZE("Cards", CardGap, 0.0f, 24.0f, 0.1f),
			NYX_THEME_EFFECT("Cards", CardShadow, Shadow, 64.0f),
			NYX_THEME_COLOR("Cards", FloatingCard),
			NYX_THEME_EFFECT("Cards", FloatingCardShadow, Shadow, 64.0f),
			NYX_THEME_COLOR("Cards", GlassTint),
			NYX_THEME_EFFECT("Cards", Glass, Glass, 48.0f),
			NYX_THEME_COLOR("Cards", Splitter),
			NYX_THEME_SIZE("Cards", SplitterWidth, 0.0f, 8.0f, 0.05f),

			NYX_THEME_COLOR("Tabs", TabSelected),
			NYX_THEME_COLOR("Tabs", TabHovered),
			NYX_THEME_SIZE("Tabs", TabPillInset, 0.0f, 12.0f, 0.1f),
			NYX_THEME_COLOR("Tabs", TabUnderline),
			NYX_THEME_COLOR("Tabs", TabUnderlineDimmed),
			NYX_THEME_SIZE("Tabs", TabUnderlineWidth, 0.0f, 6.0f, 0.05f),

			NYX_THEME_COLOR("Tree rows", RowSelected),
			NYX_THEME_COLOR("Tree rows", RowHovered),
			NYX_THEME_SIZE("Tree rows", RowPillMargin, 0.0f, 16.0f, 0.1f),
			NYX_THEME_SIZE("Tree rows", RowPillInset, 0.0f, 8.0f, 0.05f),
			NYX_THEME_COLOR("Tree rows", IndentGuide),
			NYX_THEME_COLOR("Tree rows", FolderIcon),
			NYX_THEME_COLOR("Tree rows", EntityIcon),

			NYX_THEME_COLOR("Fields", Field),
			NYX_THEME_COLOR("Fields", FieldHovered),
			NYX_THEME_COLOR("Fields", FieldBorder),
			NYX_THEME_EFFECT("Fields", FieldEditGlow, Glow, 32.0f),
			NYX_THEME_COLOR("Fields", TextSelection),
			NYX_THEME_COLOR("Fields", TextCursor),
			NYX_THEME_SIZE("Fields", SearchIconSize, 0.2f, 1.0f, 0.01f),

			NYX_THEME_COLOR("Axes", AxisX),
			NYX_THEME_COLOR("Axes", AxisY),
			NYX_THEME_COLOR("Axes", AxisZ),
			NYX_THEME_COLOR("Axes", AxisW),

			NYX_THEME_COLOR("Property rows", PropertyLabel),
			NYX_THEME_SIZE("Property rows", PropertyLabelWidth, 0.1f, 0.9f, 0.005f),

			NYX_THEME_COLOR("Collapsing headers", Header),
			NYX_THEME_COLOR("Collapsing headers", HeaderHovered),

			NYX_THEME_SIZE("Checkboxes", CheckboxSize, 0.3f, 1.0f, 0.01f),
			NYX_THEME_SIZE("Checkboxes", CheckboxRounding, 0.0f, 12.0f, 0.05f),
			NYX_THEME_SIZE("Checkboxes", CheckmarkWidth, 0.05f, 0.3f, 0.005f),

			NYX_THEME_COLOR("Menus", MenuItemHovered),

			NYX_THEME_SIZE("Bars", TitlebarHeight, 24.0f, 80.0f, 0.5f),
			NYX_THEME_SIZE("Bars", ToolbarHeight, 24.0f, 80.0f, 0.5f),
			NYX_THEME_SIZE("Bars", StatusBarHeight, 20.0f, 60.0f, 0.5f),
			NYX_THEME_COLOR("Bars", Titlebar),
			NYX_THEME_COLOR("Bars", Toolbar),
			NYX_THEME_COLOR("Bars", StatusBar),
			NYX_THEME_COLOR("Bars", BarBorder),
			NYX_THEME_COLOR("Bars", BarDivider),
			NYX_THEME_SIZE("Bars", BarDividerHeight, 0.0f, 1.0f, 0.01f),
			NYX_THEME_COLOR("Bars", BarTab),
			NYX_THEME_COLOR("Bars", BarTabBorder),
			NYX_THEME_SIZE("Bars", BarTabRounding, 0.0f, 16.0f, 0.1f),
			NYX_THEME_COLOR("Bars", Logo),
			NYX_THEME_SIZE("Bars", IconSize, 0.2f, 0.9f, 0.01f),
			NYX_THEME_COLOR("Bars", ButtonPrimary),
			NYX_THEME_COLOR("Bars", ButtonPrimaryHovered),
			NYX_THEME_COLOR("Bars", ButtonPrimaryPressed),
			NYX_THEME_COLOR("Bars", ButtonPrimaryBorder),
			NYX_THEME_COLOR("Bars", StatusOk),
			NYX_THEME_COLOR("Bars", StatusWarning),
			NYX_THEME_COLOR("Bars", StatusError),
			NYX_THEME_COLOR("Bars", StatusIconMark),

			NYX_THEME_COLOR("Window", WindowControlHovered),
			NYX_THEME_COLOR("Window", WindowControlPressed),
			NYX_THEME_COLOR("Window", WindowCloseHovered),
			NYX_THEME_COLOR("Window", WindowClosePressed),
			NYX_THEME_SIZE("Window", WindowControlIconSize, 4.0f, 24.0f, 0.1f),
			NYX_THEME_SIZE("Window", WindowControlIconWidth, 0.5f, 3.0f, 0.05f),
			NYX_THEME_COLOR("Window", WindowFocusBorder),
			NYX_THEME_SIZE("Window", WindowFocusBorderWidth, 0.0f, 8.0f, 0.05f),
			NYX_THEME_SIZE("Window", WindowRounding, 0.0f, 20.0f, 0.1f),

			NYX_THEME_COLOR("Colour swatches", SwatchBorder),
			NYX_THEME_COLOR("Colour swatches", SwatchBorderHovered),

			NYX_THEME_COLOR("Tables", TableHeader),
			NYX_THEME_COLOR("Tables", TableRowStripe),

			NYX_THEME_SIZE("Asset cards", AssetCardWidth, 48.0f, 200.0f, 0.5f),
			NYX_THEME_SIZE("Asset cards", AssetCardPadding, 0.0f, 24.0f, 0.1f),
			NYX_THEME_COLOR("Asset cards", AssetCard),
			NYX_THEME_COLOR("Asset cards", AssetCardHovered),
			NYX_THEME_COLOR("Asset cards", FileIcon),

			NYX_THEME_COLOR("Scrollbars", ScrollbarThumb),
			NYX_THEME_COLOR("Scrollbars", ScrollbarThumbHovered),
			NYX_THEME_COLOR("Scrollbars", ScrollbarThumbHeld),
			NYX_THEME_SIZE("Scrollbars", ScrollbarThumbWidth, 1.0f, 16.0f, 0.05f),
			NYX_THEME_SIZE("Scrollbars", ScrollbarThumbHoveredWidth, 1.0f, 16.0f, 0.05f),

			NYX_THEME_SIZE("General", DisabledAlpha, 0.0f, 1.0f, 0.005f),
			NYX_THEME_SIZE("General", AnimationSpeed, 1.0f, 60.0f, 0.1f),
		};

#undef NYX_THEME_COLOR
#undef NYX_THEME_SIZE
#undef NYX_THEME_EFFECT

		// The words of a value, split at spaces, tabs and commas
		std::vector<std::string_view> SplitWords(std::string_view text)
		{
			std::vector<std::string_view> words;
			size_t start = 0;
			while (start < text.size())
			{
				while (start < text.size() && (text[start] == ' ' || text[start] == '\t' || text[start] == ','))
				{
					++start;
				}
				size_t end = start;
				while (end < text.size() && text[end] != ' ' && text[end] != '\t' && text[end] != ',')
				{
					++end;
				}
				if (end > start)
				{
					words.push_back(text.substr(start, end - start));
				}
				start = end;
			}
			return words;
		}

		std::optional<float> ParseNumber(std::string_view text)
		{
			float value = 0.0f;
			const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
			if (error != std::errc() || end != text.data() + text.size() || !std::isfinite(value))
			{
				return std::nullopt;
			}
			return value;
		}

		// A colour at the start of `words`: a hex word, or four numbers. Moves `index` past it.
		std::optional<glm::vec4> ParseColorWords(std::span<const std::string_view> words, size_t& index)
		{
			if (index < words.size() && !words[index].empty() && words[index].front() == '#')
			{
				return ParseThemeColor(words[index++]);
			}
			if (index + 4 > words.size())
			{
				return std::nullopt;
			}
			glm::vec4 color;
			for (int channel = 0; channel < 4; ++channel)
			{
				const std::optional<float> number = ParseNumber(words[index + static_cast<size_t>(channel)]);
				if (!number)
				{
					return std::nullopt;
				}
				color[channel] = *number;
			}
			index += 4;
			return color;
		}

		// `count` numbers from `index` on, which must be the last words
		bool ParseNumberWords(std::span<const std::string_view> words, size_t index, std::span<float> out)
		{
			if (words.size() != index + out.size())
			{
				return false;
			}
			for (size_t i = 0; i < out.size(); ++i)
			{
				const std::optional<float> number = ParseNumber(words[index + i]);
				if (!number)
				{
					return false;
				}
				out[i] = *number;
			}
			return true;
		}

		// Reads one value's text into it; false if the text doesn't fit its kind
		bool ParseValue(const ThemeValue& value, std::string_view text, Theme& theme)
		{
			const std::vector<std::string_view> words = SplitWords(text);
			size_t index = 0;
			switch (value.Kind)
			{
			case EThemeValueKind::Color:
			{
				std::optional<glm::vec4> color;
				if (words.size() == 1)
				{
					color = ParseThemeColor(words[0]);
				}
				else if (words.size() == 3 || words.size() == 4)
				{
					float channels[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
					if (ParseNumberWords(words, 0, std::span<float>(channels, words.size())))
					{
						color = glm::vec4(channels[0], channels[1], channels[2], channels[3]);
					}
				}
				if (!color)
				{
					return false;
				}
				value.In<glm::vec4>(theme) = *color;
				return true;
			}
			case EThemeValueKind::Float:
			{
				float number = 0.0f;
				if (!ParseNumberWords(words, 0, std::span<float>(&number, 1)))
				{
					return false;
				}
				value.In<float>(theme) = number;
				return true;
			}
			case EThemeValueKind::Shadow:
			{
				const std::optional<glm::vec4> color = ParseColorWords(words, index);
				float numbers[3] = {};
				if (!color || !ParseNumberWords(words, index, numbers))
				{
					return false;
				}
				value.In<Engine::PainterShadow>(theme) = Engine::PainterShadow{ *color, { numbers[0], numbers[1] }, numbers[2] };
				return true;
			}
			case EThemeValueKind::Glow:
			{
				const std::optional<glm::vec4> color = ParseColorWords(words, index);
				float numbers[2] = {};
				if (!color || !ParseNumberWords(words, index, numbers))
				{
					return false;
				}
				value.In<Engine::PainterGlow>(theme) = Engine::PainterGlow{ *color, numbers[0], numbers[1] };
				return true;
			}
			case EThemeValueKind::Glass:
			{
				float numbers[3] = {};
				if (!ParseNumberWords(words, 0, numbers))
				{
					return false;
				}
				value.In<Engine::PainterGlass>(theme) = Engine::PainterGlass{ numbers[0], numbers[1], numbers[2] };
				return true;
			}
			}
			return false;
		}

		const char* DescribeKind(EThemeValueKind kind)
		{
			switch (kind)
			{
			case EThemeValueKind::Color:
				return "a colour (#RRGGBB, #RRGGBBAA, or 3 or 4 numbers from 0 to 1)";
			case EThemeValueKind::Float:
				return "a number";
			case EThemeValueKind::Shadow:
				return "a shadow: a colour, the offset x and y, and the blur";
			case EThemeValueKind::Glow:
				return "a glow: a colour, the distance and the strength";
			case EThemeValueKind::Glass:
				return "glass: the refraction, the edge width and the rim's strength";
			}
			return "a value";
		}

		std::string FormatNumber(float value)
		{
			return std::format("{:.5g}", value);
		}

		std::string FormatValue(const ThemeValue& value, const Theme& theme)
		{
			switch (value.Kind)
			{
			case EThemeValueKind::Color:
				return FormatThemeColor(value.In<glm::vec4>(theme));
			case EThemeValueKind::Float:
				return FormatNumber(value.In<float>(theme));
			case EThemeValueKind::Shadow:
			{
				const Engine::PainterShadow& shadow = value.In<Engine::PainterShadow>(theme);
				return FormatThemeColor(shadow.Color) + " " + FormatNumber(shadow.Offset.x) + " " + FormatNumber(shadow.Offset.y) + " " + FormatNumber(shadow.Blur);
			}
			case EThemeValueKind::Glow:
			{
				const Engine::PainterGlow& glow = value.In<Engine::PainterGlow>(theme);
				return FormatThemeColor(glow.Color) + " " + FormatNumber(glow.Distance) + " " + FormatNumber(glow.Strength);
			}
			case EThemeValueKind::Glass:
			{
				const Engine::PainterGlass& glass = value.In<Engine::PainterGlass>(theme);
				return FormatNumber(glass.Refraction) + " " + FormatNumber(glass.EdgeWidth) + " " + FormatNumber(glass.RimStrength);
			}
			}
			return {};
		}

		int HexDigit(char c)
		{
			if (c >= '0' && c <= '9')
			{
				return c - '0';
			}
			if (c >= 'a' && c <= 'f')
			{
				return c - 'a' + 10;
			}
			if (c >= 'A' && c <= 'F')
			{
				return c - 'A' + 10;
			}
			return -1;
		}

		int ToByte(float channel)
		{
			return static_cast<int>(std::lround(std::clamp(channel, 0.0f, 1.0f) * 255.0f));
		}
	}

	std::span<const ThemeValue> GetThemeValues()
	{
		return ThemeValues;
	}

	size_t GetThemeValueSize(EThemeValueKind kind)
	{
		switch (kind)
		{
		case EThemeValueKind::Color:
			return sizeof(glm::vec4);
		case EThemeValueKind::Float:
			return sizeof(float);
		case EThemeValueKind::Shadow:
			return sizeof(Engine::PainterShadow);
		case EThemeValueKind::Glow:
			return sizeof(Engine::PainterGlow);
		case EThemeValueKind::Glass:
			return sizeof(Engine::PainterGlass);
		}
		return 0;
	}

	bool AreThemesEqual(const Theme& a, const Theme& b)
	{
		// Every member is one of the values, so comparing the values compares the themes
		return std::all_of(std::begin(ThemeValues), std::end(ThemeValues), [&](const ThemeValue& value)
			{
				return std::memcmp(reinterpret_cast<const std::byte*>(&a) + value.Offset, reinterpret_cast<const std::byte*>(&b) + value.Offset,
						   GetThemeValueSize(value.Kind)) == 0;
			});
	}

	std::string FormatThemeColor(const glm::vec4& color)
	{
		const int alpha = ToByte(color.a);
		if (alpha == 255)
		{
			return std::format("#{:02X}{:02X}{:02X}", ToByte(color.r), ToByte(color.g), ToByte(color.b));
		}
		return std::format("#{:02X}{:02X}{:02X}{:02X}", ToByte(color.r), ToByte(color.g), ToByte(color.b), alpha);
	}

	std::optional<glm::vec4> ParseThemeColor(std::string_view text)
	{
		text = Trim(text);
		if (!text.empty() && text.front() == '#')
		{
			text.remove_prefix(1);
		}
		if (text.size() != 6 && text.size() != 8)
		{
			return std::nullopt;
		}
		glm::vec4 color(1.0f);
		for (size_t channel = 0; channel < text.size() / 2; ++channel)
		{
			const int high = HexDigit(text[channel * 2]);
			const int low = HexDigit(text[channel * 2 + 1]);
			if (high < 0 || low < 0)
			{
				return std::nullopt;
			}
			color[static_cast<int>(channel)] = static_cast<float>(high * 16 + low) / 255.0f;
		}
		return color;
	}

	std::string WriteThemeText(const Theme& theme)
	{
		size_t nameWidth = 0;
		for (const ThemeValue& value : ThemeValues)
		{
			nameWidth = std::max(nameWidth, std::strlen(value.Name));
		}

		std::string text = "# Nyx editor theme: one value per line, \"Name = value\". Colours are #RRGGBB or #RRGGBBAA;\n"
						   "# shadows a colour, offset x and y, and blur; glows a colour, distance and strength; glass\n"
						   "# refraction, edge width and rim strength. Values left out keep their defaults.\n";
		const char* group = nullptr;
		for (const ThemeValue& value : ThemeValues)
		{
			if (group == nullptr || std::strcmp(group, value.Group) != 0)
			{
				group = value.Group;
				text += std::format("\n[{}]\n", group);
			}
			text += std::format("{:<{}} = {}\n", value.Name, nameWidth, FormatValue(value, theme));
		}
		return text;
	}

	std::vector<std::string> ReadThemeText(std::string_view text, Theme& theme)
	{
		// The byte order mark some editors write at the start of UTF-8 files
		if (text.starts_with("\xEF\xBB\xBF"))
		{
			text.remove_prefix(3);
		}

		std::vector<std::string> problems;
		int lineNumber = 0;
		size_t start = 0;
		while (start <= text.size())
		{
			const size_t end = std::min(text.find('\n', start), text.size());
			const std::string_view line = Trim(text.substr(start, end - start));
			start = end + 1;
			++lineNumber;

			if (line.empty() || line.front() == '#' || line.front() == '[')
			{
				continue;
			}

			const size_t equals = line.find('=');
			if (equals == std::string_view::npos)
			{
				problems.push_back(std::format("Line {}: expected \"Name = value\", got \"{}\"", lineNumber, line));
				continue;
			}
			const std::string_view name = Trim(line.substr(0, equals));
			const std::string_view valueText = Trim(line.substr(equals + 1));

			const auto value = std::find_if(std::begin(ThemeValues), std::end(ThemeValues), [&](const ThemeValue& candidate)
				{
					return name == candidate.Name;
				});
			if (value == std::end(ThemeValues))
			{
				problems.push_back(std::format("Line {}: there is no theme value \"{}\"", lineNumber, name));
				continue;
			}
			if (!ParseValue(*value, valueText, theme))
			{
				problems.push_back(std::format("Line {}: {} should be {}, got \"{}\"", lineNumber, name, DescribeKind(value->Kind), valueText));
			}
		}
		return problems;
	}

	std::optional<std::string> SaveThemeFile(const std::filesystem::path& path, const Theme& theme)
	{
		std::error_code error;
		std::filesystem::create_directories(path.parent_path(), error);
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return std::format("Couldn't write \"{}\"", path.string());
		}
		file << WriteThemeText(theme);
		file.close();
		if (!file)
		{
			return std::format("Couldn't write all of \"{}\"", path.string());
		}
		return std::nullopt;
	}

	std::optional<std::vector<std::string>> LoadThemeFile(const std::filesystem::path& path, Theme& theme)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return std::nullopt;
		}
		std::stringstream contents;
		contents << file.rdbuf();
		return ReadThemeText(contents.str(), theme);
	}
}
