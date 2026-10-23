#include "TransactionSystem.h"
#include "ReflectionUtils.h"
#include "RootObjectSnapshotUtils.h"

namespace
{
	using namespace Nyx::Editor;

	// The object whose property a change sets: the subobject itself, or the struct (or array
	// element) inside it that location leads to. Null if the path no longer fits the types, e.g.
	// after a property was removed from the code.
	void* ResolveLocation(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& target,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		const SubobjectPath& location)
	{
		if (!location.SubobjectType)
		{
			return domain.ResolveMutable(context, target, typeMetadata);
		}

		void* subobject = domain.ResolveMutable(context, target, *location.SubobjectType);
		if (!subobject)
		{
			return nullptr;
		}

		const ResolvedObject resolved = ResolvePropertyPath(subobject, *location.SubobjectType, location.PropertyIndices);
		return resolved.TypeMetadata == &typeMetadata ? resolved.Object : nullptr;
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

	void TransactionSystem::Preview(const Transaction& transaction)
	{
		if (transaction.IsEmpty())
		{
			return;
		}

		for (ITransactionSubscriber* subscriber : Subscribers)
		{
			subscriber->OnTransactionPreviewed(transaction);
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

		case EChangeKind::AddSubobject:
		{
			// Redo adds it again, undo removes it
			const AddSubobjectChange& add = std::get<AddSubobjectChange>(change.Payload);
			ApplySubobjectChange(context, add.Target, add.AfterAdd, bRedo);
			break;
		}

		case EChangeKind::RemoveSubobject:
		{
			// Redo removes it again, undo brings it back
			const RemoveSubobjectChange& remove = std::get<RemoveSubobjectChange>(change.Payload);
			ApplySubobjectChange(context, remove.Target, remove.BeforeRemove, !bRedo);
			break;
		}

		case EChangeKind::ReplaceArray:
			ApplyReplaceArrayChange(context, std::get<ReplaceArrayChange>(change.Payload), bRedo);
			break;

		default:
			break;
		}
	}

	void TransactionSystem::ApplySubobjectChange(
		EditorTransactionContext& context, const ObjectRef& target, const SubobjectSnapshot& subobject, bool bAdd)
	{
		ITransactionDomain* domain = FindDomain(target.Domain);
		if (!domain || !subobject.TypeMetadata)
		{
			return;
		}

		if (bAdd)
		{
			RestoreSubobjectSnapshot(*domain, context, target, subobject);
		}
		else
		{
			domain->RemoveSubobject(context, target, *subobject.TypeMetadata);
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

		void* object = ResolveLocation(*domain, context, change.Target, *change.TypeMetadata, change.Location);
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

	void TransactionSystem::ApplyReplaceArrayChange(EditorTransactionContext& context, const ReplaceArrayChange& change, bool bRedo)
	{
		ITransactionDomain* domain = change.Target.IsValid() && change.TypeMetadata ? FindDomain(change.Target.Domain) : nullptr;
		if (!domain)
		{
			return;
		}

		if (void* object = ResolveLocation(*domain, context, change.Target, *change.TypeMetadata, change.Location))
		{
			LoadReflectedProperty(object, *change.TypeMetadata, bRedo ? change.After : change.Before);
		}
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