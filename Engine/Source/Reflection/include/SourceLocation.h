#pragma once

#include <cstdint>
#include <source_location>

namespace Nyx
{
	// Development information only; never written to a scene or a link message.
	struct SourceLocation
	{
		const char* File = "";
		uint32_t Line = 0;

		constexpr SourceLocation() = default;
		constexpr SourceLocation(const char* file, uint32_t line) : File(file), Line(line) {}
		constexpr SourceLocation(std::source_location location) : File(location.file_name()), Line(location.line()) {}

		constexpr bool IsValid() const { return File && File[0] && Line != 0; }
		static constexpr SourceLocation Current(std::source_location location = std::source_location::current())
		{
			return { location.file_name(), location.line() };
		}
	};
}
