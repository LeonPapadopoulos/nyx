#pragma once

#include "InspectorDrawContext.h"
#include "ReflectionTypes.h"

namespace Nyx::Editor
{
	struct PropertyWidgetArgs
	{
		void* Object = nullptr;
		const Nyx::Reflection::PropertyMetadata* Property = nullptr;
		const Nyx::Reflection::TypeMetadata* OwnerType = nullptr;
		Nyx::Editor::InspectorDrawContext* DrawContext = nullptr;
	};

	using PropertyWidgetDrawFn = bool(*)(const PropertyWidgetArgs& args);

	class PropertyWidgetRegistry
	{
	public:
		static PropertyWidgetRegistry& Get();

		void Register(Nyx::Reflection::EPropertyKind kind, PropertyWidgetDrawFn drawFn);
		PropertyWidgetDrawFn Find(Nyx::Reflection::EPropertyKind kind) const;

	private:
		PropertyWidgetRegistry() = default;
	};

	void RegisterDefaultPropertyWidgets();
}