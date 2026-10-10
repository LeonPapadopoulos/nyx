#pragma once

#include "SceneSerializationTypes.h"
#include "TransactionDomain.h"

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	// After every edit, undo and redo, runs the PostLoad step of each component of the entities
	// the transaction touched, as loading a scene does. That is what turns a MeshRenderer's mesh
	// and material paths into the renderer's assets, so e.g. typing a mesh path in the details
	// panel, or undoing a delete, shows the mesh. The game does the same for every live edit.
	class ComponentPostLoadSubscriber final : public ITransactionSubscriber
	{
	public:
		explicit ComponentPostLoadSubscriber(Nyx::SceneDocument& scene);

		// Where assets come from; until set, PostLoad steps run without an asset resolver
		void SetPostLoadContext(const Nyx::Engine::ScenePostLoadContext& context)
		{
			PostLoadContext = context;
		}

		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;

	private:
		void RunPostLoad(const Transaction& transaction);

	private:
		Nyx::SceneDocument& Scene;
		Nyx::Engine::ScenePostLoadContext PostLoadContext;
	};
}
