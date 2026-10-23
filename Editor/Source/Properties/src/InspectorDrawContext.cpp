#include "EditorPCH.h"
#include "InspectorDrawContext.h"

namespace Nyx::Editor
{
	void InspectorDrawContext::CancelPendingEdits()
	{
		CurrentObjectRef = {};
		CurrentLocation = {};
		GenericPropertyEdit = {};
		TransformRotationEdit = {};
		EntityToSelect.reset();
		EntityPick = {};
	}

	void InspectorDrawContext::PreviewPendingEdits()
	{
		if (!Transactions)
		{
			return;
		}

		for (PropertyEditTransactionState* edit : { static_cast<PropertyEditTransactionState*>(&GenericPropertyEdit),
				 static_cast<PropertyEditTransactionState*>(&TransformRotationEdit) })
		{
			if (edit->bEditing && edit->PendingDiff)
			{
				edit->PendingDiff->PreviewChanges("Edit Property", *Transactions);
			}
		}
	}
}
