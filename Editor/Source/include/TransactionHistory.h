#pragma once

#include "EditorTransactionContext.h"

#include <memory>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	struct IChange
	{
		virtual ~IChange() = default;
		virtual void Undo(EditorTransactionContext& context) = 0;
		virtual void Redo(EditorTransactionContext& context) = 0;
	};

	struct Transaction
	{
		std::string Label;
		std::vector<std::unique_ptr<IChange>> Changes;

		bool IsEmpty() const
		{
			return Changes.empty();
		}
	};

	class TransactionHistory
	{
	public:
		void Push(Transaction&& transaction)
		{
			if (transaction.IsEmpty())
			{
				return;
			}

			UndoStack.push_back(std::move(transaction));
			RedoStack.clear();
		}

		bool Undo(EditorTransactionContext& context)
		{
			if (UndoStack.empty())
			{
				return false;
			}

			Transaction transaction = std::move(UndoStack.back());
			UndoStack.pop_back();

			for (auto it = transaction.Changes.rbegin(); it != transaction.Changes.rend(); ++it)
			{
				(*it)->Undo(context);
			}

			RedoStack.push_back(std::move(transaction));
			return true;
		}

		bool Redo(EditorTransactionContext& context)
		{
			if (RedoStack.empty())
			{
				return false;
			}

			Transaction transaction = std::move(RedoStack.back());
			RedoStack.pop_back();

			for (const std::unique_ptr<IChange>& change : transaction.Changes)
			{
				change->Redo(context);
			}

			UndoStack.push_back(std::move(transaction));
			return true;
		}

		void Clear()
		{
			UndoStack.clear();
			RedoStack.clear();
		}

	private:
		std::vector<Transaction> UndoStack;
		std::vector<Transaction> RedoStack;
	};
}