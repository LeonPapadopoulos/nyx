#pragma once

#include "TransactionDomain.h"

namespace Nyx::Editor
{
	// One subobject (component), whole
	SubobjectSnapshot CaptureSubobjectSnapshot(const void* object, const Nyx::Reflection::TypeMetadata& typeMetadata);

	// Adds the subobject to the root object if it lacks it, and gives it the snapshot's values
	bool RestoreSubobjectSnapshot(
		ITransactionDomain& domain,
		EditorTransactionContext& context,
		const ObjectRef& root,
		const SubobjectSnapshot& snapshot);

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