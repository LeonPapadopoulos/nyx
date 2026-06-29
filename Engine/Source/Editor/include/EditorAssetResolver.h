#pragma once

#include "IAssetResolver.h"

namespace Nyx
{
	class VulkanRenderer;
}

namespace Nyx::Editor
{
	class EditorAssetResolver final : public Nyx::Engine::IAssetResolver
	{
	public:
		explicit EditorAssetResolver(Nyx::VulkanRenderer& renderer)
			: Renderer(renderer)
		{
		}

		Nyx::Mesh* ResolveMesh(const std::string& meshId) override;
		Nyx::Material* ResolveMaterial(const std::string& materialId) override;

	private:
		Nyx::VulkanRenderer& Renderer;
	};
}