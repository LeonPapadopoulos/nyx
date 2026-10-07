#pragma once

#include "EditorTransactionContext.h"
#include "InspectorTargetId.h"
#include "PropertyValue.h"
#include "PropertyValueUtils.h"
#include "ReflectionTypes.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Editor
{
	struct ReflectedPropertyChange
	{
		InspectorTargetId TargetId{};
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Before;
		Nyx::Reflection::PropertyValue After;
	};

	struct ReflectedTransaction
	{
		std::string Label;
		std::vector<ReflectedPropertyChange> Changes;

		bool IsEmpty() const
		{
			return Changes.empty();
		}
	};

	struct IReflectedObjectResolver
	{
		virtual ~IReflectedObjectResolver() = default;

		virtual void* ResolveMutable(
			EditorTransactionContext& context,
			InspectorTargetId targetId,
			const Nyx::Reflection::TypeMetadata& typeMetadata) = 0;
	};

	struct IReflectedTransactionSubscriber
	{
		virtual ~IReflectedTransactionSubscriber() = default;
		virtual void OnTransactionApplied(const ReflectedTransaction& transaction, bool bWasUndo) = 0;
	};

	class ReflectedTransactionHistory
	{
	public:
		explicit ReflectedTransactionHistory(IReflectedObjectResolver& resolver)
			: Resolver(resolver)
		{
		}

		void Push(ReflectedTransaction&& transaction);

		bool Undo(EditorTransactionContext& context);
		bool Redo(EditorTransactionContext& context);

		void Clear();

		void Subscribe(IReflectedTransactionSubscriber* subscriber);

	private:
		void ApplyTransaction(EditorTransactionContext& context, const ReflectedTransaction& transaction, bool bRedo);
		void ApplyChange(EditorTransactionContext& context, const ReflectedPropertyChange& change, bool bRedo);

	private:
		IReflectedObjectResolver& Resolver;
		std::vector<ReflectedTransaction> UndoStack;
		std::vector<ReflectedTransaction> RedoStack;
		std::vector<IReflectedTransactionSubscriber*> Subscribers;
	};
}