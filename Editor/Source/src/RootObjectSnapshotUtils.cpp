#include "RootObjectSnapshotUtils.h"

#include "BinaryArchive.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"

namespace Nyx::Editor
{
	SubobjectSnapshot CaptureSubobjectSnapshot(const void* object, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		// As a property block, as scene files store components, so struct properties are kept
		Nyx::Engine::BinaryWriter writer;
		if (!Nyx::Engine::ReflectedArchiveSerializer::SerializeObject(writer, object, typeMetadata))
		{
			LOG_WARNING("Undo: a property of {0} can't be kept; undo may not bring it back", typeMetadata.Name);
		}

		return SubobjectSnapshot{
			.TypeMetadata = &typeMetadata,
			.Properties = writer.GetBytes() };
	}

	bool RestoreSubobjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root,
		const SubobjectSnapshot& snapshot)
	{
		if (!root.IsValid() || !snapshot.TypeMetadata || !domain.EnsureSubobject(context, root, *snapshot.TypeMetadata))
		{
			return false;
		}

		void* object = domain.ResolveMutable(context, root, *snapshot.TypeMetadata);
		if (!object)
		{
			return false;
		}

		// Properties that aren't saved (e.g. a MeshRenderer's loaded mesh) are left alone;
		// ComponentPostLoadSubscriber fills them in
		Nyx::Engine::ReadWarnings warnings;
		Nyx::Engine::BinaryReader reader;
		reader.LoadFromMemory(snapshot.Properties);
		const bool bRead = Nyx::Engine::ReflectedArchiveSerializer::DeserializeObject(reader, object, *snapshot.TypeMetadata, warnings);
		if (!bRead)
		{
			LOG_WARNING("Undo: {0} couldn't be brought back completely", snapshot.TypeMetadata->Name);
		}

		// Snapshots are written and read by the same build, so this only happens if something is broken
		warnings.Log("Undo");
		return bRead;
	}

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

			snapshot.Subobjects.push_back(CaptureSubobjectSnapshot(view.Object, *view.TypeMetadata));
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

		// Ensure + restore every subobject from the snapshot
		for (const SubobjectSnapshot& subobjectSnapshot : snapshot.Subobjects)
		{
			RestoreSubobjectSnapshot(domain, context, root, subobjectSnapshot);
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