#pragma once

#include "EditorTransactionContext.h"
#include "InspectorTargetId.h"

namespace Nyx::Editor
{
	template<typename TObject>
	struct EditableTargetResolver
	{
		static constexpr bool Enabled = false;

		static TObject* Resolve(EditorTransactionContext&, InspectorTargetId)
		{
			return nullptr;
		}
	};
}