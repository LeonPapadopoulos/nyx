#include "ComponentTypeRegistry.h"
#include "Log.h"

#include <string>

namespace Nyx
{
	ComponentTypeRegistry& ComponentTypeRegistry::Get()
	{
		static ComponentTypeRegistry Instance;
		return Instance;
	}

	bool ComponentTypeRegistry::CanRegister(const Nyx::Reflection::TypeMetadata& typeMetadata) const
	{
		if (FindByTypeMetadata(typeMetadata))
		{
			return false;
		}

		// Scene files, undo and the editor link name component types by name: two types of one name
		// would load into each other
		if (FindByName(typeMetadata.Name))
		{
			LOG_ERROR("Two component types are named '{0}': the second is ignored. Rename one of them, e.g. "
					  "by putting a game module's types into a namespace of their own.",
				typeMetadata.Name);
			return false;
		}

		return true;
	}

	bool ComponentTypeRegistry::IsOwnRuntimeType(std::string_view name) const
	{
		const ComponentTypeOps* ops = FindByName(name);
		if (!ops)
		{
			return false;
		}
		for (const Nyx::Reflection::TypeMetadata& metadata : OwnMetadata)
		{
			if (&metadata == ops->TypeMetadata)
			{
				return true;
			}
		}
		return false;
	}

	ComponentTypeOps* ComponentTypeRegistry::FindRegistered(const Nyx::Reflection::TypeMetadata& typeMetadata, const char* what)
	{
		for (ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == &typeMetadata)
			{
				return &ops;
			}
		}

		LOG_ERROR("Component type '{0}' isn't registered, {1} is ignored. Register it first.", typeMetadata.Name, what);
		ASSERT(false && "Register the component type first.");
		return nullptr;
	}

	const ComponentTypeOps* ComponentTypeRegistry::FindByTypeMetadata(
		const Nyx::Reflection::TypeMetadata& typeMetadata) const
	{
		for (const ComponentTypeOps& ops : Types)
		{
			if (ops.TypeMetadata == &typeMetadata)
			{
				return &ops;
			}
		}

		return nullptr;
	}

	const ComponentTypeOps* ComponentTypeRegistry::FindByName(std::string_view name) const
	{
		// Scene files and link recordings saved while the components were in Nyx::Engine name them
		// "Nyx::Engine::<Type>"; they are "Nyx::<Type>" now
		constexpr std::string_view OldNamespace = "Nyx::Engine::";
		if (name.starts_with(OldNamespace))
		{
			return FindByName("Nyx::" + std::string(name.substr(OldNamespace.size())));
		}

		for (const ComponentTypeOps& ops : Types)
		{
			if (std::string_view(ops.TypeMetadata->Name) == name)
			{
				return &ops;
			}
		}

		return nullptr;
	}
}
