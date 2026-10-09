#include "ReflectionSourceRegistry.h"

namespace Nyx::Editor
{
	ReflectionSourceRegistry& ReflectionSourceRegistry::Get()
	{
		static ReflectionSourceRegistry instance;
		return instance;
	}

	void ReflectionSourceRegistry::Register(const Reflection::TypeMetadata& type, SourceLocation source)
	{
		Types[&type] = source;
	}

	void ReflectionSourceRegistry::Register(const Reflection::PropertyMetadata& property, SourceLocation source)
	{
		Properties[&property] = source;
	}

	SourceLocation ReflectionSourceRegistry::Find(const Reflection::TypeMetadata& type) const
	{
		const auto found = Types.find(&type);
		return found != Types.end() ? found->second : SourceLocation{};
	}

	SourceLocation ReflectionSourceRegistry::Find(const Reflection::PropertyMetadata& property) const
	{
		const auto found = Properties.find(&property);
		return found != Properties.end() ? found->second : SourceLocation{};
	}
}
