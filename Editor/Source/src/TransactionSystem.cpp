#include "TransactionSystem.h"
#include "RootObjectSnapshotUtils.h"

namespace Nyx::Editor
{
	void TransactionSystem::RegisterDomain(EObjectDomain domain, ITransactionDomain* handler)
	{
		if (!handler || domain == EObjectDomain::None)
		{
			return;
		}

		Domains[domain] = handler;
	}

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
			ApplyAddObjectChange(context, std::get<AddObjectChange>(change.Payload), bRedo);
			break;

		case EChangeKind::DeleteObject:
			ApplyDeleteObjectChange(context, std::get<DeleteObjectChange>(change.Payload), bRedo);
			break;

		default:
			break;
		}
	}

	void TransactionSystem::ApplySetValueChange(EditorTransactionContext& context, const SetValueChange& change, bool bRedo)
	{
		if (!change.Target.IsValid() || !change.TypeMetadata || !change.TypeMetadata->Properties)
		{
			return;
		}

		ITransactionDomain* domain = FindDomain(change.Target.Domain);
		if (!domain)
		{
			return;
		}

		void* object = domain->ResolveMutable(context, change.Target, *change.TypeMetadata);
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

	void TransactionSystem::ApplyAddObjectChange(EditorTransactionContext& context, const AddObjectChange& change, bool bRedo)
	{
		ITransactionDomain* domain = FindDomain(change.Target.Domain);
		if (!domain)
		{
			return;
		}

		if (bRedo)
		{
			if (domain->CreateRootObject(context, change.Target))
			{
				RestoreRootObjectSnapshot(*domain, context, change.Target, change.AfterCreate);
			}
		}
		else
		{
			domain->DeleteRootObject(context, change.Target);
		}
	}

	void TransactionSystem::ApplyDeleteObjectChange(EditorTransactionContext& context, const DeleteObjectChange& change, bool bRedo)
	{
		ITransactionDomain* domain = FindDomain(change.Target.Domain);
		if (!domain)
		{
			return;
		}

		if (bRedo)
		{
			domain->DeleteRootObject(context, change.Target);
		}
		else
		{
			if (domain->CreateRootObject(context, change.Target))
			{
				RestoreRootObjectSnapshot(*domain, context, change.Target, change.BeforeDelete);
			}
		}
	}

	ITransactionDomain* TransactionSystem::FindDomain(EObjectDomain domain)
	{
		const auto it = Domains.find(domain);
		return it != Domains.end() ? it->second : nullptr;
	}
}