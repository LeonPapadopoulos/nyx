#pragma once

#include "TransactionSystem.h"

namespace Nyx::Editor
{
	class EditorTransactionSubscriber final : public ITransactionSubscriber
	{
	public:
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;
	};
}