#pragma once

#include "IAssetResolver.h"

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

	private:
		Nyx::IRenderer& Renderer;
	};
}
