#pragma once

#include "SceneSerializationTypes.h"

#include <filesystem>

namespace Nyx::Engine
{
	class Registry;

	// Saves and loads all entities of a world, with every component type in the
	// ComponentTypeRegistry, which the Application fills at startup.
	class SceneSerializer
	{
	public:
		static bool SaveToFile(const Registry& world, const std::filesystem::path& path);

		// Replaces everything in outWorld with the scene from the file. The context's asset
		// resolver turns the asset paths stored in components into loaded assets.
		static bool LoadFromFile(
			const std::filesystem::path& path,
			Registry& outWorld,
			ScenePostLoadContext postLoadContext = {});
	};
}