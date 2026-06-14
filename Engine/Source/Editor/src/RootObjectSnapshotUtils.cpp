#include "RootObjectSnapshotUtils.h"
#include "ReflectedObjectSerialization.h"

namespace Nyx::Editor
{
	RootObjectSnapshot CaptureRootObjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root)
	{
		RootObjectSnapshot snapshot{};
		if (!root.IsValid())
		{
			return snapshot;
		}

		std::vector<ReflectedObjectView> subobjects;
		domain.EnumerateSubobjects(context, root, subobjects);

		snapshot.bAlive = true;

		for (const ReflectedObjectView& view : subobjects)
		{
			if (!view.TypeMetadata || !view.Object)
			{
				continue;
			}

			snapshot.Subobjects.push_back(
				CaptureReflectedObject(view.Object, *view.TypeMetadata)
			);
		}

		return snapshot;
	}

	bool RestoreRootObjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root,
		const RootObjectSnapshot& snapshot)
	{
		if (!root.IsValid())
		{
			return false;
		}

		// Ensure + restore every subobject from the snapshot.
		for (const ReflectedObjectSnapshot& subobjectSnapshot : snapshot.Subobjects)
		{
			if (!subobjectSnapshot.TypeMetadata)
			{
				continue;
			}

			if (!domain.EnsureSubobject(context, root, *subobjectSnapshot.TypeMetadata))
			{
				continue;
			}

			void* object = domain.ResolveMutable(context, root, *subobjectSnapshot.TypeMetadata);
			if (!object)
			{
				continue;
			}

			RestoreReflectedObject(object, subobjectSnapshot);
		}

		// Remove subobjects that exist now but are not present in the snapshot.
		std::vector<ReflectedObjectView> currentSubobjects;
		domain.EnumerateSubobjects(context, root, currentSubobjects);

		for (const ReflectedObjectView& current : currentSubobjects)
		{
			if (!current.TypeMetadata)
			{
				continue;
			}

			bool bFoundInSnapshot = false;
			for (const ReflectedObjectSnapshot& subobjectSnapshot : snapshot.Subobjects)
			{
				if (subobjectSnapshot.TypeMetadata == current.TypeMetadata)
				{
					bFoundInSnapshot = true;
					break;
				}
			}

			if (!bFoundInSnapshot)
			{
				domain.RemoveSubobject(context, root, *current.TypeMetadata);
			}
		}

		return true;
	}
}