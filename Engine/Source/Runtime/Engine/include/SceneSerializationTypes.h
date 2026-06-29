#pragma once

#include <cstdint>

namespace Nyx::Engine
{
	inline constexpr uint32_t SceneFileMagic = 0x53434E45; // 'SCNE'
	inline constexpr uint32_t SceneFileVersion = 1;

	class IAssetResolver;

	struct ScenePostLoadContext
	{
		IAssetResolver* AssetResolver = nullptr;
	};
}