#pragma once

#include "InspectorTargetId.h"
#include "ReflectedTransactionSystem.h"
#include "ReflectionTypes.h"
#include "PropertyValue.h"
#include "PropertyValueUtils.h"

#include <optional>
#include <vector>

namespace Nyx::Editor
{
	class ReflectedDiffUtil
	{
	public:
		void TakeSnapshot(
			InspectorTargetId targetId,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		bool CommitChanges(const char* label, ReflectedTransactionHistory& history);
		void Cancel();

	private:
		struct Snapshot
		{
			InspectorTargetId TargetId{};
			void* Object = nullptr;
			const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
			std::vector<std::pair<size_t, Nyx::Reflection::PropertyValue>> PropertyValues;
		};

	private:
		std::vector<Snapshot> Snapshots;
	};
}