#pragma once

#include "PropertyValue.h"
#include "PropertyValueUtils.h"
#include "ReflectedPropertyAccess.h"
#include "TransactionSystem.h"

#include <vector>

namespace Nyx::Editor
{
	class TransactionDiffUtil
	{
	public:
		// object is a subobject of target, or a struct inside one; then location says where, so
		// undo can find it again (see SubobjectPath)
		void TakeSnapshot(
			const ObjectRef& target,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata,
			const SubobjectPath& location = {});

		// Previews (TransactionSystem::Preview) the properties that changed since the last preview,
		// or since the snapshot, so a running game shows a value while it is being dragged. Call it
		// once per frame; it does nothing while the values stay the same. Returns whether it previewed.
		bool PreviewChanges(const char* label, TransactionSystem& transactions);

		// Records what changed since the snapshot as one undo step. If that is nothing but values
		// were previewed (e.g. a value dragged away and back), the values are previewed once more,
		// so what was shown matches the objects again.
		bool CommitChanges(const char* label, TransactionSystem& transactions);
		void Cancel();

	private:
		struct Snapshot
		{
			ObjectRef Target{};
			void* Object = nullptr;
			const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
			SubobjectPath Location;
			std::vector<std::pair<size_t, Nyx::Reflection::PropertyValue>> PropertyValues;

			// The value each of PropertyValues' properties had in the last preview; at first, as in the snapshot
			std::vector<Nyx::Reflection::PropertyValue> PreviewedValues;
		};

	private:
		std::vector<Snapshot> Snapshots;

		// Whether PreviewChanges() showed anything since the snapshot
		bool bPreviewed = false;
	};
}