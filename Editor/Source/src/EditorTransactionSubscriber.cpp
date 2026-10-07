#include "EditorTransactionSubscriber.h"
#include "Log.h"

namespace Nyx::Editor
{
	void EditorTransactionSubscriber::OnTransactionApplied(const Transaction& transaction, bool bWasUndo)
	{
		(void)transaction;
		(void)bWasUndo;

		LOG_INFO("OnTransactionApplied: {0}, {1}", bWasUndo ? "UNDO" : "REDO", transaction.Label.c_str());

		// Next step:
		// - mark scene dirty
		// - refresh derived editor views
		// - invalidate cached data
	}
}