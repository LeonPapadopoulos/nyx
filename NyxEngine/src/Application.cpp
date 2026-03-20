#include "NyxEnginePCH.h"
#include "Application.h"
#include "Window.h"

namespace Nyx
{
	namespace Engine
	{
		Application::Application()
		{
			Window = std::unique_ptr<IWindow>(IWindow::Create());
		}

		Application::~Application()
		{
		}

		void Application::Run()
		{
			while (bRunning)
			{
				Window->OnUpdate();

				if (Window->ShouldClose())
				{
					// glfwPollEvents(); ?
					bRunning = false;
				}
			}
		}
	}

} // Engine
