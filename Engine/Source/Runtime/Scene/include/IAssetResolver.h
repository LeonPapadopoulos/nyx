#pragma once

#include <string>

namespace Nyx
{
	class Mesh;
	struct Material;
	class PrefabAsset;
}

namespace Nyx
{
	class IAssetResolver
	{
	public:
		virtual ~IAssetResolver() = default;

		virtual Nyx::Mesh* ResolveMesh(const std::string& meshId) = 0;
		virtual Nyx::Material* ResolveMaterial(const std::string& materialId) = 0;

		// The prefab at the path ("Prefabs/Wall.nyxprefab"), which instances are made from and saved
		// against; null if there is none, as without a prefab library
		virtual const Nyx::PrefabAsset* ResolvePrefab(const std::string& /*prefabPath*/)
		{
			return nullptr;
		}

		// Reads the prefab again the next time it is resolved, e.g. after its file was written
		virtual void ForgetPrefab(const std::string& /*prefabPath*/)
		{
		}
	};
}