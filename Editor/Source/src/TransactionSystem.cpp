#include "TransactionSystem.h"
#include "ReflectionUtils.h"
#include "RootObjectSnapshotUtils.h"

namespace
{
	using namespace Nyx::Editor;

	// The object whose property the change sets: the subobject itself, or the struct inside it
	// that change.Location leads to. Null if the path no longer fits the types, e.g. after a
	// property was removed from the code.
	void* ResolveLocation(ITransactionDomain& domain, EditorTransactionContext& context, const SetValueChange& change)
	{
		const SubobjectPath& location = change.Location;
		if (!location.SubobjectType)
		{
			return domain.ResolveMutable(context, change.Target, *change.TypeMetadata);
		}

		void* object = domain.ResolveMutable(context, change.Target, *location.SubobjectType);
		const Nyx::Reflection::TypeMetadata* type = location.SubobjectType;

		for (const size_t propertyIndex : location.PropertyIndices)
		{
			if (!object || !type->Properties || propertyIndex >= type->PropertyCount)
			{
				return nullptr;
			}

			const Nyx::Reflection::PropertyMetadata& property = type->Properties[propertyIndex];
			const Nyx::Reflection::TypeMetadata* nestedType = Nyx::Reflection::TryGetNestedType(property);
			if (!nestedType)
			{
				return nullptr;
			}

			object = Nyx::Reflection::GetPropertyAddress(object, property);
			type = nestedType;
		}

		return type == change.TypeMetadata ? object : nullptr;
	}
}

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

		for (ITransactionSubscriber* subscriber : Subscribers)
		{
			subscriber->OnTransactionCommitted(UndoStack.back());
		}
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

		void* object = ResolveLocation(*domain, context, change);
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