#include "RootObjectSnapshotUtils.h"

#include "BinaryArchive.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"

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

			// As a property block, as scene files store components, so struct properties are kept
			Nyx::Engine::BinaryWriter writer;
			if (!Nyx::Engine::ReflectedArchiveSerializer::SerializeObject(writer, view.Object, *view.TypeMetadata))
			{
				LOG_WARNING("Undo: a property of {0} can't be kept; undo may not bring it back", view.TypeMetadata->Name);
			}

			snapshot.Subobjects.push_back(SubobjectSnapshot{
				.TypeMetadata = view.TypeMetadata,
				.Properties = writer.GetBytes() });
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

		// Ensure + restore every subobject from the snapshot. Properties that aren't saved (e.g. a
		// MeshRenderer's loaded mesh) are left alone; ComponentPostLoadSubscriber fills them in.
		Nyx::Engine::ReadWarnings warnings;
		for (const SubobjectSnapshot& subobjectSnapshot : snapshot.Subobjects)
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

			Nyx::Engine::BinaryReader reader;
			reader.LoadFromMemory(subobjectSnapshot.Properties);
			if (!Nyx::Engine::ReflectedArchiveSerializer::DeserializeObject(reader, object, *subobjectSnapshot.TypeMetadata, warnings))
			{
				LOG_WARNING("Undo: {0} couldn't be brought back completely", subobjectSnapshot.TypeMetadata->Name);
			}
		}

		// Snapshots are written and read by the same build, so this only happens if something is broken
		warnings.Log("Undo");

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
			for (const SubobjectSnapshot& subobjectSnapshot : snapshot.Subobjects)
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