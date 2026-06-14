#pragma once

#include "TransactionDomain.h"

namespace Nyx::Editor
{
	RootObjectSnapshot CaptureRootObjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root);

	bool RestoreRootObjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root,
		const RootObjectSnapshot& snapshot);
}