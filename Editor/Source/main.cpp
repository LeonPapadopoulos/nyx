#include "Engine.h"
#include "EditorLayer.h"

#include <memory>

namespace Nyx::Editor
{
	// The editor application: the engine's window and frame loop, with the editor as its only layer.
	class EditorApplication : public Nyx::Application
	{
	public:
		EditorApplication()
			: Application(Nyx::ApplicationSpecs{ .Window = { .Title = "Nyx Editor" }, .Module = &Nyx::GetLinkedGameModule() })
		{
			PushLayer(std::make_unique<EditorLayer>());
		}
	};
}

Nyx::Application* Nyx::CreateApplication(int /*argc*/, char** /*argv*/)
{
	return new Nyx::Editor::EditorApplication();
}
