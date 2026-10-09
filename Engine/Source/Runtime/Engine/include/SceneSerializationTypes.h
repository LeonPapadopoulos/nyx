#pragma once

#include <cstdint>

namespace Nyx::Engine
{
	inline constexpr uint32_t SceneFileMagic = 0x53434E45; // 'SCNE'

	// Version 1: property values without names, in declaration order. Still loaded.
	// Version 2: properties with name hash and kind (see ReflectedArchiveSerializer).
	inline constexpr uint32_t SceneFileVersion = 2;

	class IAssetResolver;

	struct ScenePostLoadContext
	{
		IAssetResolver* AssetResolver = nullptr;
	};
}