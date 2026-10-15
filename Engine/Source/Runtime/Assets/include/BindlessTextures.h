#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace Nyx
{
	// Texture slots every program can count on, made by VulkanBindless
	enum class EBindlessTexture : uint32_t
	{
		White = 0,
		Black = 1,
		// (0.5, 0.5, 1): a normal map that changes nothing
		FlatNormal = 2,
		// A magenta checker, for a texture that is missing
		Missing = 3,
	};

	// The slot a material field's Default(...) attribute names, e.g. "FlatNormal"
	inline std::optional<EBindlessTexture> FindBindlessTexture(std::string_view name)
	{
		if (name == "White")
		{
			return EBindlessTexture::White;
		}
		if (name == "Black")
		{
			return EBindlessTexture::Black;
		}
		if (name == "FlatNormal")
		{
			return EBindlessTexture::FlatNormal;
		}
		if (name == "Missing")
		{
			return EBindlessTexture::Missing;
		}
		return std::nullopt;
	}
}
