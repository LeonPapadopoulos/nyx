#include "TransactionSystem.h"
#include "ReflectedPropertyAccess.h"

namespace Nyx::Editor
{
	void TransactionSystem::Push(Transaction&& transaction)
	{
		if (transaction.IsEmpty())
		{
			return;
		}

		UndoStack.push_back(std::move(transaction));
		RedoStack.clear();
	}

	bool TransactionSystem::Undo(EditorTransactionContext& context)
	{
		if (UndoStack.empty())
		{
			return false;
		}

		Transaction transaction = std::move(UndoStack.back());
		UndoStack.pop_back();

		ApplyTransaction(context, transaction, false);
		RedoStack.push_back(std::move(transaction));
		return true;
	}

	bool TransactionSystem::Redo(EditorTransactionContext& context)
	{
		if (RedoStack.empty())
		{
			return false;
		}

		Transaction transaction = std::move(RedoStack.back());
		RedoStack.pop_back();

		ApplyTransaction(context, transaction, true);
		UndoStack.push_back(std::move(transaction));
		return true;
	}

	void TransactionSystem::Clear()
	{
		UndoStack.clear();
		RedoStack.clear();
	}

	void TransactionSystem::Subscribe(ITransactionSubscriber* subscriber)
	{
		if (!subscriber)
		{
			return;
		}

		Subscribers.push_back(subscriber);
	}

	void TransactionSystem::ApplyTransaction(EditorTransactionContext& context, const Transaction& transaction, bool bRedo)
	{
		if (bRedo)
		{
			for (const Change& change : transaction.Changes)
			{
				ApplyChange(context, change, true);
			}
		}
		else
		{
			for (auto it = transaction.Changes.rbegin(); it != transaction.Changes.rend(); ++it)
			{
				ApplyChange(context, *it, false);
			}
		}

		for (ITransactionSubscriber* subscriber : Subscribers)
		{
			subscriber->OnTransactionApplied(transaction, !bRedo);
		}
	}

	void TransactionSystem::ApplyChange(EditorTransactionContext& context, const Change& change, bool bRedo)
	{
		switch (change.Kind)
		{
		case EChangeKind::SetValue:
			ApplySetValueChange(context, std::get<SetValueChange>(change.Payload), bRedo);
			break;

		case EChangeKind::AddObject:
			// Next step.
			break;

		case EChangeKind::DeleteObject:
			// Next step.
			break;

		default:
			break;
		}
	}

	void TransactionSystem::ApplySetValueChange(EditorTransactionContext& context, const SetValueChange& change, bool bRedo)
	{
		if (!change.TypeMetadata || !change.TypeMetadata->Properties)
		{
			return;
		}

		void* object = Resolver.ResolveMutable(context, change.TargetId, *change.TypeMetadata);
		if (!object)
		{
			return;
		}

		if (change.PropertyIndex >= change.TypeMetadata->PropertyCount)
		{
			return;
		}

		const Nyx::Reflection::PropertyMetadata& property = change.TypeMetadata->Properties[change.PropertyIndex];
		WriteReflectedPropertyValue(object, property, bRedo ? change.After : change.Before);
	}
}