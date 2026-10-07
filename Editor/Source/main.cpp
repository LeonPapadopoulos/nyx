#include "Engine.h"
#include "EditorLayer.h"

#include <memory>

namespace Nyx::Editor
{
	// The editor application: the engine's window and frame loop, with the editor as its only layer.
	class EditorApplication : public Nyx::Engine::Application
	{
	public:
		EditorApplication()
			: Application(Nyx::Engine::ApplicationSpecs{ .Window = { .Title = "Nyx Editor" } })
		{
			PushLayer(std::make_unique<EditorLayer>());
		}
	};
}

Nyx::Engine::Application* Nyx::Engine::CreateApplication(int /*argc*/, char** /*argv*/)
{
	return new Nyx::Editor::EditorApplication();
}
