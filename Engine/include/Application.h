#pragma once
#include "Core.h"
#include "Window.h"

namespace Engine
{
	class ENGINE_API Application
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
	ENGINE_API Application* CreateApplication();

} // Engine