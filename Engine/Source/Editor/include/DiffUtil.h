#pragma once

#include "EditableObjectTraits.h"
#include "EditableTargetResolver.h"
#include "TransactionHistory.h"
#include "PropertyValue.h"
#include "PropertyValueUtils.h"

#include <memory>
#include <type_traits>
#include <vector>

namespace Nyx::Editor
{
	template<typename TObject>
	struct PropertyDelta
	{
		size_t PropertyIndex = 0;
		Nyx::Reflection::PropertyValue Before;
		Nyx::Reflection::PropertyValue After;
	};

	template<typename TObject>
	class ObjectPropertyChange final : public IChange
	{
	public:
		ObjectPropertyChange(
			InspectorTargetId targetId,
			std::vector<PropertyDelta<TObject>> deltas)
			: TargetId(targetId)
			, Deltas(std::move(deltas))
		{
		}

		void Undo(EditorTransactionContext& context) override
		{
			Apply(context, false);
		}

		void Redo(EditorTransactionContext& context) override
		{
			Apply(context, true);
		}

	private:
		void Apply(EditorTransactionContext& context, bool bRedo)
		{
			static_assert(EditableTargetResolver<TObject>::Enabled, "Missing EditableTargetResolver specialization.");
			static_assert(EditableObjectTraits<TObject>::Enabled, "Missing EditableObjectTraits specialization.");

			TObject* object = EditableTargetResolver<TObject>::Resolve(context, TargetId);
			if (!object)
			{
				return;
			}

			const auto& properties = EditableObjectTraits<TObject>::GetProperties();

			for (const PropertyDelta<TObject>& delta : Deltas)
			{
				if (delta.PropertyIndex >= properties.size())
				{
					continue;
				}

				const auto& property = properties[delta.PropertyIndex];
				property.SetValue(*object, bRedo ? delta.After : delta.Before);
			}
		}

	private:
		InspectorTargetId TargetId{};
		std::vector<PropertyDelta<TObject>> Deltas;
	};

	class DiffUtil
	{
	public:
		template<typename TObject>
		void TakeSnapshot(InspectorTargetId targetId, TObject& object)
		{
			using TObjectType = std::remove_cv_t<std::remove_reference_t<TObject>>;

			static_assert(EditableObjectTraits<TObjectType>::Enabled, "Missing EditableObjectTraits specialization.");

			Snapshots.push_back(std::make_unique<Snapshot<TObjectType>>(targetId, object));
		}

		void CommitChanges(const char* label, TransactionHistory& history)
		{
			Transaction transaction{};
			transaction.Label = label;

			for (const std::unique_ptr<ISnapshot>& snapshot : Snapshots)
			{
				snapshot->AppendChange(transaction);
			}

			Snapshots.clear();

			if (!transaction.IsEmpty())
			{
				history.Push(std::move(transaction));
			}
		}

		void Cancel()
		{
			Snapshots.clear();
		}

	private:
		struct ISnapshot
		{
			virtual ~ISnapshot() = default;
			virtual void AppendChange(Transaction& transaction) const = 0;
		};

		template<typename TObject>
		struct Snapshot final : ISnapshot
		{
			Snapshot(InspectorTargetId targetId, TObject& object)
				: TargetId(targetId)
				, LiveObject(&object)
			{
				const auto& properties = EditableObjectTraits<TObject>::GetProperties();
				BeforeValues.reserve(properties.size());

				for (const auto& property : properties)
				{
					BeforeValues.push_back(property.GetValue(object));
				}
			}

			void AppendChange(Transaction& transaction) const override
			{
				if (!LiveObject)
				{
					return;
				}

				const auto& properties = EditableObjectTraits<TObject>::GetProperties();
				std::vector<PropertyDelta<TObject>> deltas;

				for (size_t index = 0; index < properties.size(); ++index)
				{
					const Nyx::Reflection::PropertyValue afterValue = properties[index].GetValue(*LiveObject);
					if (!AreEditorValuesEqual(BeforeValues[index], afterValue))
					{
						deltas.push_back(PropertyDelta<TObject>{
							.PropertyIndex = index,
								.Before = BeforeValues[index],
								.After = afterValue
						});
					}
				}

				if (!deltas.empty())
				{
					transaction.Changes.push_back(
						std::make_unique<ObjectPropertyChange<TObject>>(TargetId, std::move(deltas))
					);
				}
			}

			InspectorTargetId TargetId{};
			TObject* LiveObject = nullptr;
			std::vector<Nyx::Reflection::PropertyValue> BeforeValues;
		};

	private:
		std::vector<std::unique_ptr<ISnapshot>> Snapshots;
	};
}