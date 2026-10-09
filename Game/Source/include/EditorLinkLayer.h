#pragma once

#include "EditorLink.h"
#include "Layer.h"

#include <cstdint>
#include <memory>

namespace Nyx::Game
{
	// The game's end of the editor link, used when the editor started the game: connects back to
	// the editor on the port given with --editor-port. If that fails, or the editor goes away,
	// the game keeps running.
	class EditorLinkLayer : public Nyx::Engine::ILayer
	{
	public:
		explicit EditorLinkLayer(uint16_t editorPort);

		void OnAttach(Nyx::Engine::Application& application) override;
		void OnDetach() override;
		void OnUpdate(float deltaTime) override;

	private:
		uint16_t EditorPort = 0;
		std::unique_ptr<Nyx::Engine::EditorLink> Link;
	};
}
