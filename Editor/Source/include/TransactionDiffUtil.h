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
		void TakeSnapshot(
			const ObjectRef& target,
			void* object,
			const Nyx::Reflection::TypeMetadata& typeMetadata);

		bool CommitChanges(const char* label, TransactionSystem& transactions);
		void Cancel();

	private:
		struct Snapshot
		{
			ObjectRef Target{};
			void* Object = nullptr;
			const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
			std::vector<std::pair<size_t, Nyx::Reflection::PropertyValue>> PropertyValues;
		};

	private:
		std::vector<Snapshot> Snapshots;
	};
}