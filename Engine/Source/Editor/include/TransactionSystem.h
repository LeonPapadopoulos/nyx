#pragma once

#include "EditorTransactionContext.h"
#include "TransactionTypes.h"

#include <vector>

namespace Nyx::Editor
{
	struct ITransactionObjectResolver
	{
		virtual ~ITransactionObjectResolver() = default;

		virtual void* ResolveMutable(
			EditorTransactionContext& context,
			InspectorTargetId targetId,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;
	};

	struct ITransactionSubscriber
	{
		virtual ~ITransactionSubscriber() = default;
		virtual void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) = 0;
	};

	class TransactionSystem
	{
	public:
		explicit TransactionSystem(ITransactionObjectResolver& resolver)
			: Resolver(resolver)
		{
		}

		void Push(Transaction&& transaction);

		bool Undo(EditorTransactionContext& context);
		bool Redo(EditorTransactionContext& context);

		void Clear();

		void Subscribe(ITransactionSubscriber* subscriber);

	private:
		void ApplyTransaction(EditorTransactionContext& context, const Transaction& transaction, bool bRedo);
		void ApplyChange(EditorTransactionContext& context, const Change& change, bool bRedo);
		void ApplySetValueChange(EditorTransactionContext& context, const SetValueChange& change, bool bRedo);

	private:
		ITransactionObjectResolver& Resolver;
		std::vector<Transaction> UndoStack;
		std::vector<Transaction> RedoStack;
		std::vector<ITransactionSubscriber*> Subscribers;
	};
}