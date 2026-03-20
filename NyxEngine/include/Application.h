#pragma once
#include "NyxEngineAPI.h"
#include "Window.h"
#include <memory>

namespace Nyx
{
	namespace Engine
	{
		class NYXENGINE_API Application
		{
		public:
			Application();
			virtual ~Application();

			void Run();
		private:
			std::unique_ptr<IWindow> Window;
			bool bRunning = true;
		};

		// To be defined by CLIENT
		Application* CreateApplication();
	}

} // Engine