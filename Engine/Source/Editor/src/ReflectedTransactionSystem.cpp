#include "ReflectedTransactionSystem.h"
#include "ReflectedPropertyAccess.h"

namespace Nyx::Editor
{
	void ReflectedTransactionHistory::Push(ReflectedTransaction&& transaction)
	{
		if (transaction.IsEmpty())
		{
			return;
		}

		UndoStack.push_back(std::move(transaction));
		RedoStack.clear();
	}

	bool ReflectedTransactionHistory::Undo(EditorTransactionContext& context)
	{
		if (UndoStack.empty())
		{
			return false;
		}

		ReflectedTransaction transaction = std::move(UndoStack.back());
		UndoStack.pop_back();

		ApplyTransaction(context, transaction, false);
		RedoStack.push_back(std::move(transaction));
		return true;
	}

	bool ReflectedTransactionHistory::Redo(EditorTransactionContext& context)
	{
		if (RedoStack.empty())
		{
			return false;
		}

		ReflectedTransaction transaction = std::move(RedoStack.back());
		RedoStack.pop_back();

		ApplyTransaction(context, transaction, true);
		UndoStack.push_back(std::move(transaction));
		return true;
	}

	void ReflectedTransactionHistory::Clear()
	{
		UndoStack.clear();
		RedoStack.clear();
	}

	void ReflectedTransactionHistory::Subscribe(IReflectedTransactionSubscriber* subscriber)
	{
		if (!subscriber)
		{
			return;
		}

		Subscribers.push_back(subscriber);
	}

	void ReflectedTransactionHistory::ApplyTransaction(EditorTransactionContext& context, const ReflectedTransaction& transaction, bool bRedo)
	{
		if (bRedo)
		{
			for (const ReflectedPropertyChange& change : transaction.Changes)
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

		for (IReflectedTransactionSubscriber* subscriber : Subscribers)
		{
			subscriber->OnTransactionApplied(transaction, !bRedo);
		}
	}

	void ReflectedTransactionHistory::ApplyChange(EditorTransactionContext& context, const ReflectedPropertyChange& change, bool bRedo)
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