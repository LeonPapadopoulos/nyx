#include "ReflectedObjectSerialization.h"
#include "ReflectedPropertyAccess.h"

namespace Nyx::Editor
{
	ReflectedObjectSnapshot CaptureReflectedObject(
		void* object,
		const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		ReflectedObjectSnapshot snapshot{};
		snapshot.TypeMetadata = &typeMetadata;

		if (!object || !typeMetadata.Properties)
		{
			return snapshot;
		}

		for (size_t propertyIndex = 0; propertyIndex < typeMetadata.PropertyCount; ++propertyIndex)
		{
			const Nyx::Reflection::PropertyMetadata& property = typeMetadata.Properties[propertyIndex];

			if (!Nyx::Reflection::HasFlag(property.Flags, Nyx::Reflection::EPropertyFlags::Serialize))
			{
				continue;
			}

			snapshot.Properties.push_back(ReflectedPropertySnapshot{
				.PropertyIndex = propertyIndex,
				.Value = ReadReflectedPropertyValue(object, property)
				});
		}

		return snapshot;
	}

	void RestoreReflectedObject(
		void* object,
		const ReflectedObjectSnapshot& snapshot)
	{
		if (!object || !snapshot.TypeMetadata || !snapshot.TypeMetadata->Properties)
		{
			return;
		}

		for (const ReflectedPropertySnapshot& propertySnapshot : snapshot.Properties)
		{
			if (propertySnapshot.PropertyIndex >= snapshot.TypeMetadata->PropertyCount)
			{
				continue;
			}

			const Nyx::Reflection::PropertyMetadata& property =
				snapshot.TypeMetadata->Properties[propertySnapshot.PropertyIndex];

			WriteReflectedPropertyValue(object, property, propertySnapshot.Value);
		}
	}
}