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
		// Owns the window and runs the frame loop. What the application actually does,
		// for example the editor, is added as layers.
		class NYXENGINE_API Application
		{
		public:
			Application();
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

		// To be defined by CLIENT
		Application* CreateApplication();
	}

} // Engine