#pragma once

#include "ReflectedPropertyAccess.h"
#include "TransactionDomain.h"

#include <unordered_map>
#include <vector>

namespace Nyx::Editor
{
	class TransactionSystem
	{
	public:
		void RegisterDomain(EObjectDomain domain, ITransactionDomain* handler);

		void Push(Transaction&& transaction);

		bool Undo(EditorTransactionContext& context);
		bool Redo(EditorTransactionContext& context);

		void Clear();

		void Subscribe(ITransactionSubscriber* subscriber);

	private:
		void ApplyTransaction(EditorTransactionContext& context, const Transaction& transaction, bool bRedo);
		void ApplyChange(EditorTransactionContext& context, const Change& change, bool bRedo);

		void ApplySetValueChange(EditorTransactionContext& context, const SetValueChange& change, bool bRedo);
		void ApplyAddObjectChange(EditorTransactionContext& context, const AddObjectChange& change, bool bRedo);
		void ApplyDeleteObjectChange(EditorTransactionContext& context, const DeleteObjectChange& change, bool bRedo);

		ITransactionDomain* FindDomain(EObjectDomain domain);

	private:
		std::unordered_map<EObjectDomain, ITransactionDomain*> Domains;
		std::vector<Transaction> UndoStack;
		std::vector<Transaction> RedoStack;
		std::vector<ITransactionSubscriber*> Subscribers;
	};
}