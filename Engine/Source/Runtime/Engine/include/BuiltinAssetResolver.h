#pragma once

#include "IAssetResolver.h"

namespace Nyx
{
	class IRenderer;
}

namespace Nyx::Engine
{
	// Resolves the asset paths of the built-in test assets (a cube mesh and three materials)
	// to the instances the renderer creates. Stands in until there is a real asset system.
	class BuiltinAssetResolver final : public IAssetResolver
	{
	public:
		explicit BuiltinAssetResolver(Nyx::IRenderer& renderer);

		Nyx::Mesh* ResolveMesh(const std::string& meshPath) override;
		Nyx::Material* ResolveMaterial(const std::string& materialPath) override;

	private:
		Nyx::IRenderer& Renderer;
	};
}
