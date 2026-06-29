#pragma once

#include "SceneSerializationTypes.h"

#include <filesystem>

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{

	class SceneSerializer
	{
	public:
		static bool SaveToFile(
			const Nyx::SceneDocument& scene,
			const std::filesystem::path& path);

		static bool LoadFromFile(
			const std::filesystem::path& path,
			Nyx::SceneDocument& outScene,
			Nyx::Engine::ScenePostLoadContext postLoadContext = {});
	};
}