#pragma once

#include "IAssetResolver.h"
#include "Prefab.h"

namespace Nyx
{
	class IRenderer;
}

namespace Nyx
{
	// Resolves asset paths to what the renderer draws: materials from their .nyxmat files in the
	// assets folder, and the built-in cube and sphere meshes until meshes are assets of their own.
	class AssetResolver final : public IAssetResolver
	{
	public:
		explicit AssetResolver(Nyx::IRenderer& renderer);

		Nyx::Mesh* ResolveMesh(const std::string& meshPath) override;
		Nyx::Material* ResolveMaterial(const std::string& materialPath) override;
		Nyx::PrefabAsset* ResolvePrefab(const std::string& prefabPath) override;
		void ForgetPrefab(const std::string& prefabPath) override;

		// The prefabs read so far, e.g. to read one again after its file changed
		Nyx::PrefabLibrary& GetPrefabs()
		{
			return Prefabs;
		}

	private:
		Nyx::IRenderer& Renderer;
		Nyx::PrefabLibrary Prefabs;
	};
}
