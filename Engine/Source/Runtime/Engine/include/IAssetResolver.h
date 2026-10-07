#pragma once

#include <string>

namespace Nyx
{
	class Mesh;
	struct Material;
}

namespace Nyx::Engine
{
	class IAssetResolver
	{
	public:
		virtual ~IAssetResolver() = default;

		virtual Nyx::Mesh* ResolveMesh(const std::string& meshId) = 0;
		virtual Nyx::Material* ResolveMaterial(const std::string& materialId) = 0;
	};
}