#pragma once

#include "NameComponent.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"

#include <optional>

namespace Nyx::Editor
{
	struct SceneEntitySnapshot
	{
		bool bAlive = false;

		std::optional<Nyx::Engine::NameComponent> Name;
		std::optional<Nyx::Engine::TransformComponent> Transform;
		std::optional<Nyx::Engine::MeshRendererComponent> MeshRenderer;
	};
}