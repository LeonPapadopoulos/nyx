#include "EditorPCH.h"
#include "InspectorDrawContext.h"

namespace Nyx::Editor
{
	void InspectorDrawContext::CancelPendingEdits()
	{
		CurrentObjectRef = {};
		CurrentLocation = {};
		CurrentPath.clear();
		GenericPropertyEdit = {};
		TransformRotationEdit = {};
		OverrideEdit = {};
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
		if (OverrideEdit.bEditing && OverrideEdit.PendingDiff)
		{
			OverrideEdit.PendingDiff->PreviewChanges("Edit Property", *Transactions);
		}
	}
}
