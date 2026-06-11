#pragma once

#include "EditorValue.h"

#include <vector>

namespace Nyx::Editor
{
	template<typename TObject>
	struct EditablePropertyDesc
	{
		const char* Name = "";
		EditorValue(*GetValue)(const TObject&) = nullptr;
		void (*SetValue)(TObject&, const EditorValue&) = nullptr;
	};

	template<typename TObject>
	struct EditableObjectTraits
	{
		static constexpr bool Enabled = false;

		static const std::vector<EditablePropertyDesc<TObject>>& GetProperties()
		{
			static const std::vector<EditablePropertyDesc<TObject>> EmptyProperties{};
			return EmptyProperties;
		}
	};

	template<typename TObject, typename TValue, TValue TObject::* Member>
	EditorValue GetMemberEditorValue(const TObject& object)
	{
		return object.*Member;
	}

	template<typename TObject, typename TValue, TValue TObject::* Member>
	void SetMemberEditorValue(TObject& object, const EditorValue& value)
	{
		object.*Member = std::get<TValue>(value);
	}

#define NYX_EDITABLE_MEMBER_PROPERTY(TObject, Member, TValue) \
	Nyx::Editor::EditablePropertyDesc<TObject>{                \
		#Member,                                               \
		&Nyx::Editor::GetMemberEditorValue<TObject, TValue, &TObject::Member>, \
		&Nyx::Editor::SetMemberEditorValue<TObject, TValue, &TObject::Member>  \
	}
}