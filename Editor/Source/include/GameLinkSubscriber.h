#pragma once

#include "NetConnection.h"
#include "TransactionDomain.h"

#include <cstddef>
#include <vector>

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	// Sends the editor's edits to the game started with Play, so they show up there live.
	//
	// Every new edit, undo and redo becomes editor link messages (see LiveEdits.h) that bring the
	// game's copies of the entities it touched to the state they have in the editor afterwards:
	// - SetProperties with the current values of the properties that changed,
	// - CreateEntity with the whole entity, for one that was added or brought back by undo,
	// - DeleteEntity for one that is gone.
	// Tools don't need to do anything for this: whatever they record in the TransactionSystem is sent.
	//
	// Messages are kept per play session, from Play on, so edits made while the game is still
	// starting are sent once it is linked.
	class GameLinkSubscriber final : public ITransactionSubscriber
	{
	public:
		explicit GameLinkSubscriber(const Nyx::SceneDocument& scene);

		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;

		// From now on, edits are kept as messages until TakeMessages()
		void StartSession();

		// Drops the messages not taken yet, and ignores edits until the next StartSession(), e.g.
		// once the game was asked to quit
		void EndSession();

		bool IsSessionActive() const
		{
			return bSessionActive;
		}

		// The messages for the edits since the last call, in order
		std::vector<Nyx::Net::Message> TakeMessages();

		// If more waits, e.g. because the game never links, the session ends with a warning
		static constexpr size_t MaxPendingBytes = 16 * 1024 * 1024;

	private:
		void AddMessagesFor(const Transaction& transaction);
		void Keep(Nyx::Net::Message message);

	private:
		const Nyx::SceneDocument& Scene;
		bool bSessionActive = false;
		std::vector<Nyx::Net::Message> Pending;
		size_t PendingBytes = 0;
	};
}
