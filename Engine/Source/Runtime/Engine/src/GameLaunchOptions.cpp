#include "GameLaunchOptions.h"

#include <charconv>
#include <limits>
#include <system_error>
#include <utility>

namespace
{
	// The largest window side the game accepts; Vulkan devices allow at least this much
	constexpr uint32_t MaxWindowSide = 16384;

	// The whole text has to be the number
	template <typename TNumber>
	std::optional<TNumber> ParseNumber(std::string_view text)
	{
		TNumber number{};
		const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), number);
		if (text.empty() || error != std::errc() || end != text.data() + text.size())
		{
			return std::nullopt;
		}

		return number;
	}

	// "<first><separator><second>", e.g. "1280x800" or "-1920,0"
	template <typename TNumber>
	std::optional<std::pair<TNumber, TNumber>> ParsePair(std::string_view text, char separator)
	{
		const size_t separatorAt = text.find(separator);
		if (separatorAt == std::string_view::npos)
		{
			return std::nullopt;
		}

		const std::optional<TNumber> first = ParseNumber<TNumber>(text.substr(0, separatorAt));
		const std::optional<TNumber> second = ParseNumber<TNumber>(text.substr(separatorAt + 1));
		if (!first || !second)
		{
			return std::nullopt;
		}

		return std::pair<TNumber, TNumber>{ *first, *second };
	}

	std::optional<uint16_t> ParsePort(std::string_view text)
	{
		const std::optional<uint32_t> port = ParseNumber<uint32_t>(text);
		if (!port || *port == 0 || *port > std::numeric_limits<uint16_t>::max())
		{
			return std::nullopt;
		}

		return static_cast<uint16_t>(*port);
	}

	std::optional<Nyx::Engine::GameWindowSize> ParseWindowSize(std::string_view text)
	{
		// Both "1280x800" and "1280X800"
		std::string lowerCase(text);
		for (char& c : lowerCase)
		{
			c = c == 'X' ? 'x' : c;
		}

		const std::optional<std::pair<uint32_t, uint32_t>> size = ParsePair<uint32_t>(lowerCase, 'x');
		if (!size || size->first == 0 || size->second == 0 || size->first > MaxWindowSide || size->second > MaxWindowSide)
		{
			return std::nullopt;
		}

		return Nyx::Engine::GameWindowSize{ size->first, size->second };
	}

	std::optional<Nyx::Engine::GameWindowPosition> ParseWindowPosition(std::string_view text)
	{
		const std::optional<std::pair<int32_t, int32_t>> position = ParsePair<int32_t>(text, ',');
		if (!position)
		{
			return std::nullopt;
		}

		return Nyx::Engine::GameWindowPosition{ position->first, position->second };
	}

	// Paths go to the started program as UTF-8, which is what ChildProcess::Start() expects
	std::string ToUtf8(const std::filesystem::path& path)
	{
		const std::u8string text = path.u8string();
		return std::string(text.begin(), text.end());
	}
}

namespace Nyx::Engine
{
	std::vector<std::string> MakeGameArguments(const GameLaunchOptions& options)
	{
		std::vector<std::string> arguments;

		if (!options.ScenePath.empty())
		{
			arguments.push_back(ToUtf8(options.ScenePath));
		}

		if (options.bWaitForDebugger)
		{
			arguments.push_back("--wait-for-debugger");
		}

		if (options.EditorPort)
		{
			arguments.push_back("--editor-port");
			arguments.push_back(std::to_string(*options.EditorPort));
		}

		if (!options.LinkLogPath.empty())
		{
			arguments.push_back("--link-log");
			arguments.push_back(ToUtf8(options.LinkLogPath));
		}

		if (options.WindowSize)
		{
			arguments.push_back("--window-size");
			arguments.push_back(std::to_string(options.WindowSize->Width) + "x" + std::to_string(options.WindowSize->Height));
		}

		if (options.WindowPosition)
		{
			arguments.push_back("--window-pos");
			arguments.push_back(std::to_string(options.WindowPosition->X) + "," + std::to_string(options.WindowPosition->Y));
		}

		if (!options.WindowTitle.empty())
		{
			arguments.push_back("--window-title");
			arguments.push_back(options.WindowTitle);
		}

		return arguments;
	}

	GameLaunchOptions ParseGameArguments(const std::vector<std::string_view>& arguments, std::vector<std::string>& outWarnings)
	{
		GameLaunchOptions options;

		for (size_t i = 0; i < arguments.size(); ++i)
		{
			const std::string_view argument = arguments[i];

			// The value after an option, if there is one
			const auto takeValue = [&]() -> std::optional<std::string_view>
			{
				if (i + 1 < arguments.size())
				{
					return arguments[++i];
				}

				return std::nullopt;
			};

			if (argument == "--wait-for-debugger")
			{
				options.bWaitForDebugger = true;
			}
			else if (argument == "--editor-port")
			{
				const std::optional<std::string_view> value = takeValue();
				options.EditorPort = value ? ParsePort(*value) : std::nullopt;
				if (!options.EditorPort)
				{
					outWarnings.push_back("--editor-port needs a port number from 1 to 65535; the game runs without the editor link");
				}
			}
			else if (argument == "--link-log")
			{
				if (const std::optional<std::string_view> value = takeValue())
				{
					options.LinkLogPath = std::string(*value);
				}
				else
				{
					outWarnings.push_back("--link-log needs a file name");
				}
			}
			else if (argument == "--window-size")
			{
				const std::optional<std::string_view> value = takeValue();
				options.WindowSize = value ? ParseWindowSize(*value) : std::nullopt;
				if (!options.WindowSize)
				{
					outWarnings.push_back("--window-size needs <width>x<height>, each from 1 to " + std::to_string(MaxWindowSide) +
						", e.g. 1280x800; the window gets its default size");
				}
			}
			else if (argument == "--window-pos")
			{
				const std::optional<std::string_view> value = takeValue();
				options.WindowPosition = value ? ParseWindowPosition(*value) : std::nullopt;
				if (!options.WindowPosition)
				{
					outWarnings.push_back("--window-pos needs <x>,<y>, e.g. 100,50 or -1920,0; the system places the window");
				}
			}
			else if (argument == "--window-title")
			{
				if (const std::optional<std::string_view> value = takeValue())
				{
					options.WindowTitle = std::string(*value);
				}
				else
				{
					outWarnings.push_back("--window-title needs a text");
				}
			}
			else if (argument.starts_with("--"))
			{
				outWarnings.push_back("Unknown option '" + std::string(argument) + "' ignored");
			}
			else
			{
				// Like the arguments the game gets from main(): in the system's code page
				options.ScenePath = std::string(argument);
			}
		}

		return options;
	}
}
