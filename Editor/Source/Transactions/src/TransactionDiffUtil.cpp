#include "TransactionDiffUtil.h"

namespace Nyx::Editor
{
	namespace
	{
		SetValueChange MakeSetValueChange(
			const ObjectRef& target,
			const Nyx::Reflection::TypeMetadata* typeMetadata,
			size_t propertyIndex,
			const Nyx::Reflection::PropertyValue& before,
			const Nyx::Reflection::PropertyValue& after,
			const SubobjectPath& location)
		{
			return SetValueChange{
				.Target = target,
				.TypeMetadata = typeMetadata,
				.PropertyIndex = propertyIndex,
				.Before = before,
				.After = after,
				.Location = location
			};
		}
	}

	void TransactionDiffUtil::TakeSnapshot(
		const ObjectRef& target,
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata,
		const SubobjectPath& location)
	{
		if (!target.IsValid() || !object || !typeMetadata.Properties || typeMetadata.PropertyCount == 0)
		{
			return;
		}

		Snapshot snapshot{};
		snapshot.Target = target;
		snapshot.Object = object;
		snapshot.TypeMetadata = &typeMetadata;
		snapshot.Location = location;

		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[i];

			if (!Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Undo))
			{
				continue;
			}

			snapshot.PropertyValues.emplace_back(i, ReadReflectedPropertyValue(object, property));
			snapshot.PreviewedValues.push_back(snapshot.PropertyValues.back().second);
		}

		Snapshots.push_back(std::move(snapshot));
	}

	bool TransactionDiffUtil::PreviewChanges(const char* label, TransactionSystem& transactions)
	{
		Transaction transaction{};
		transaction.Label = label;

		for (Snapshot& snapshot : Snapshots)
		{
			if (!snapshot.Object || !snapshot.TypeMetadata || !snapshot.TypeMetadata->Properties)
			{
				continue;
			}

			for (size_t i = 0; i < snapshot.PropertyValues.size(); ++i)
			{
				const size_t propertyIndex = snapshot.PropertyValues[i].first;
				if (propertyIndex >= snapshot.TypeMetadata->PropertyCount)
				{
					continue;
				}

				const Nyx::Reflection::PropertyMetadata& property = snapshot.TypeMetadata->Properties[propertyIndex];
				Nyx::Reflection::PropertyValue currentValue = ReadReflectedPropertyValue(snapshot.Object, property);

				if (Nyx::Reflection::ArePropertyValuesEqual(snapshot.PreviewedValues[i], currentValue))
				{
					continue;
				}

				Change change{};
				change.Kind = EChangeKind::SetValue;
				change.Payload = MakeSetValueChange(snapshot.Target, snapshot.TypeMetadata, propertyIndex, snapshot.PreviewedValues[i],
					currentValue, snapshot.Location);
				transaction.Changes.push_back(std::move(change));

				snapshot.PreviewedValues[i] = std::move(currentValue);
			}
		}

		if (transaction.IsEmpty())
		{
			return false;
		}

		bPreviewed = true;
		transactions.Preview(transaction);
		return true;
	}

	bool TransactionDiffUtil::CommitChanges(const char* label, TransactionSystem& transactions)
	{
		Transaction transaction{};
		transaction.Label = label;

		for (const Snapshot& snapshot : Snapshots)
		{
			if (!snapshot.Object || !snapshot.TypeMetadata || !snapshot.TypeMetadata->Properties)
			{
				continue;
			}

			for (const auto& entry : snapshot.PropertyValues)
			{
				const size_t propertyIndex = entry.first;
				const Nyx::Reflection::PropertyValue& beforeValue = entry.second;

				if (propertyIndex >= snapshot.TypeMetadata->PropertyCount)
				{
					continue;
				}

				const Nyx::Reflection::PropertyMetadata& property = snapshot.TypeMetadata->Properties[propertyIndex];
				const Nyx::Reflection::PropertyValue afterValue = ReadReflectedPropertyValue(snapshot.Object, property);

				if (!Nyx::Reflection::ArePropertyValuesEqual(beforeValue, afterValue))
				{
					Change change{};
					change.Kind = EChangeKind::SetValue;
					change.Payload = MakeSetValueChange(snapshot.Target, snapshot.TypeMetadata, propertyIndex, beforeValue, afterValue,
						snapshot.Location);

					transaction.Changes.push_back(std::move(change));
				}
			}
		}

		// Dragged away and back: nothing to record, but the last preview showed another value
		if (transaction.IsEmpty() && bPreviewed)
		{
			PreviewChanges(label, transactions);
		}

		Snapshots.clear();
		bPreviewed = false;

		if (!transaction.IsEmpty())
		{
			transactions.Push(std::move(transaction));
			return true;
		}

		return false;
	}

	void TransactionDiffUtil::Cancel()
	{
		Snapshots.clear();
		bPreviewed = false;
	}
}
