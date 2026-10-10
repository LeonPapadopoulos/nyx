#include "EditorPCH.h"
#include "InspectorDrawContext.h"

namespace Nyx::Editor
{
	void InspectorDrawContext::CancelPendingEdits()
	{
		CurrentTargetId = {};
		CurrentObjectRef = {};
		CurrentLocation = {};
		GenericPropertyEdit = {};
		TransformRotationEdit = {};
	}
}
