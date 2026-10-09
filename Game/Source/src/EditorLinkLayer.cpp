#include "EditorLinkLayer.h"

#include "Log.h"
#include "NetConnection.h"

namespace Nyx::Game
{
	EditorLinkLayer::EditorLinkLayer(uint16_t editorPort)
		: EditorPort(editorPort)
	{
	}

	void EditorLinkLayer::OnAttach(Nyx::Engine::Application& /*application*/)
	{
		LOG_INFO("Editor link: connecting to the editor on port {0}", EditorPort);
		Link = std::make_unique<Nyx::Engine::EditorLink>(Nyx::Net::Connection::ConnectToLocalPort(EditorPort), "NyxGame");
	}

	void EditorLinkLayer::OnDetach()
	{
		if (Link)
		{
			Link->Close("the game is closing");
			Link.reset();
		}
	}

	void EditorLinkLayer::OnUpdate(float /*deltaTime*/)
	{
		if (!Link)
		{
			return;
		}

		Link->Update();

		// The editor sends nothing after its Hello yet
		while (std::optional<Nyx::Net::Message> message = Link->Receive())
		{
			LOG_WARNING("Editor link: skipped a message of type {0}, which this game doesn't know", message->Type);
		}

		// The link logged why it ended; the game runs on without it
		if (Link->GetState() == Nyx::Engine::EEditorLinkState::Closed)
		{
			Link.reset();
		}
	}
}
