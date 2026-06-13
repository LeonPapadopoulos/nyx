#pragma once

#include "TransactionSystem.h"

#include <vector>

namespace Nyx::Editor
{
	class TransactionDiffUtil
	{
	public:
		void TakeSnapshot(
			InspectorTargetId targetId,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		bool CommitChanges(const char* label, TransactionSystem& transactionSystem);
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