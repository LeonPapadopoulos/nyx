#pragma once

#include "EditorProperty.h"
#include "InspectorDrawContext.h"

#include <vector>

namespace Nyx::Editor
{
	template<typename T>
	struct ComponentEditorMeta
	{
		static constexpr bool Enabled = false;

		static const char* GetDisplayName()
		{
			return "Unknown";
		}

		static const std::vector<Nyx::Editor::EditorPropertyDesc>& GetProperties()
		{
			static const std::vector<Nyx::Editor::EditorPropertyDesc> EmptyProperties{};
			return EmptyProperties;
		}

		static void DrawExtra(T&, Nyx::Editor::InspectorDrawContext&)
		{
		}
	};
}

#define NYX_EDITOR_FIELD(ComponentType, Member, KindValue, DisplayNameValue, DragSpeedValue) \
	Nyx::Editor::EditorPropertyDesc{ DisplayNameValue, Nyx::Editor::EEditorPropertyKind::KindValue, offsetof(ComponentType, Member), DragSpeedValue }

#define NYX_REGISTER_COMPONENT_EDITOR(ComponentType, DisplayNameValue, ...)                     \
	template<>                                                                                  \
	struct Nyx::Editor::ComponentEditorMeta<ComponentType>                                      \
	{                                                                                           \
		static constexpr bool Enabled = true;                                                   \
		                                                                                        \
		static const char* GetDisplayName()                                                     \
		{                                                                                       \
			return DisplayNameValue;                                                            \
		}                                                                                       \
		                                                                                        \
		static const std::vector<Nyx::Editor::EditorPropertyDesc>& GetProperties()              \
		{                                                                                       \
			static const std::vector<Nyx::Editor::EditorPropertyDesc> Properties =              \
			{                                                                                   \
				__VA_ARGS__                                                                     \
			};                                                                                  \
			return Properties;                                                                  \
		}                                                                                       \
		                                                                                        \
		static void DrawExtra(ComponentType&, Nyx::Editor::InspectorDrawContext&)               \
		{                                                                                       \
		}                                                                                       \
	};