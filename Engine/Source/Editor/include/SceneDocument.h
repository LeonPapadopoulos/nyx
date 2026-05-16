#pragma once

#include "Entity.h"
//#include "NameComponent.h" // @todo

#include <optional>
#include <string>

// @todo: Eventually move to own file
namespace Nyx::Engine
{
	struct NameComponent
	{
		std::string Name = "Entity";
	};
}

namespace Nyx::Engine
{
	class Registry;
}

namespace Nyx
{
	class SceneDocument
	{
	public:
		SceneDocument() = default;

		Nyx::Engine::Registry& GetRegistry() { return Registry; }
		const Nyx::Engine::Registry& GetRegistry() const { return Registry; }

		Nyx::Engine::Entity CreateEntity(const std::string& name = "Entity");
		bool DestroyEntity(Nyx::Engine::Entity entity);

		std::optional<Nyx::Engine::Entity>& GetSelection() { return SelectedEntity; }
		const std::optional<Nyx::Engine::Entity>& GetSelection() const { return SelectedEntity; }

	private:
		Nyx::Engine::Registry Registry;
		std::optional<Nyx::Engine::Entity> SelectedEntity;
	};
}