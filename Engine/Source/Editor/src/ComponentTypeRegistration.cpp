#include "ComponentTypeRegistration.h"

#include "ComponentTypeRegistry.h"
#include "NameComponent.h"
#include "TransformComponent.h"
#include "MeshRendererComponent.h"

namespace Nyx::Editor
{
	void RegisterDefaultComponentTypes()
	{
		auto& registry = ComponentTypeRegistry::Get();

		registry.Register(MakeComponentTypeOps<Nyx::Engine::NameComponent>());
		registry.Register(MakeComponentTypeOps<Nyx::Engine::TransformComponent>());
		registry.Register(MakeComponentTypeOps<Nyx::Engine::MeshRendererComponent>());
	}
}