#pragma once

#include "EditorTransactionContext.h"
#include "TransactionTypes.h"

namespace Nyx::Editor
{
	struct ITransactionDomain
	{
		virtual ~ITransactionDomain() = default;

		virtual void* ResolveMutable(
			EditorTransactionContext& context,
			const ObjectRef& objectRef,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;

		virtual bool CreateObject(
			EditorTransactionContext& context,
			const AddObjectChange& change) = 0;

		virtual bool DeleteObject(
			EditorTransactionContext& context,
			const DeleteObjectChange& change) = 0;
	};

	struct ITransactionSubscriber
	{
		virtual ~ITransactionSubscriber() = default;
		virtual void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) = 0;
	};
}