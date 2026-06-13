#pragma once

#include "PropertyValue.h"
#include "PropertyValueUtils.h"

#include <vector>

namespace Nyx::Editor
{
	template<typename TObject>
	struct EditablePropertyDesc
	{
		const char* Name = "";
		Nyx::Reflection::PropertyValue(*GetValue)(const TObject&) = nullptr;
		void (*SetValue)(TObject&, const Nyx::Reflection::PropertyValue&) = nullptr;
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
	Nyx::Reflection::PropertyValue GetMemberEditorValue(const TObject& object)
	{
		return object.*Member;
	}

	template<typename TObject, typename TValue, TValue TObject::* Member>
	void SetMemberEditorValue(TObject& object, const Nyx::Reflection::PropertyValue& value)
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