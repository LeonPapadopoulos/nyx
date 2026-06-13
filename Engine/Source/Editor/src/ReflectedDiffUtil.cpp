#include "ReflectedDiffUtil.h"
#include "ReflectedPropertyAccess.h"
#include "PropertyValue.h"
#include "PropertyValueUtils.h"

namespace Nyx::Editor
{
	void ReflectedDiffUtil::TakeSnapshot(
		InspectorTargetId targetId,
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		if (!object || !typeMetadata.Properties || typeMetadata.PropertyCount == 0)
		{
			return;
		}

		Snapshot snapshot{};
		snapshot.TargetId = targetId;
		snapshot.Object = object;
		snapshot.TypeMetadata = &typeMetadata;

		for (size_t i = 0; i < typeMetadata.PropertyCount; ++i)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[i];

			if (!Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Undo))
			{
				continue;
			}

			snapshot.PropertyValues.emplace_back(i, ReadReflectedPropertyValue(object, property));
		}

		Snapshots.push_back(std::move(snapshot));
	}

	bool ReflectedDiffUtil::CommitChanges(const char* label, ReflectedTransactionHistory& history)
	{
		ReflectedTransaction transaction{};
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
					transaction.Changes.push_back(ReflectedPropertyChange{
						.TargetId = snapshot.TargetId,
						.TypeMetadata = snapshot.TypeMetadata,
						.PropertyIndex = propertyIndex,
						.Before = beforeValue,
						.After = afterValue
						});
				}
			}
		}

		Snapshots.clear();

		if (!transaction.IsEmpty())
		{
			history.Push(std::move(transaction));
			return true;
		}

		return false;
	}

	void ReflectedDiffUtil::Cancel()
	{
		Snapshots.clear();
	}
}