#pragma once

#include "ReflectionTypes.h"
#include "SourceLocation.h"
#include <unordered_map>

namespace Nyx::Editor
{
	// Declaration locations belong to editor tooling, never to runtime reflection records.
	class ReflectionSourceRegistry
	{
	public:
		static ReflectionSourceRegistry& Get();

		void Register(const Reflection::TypeMetadata& type, SourceLocation source);
		void Register(const Reflection::PropertyMetadata& property, SourceLocation source);
		SourceLocation Find(const Reflection::TypeMetadata& type) const;
		SourceLocation Find(const Reflection::PropertyMetadata& property) const;

	private:
		std::unordered_map<const Reflection::TypeMetadata*, SourceLocation> Types;
		std::unordered_map<const Reflection::PropertyMetadata*, SourceLocation> Properties;
	};

	// Defined by NyxHeaderTool in Runtime.reflect.sources.cpp; linked only by editor targets.
	void RegisterRuntimeReflectedSources();
}
