#pragma once
#include "NyxEngineAPI.h"
#include "Layer.h"
#include "Window.h"
#include <memory>
#include <vector>

namespace Nyx
{
	namespace Engine
	{
		// How an application wants to be set up, chosen by the client (for example the editor).
		struct ApplicationSpecs
		{
			WindowSpecs Window;
		};

		// Owns the window and runs the frame loop. What the application actually does,
		// for example the editor, is added as layers.
		class NYXENGINE_API Application
		{
		public:
			explicit Application(const ApplicationSpecs& specs = ApplicationSpecs());
			virtual ~Application();

			void Run();

			// Attaches the layer right away; from then on it runs every frame. Push layers
			// before Run(), not from inside another layer's hooks.
			void PushLayer(std::unique_ptr<ILayer> layer);

			IWindow& GetWindow() { return *Window; }

		private:
			std::unique_ptr<IWindow> Window;
			std::vector<std::unique_ptr<ILayer>> Layers;
		};

		// To be defined by the client (for example the editor), which can use the command line arguments.
		Application* CreateApplication(int argc, char** argv);
	}

} // Engine