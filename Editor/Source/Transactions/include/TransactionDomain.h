#pragma once

#include "EditorTransactionContext.h"
#include "ReflectedObjectSnapshot.h"
#include "TransactionObjectRef.h"
#include "TransactionTypes.h"

#include <vector>

namespace Nyx::Editor
{
	struct ReflectedObjectView
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		void* Object = nullptr;
	};

	struct ITransactionDomain
	{
		virtual ~ITransactionDomain() = default;

		// Used by SetValue changes
		virtual void* ResolveMutable(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;

		// Used by AddObject / DeleteObject changes
		virtual bool CreateRootObject(
			EditorTransactionContext& context,
			const ObjectRef& root) = 0;

		virtual bool DeleteRootObject(
			EditorTransactionContext& context,
			const ObjectRef& root) = 0;

		// Used by generic root snapshot capture/restore
		virtual void EnumerateSubobjects(
			EditorTransactionContext& context,
			const ObjectRef& root,
			std::vector<ReflectedObjectView>& outSubobjects) = 0;

		virtual bool EnsureSubobject(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;

		virtual bool RemoveSubobject(
			EditorTransactionContext& context,
			const ObjectRef& root,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;
	};

	struct ITransactionSubscriber
	{
		virtual ~ITransactionSubscriber() = default;

		// A new edit was recorded (TransactionSystem::Push). Its changes are already made.
		virtual void OnTransactionCommitted(const Transaction& /*transaction*/)
		{
		}

		// Undo or redo has just changed the objects back or again
		virtual void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) = 0;

		// Changes made but not recorded yet, e.g. during a drag (TransactionSystem::Preview). Each
		// change's Before is the value from the last preview, not from before the drag.
		virtual void OnTransactionPreviewed(const Transaction& /*transaction*/)
		{
		}
	};
}