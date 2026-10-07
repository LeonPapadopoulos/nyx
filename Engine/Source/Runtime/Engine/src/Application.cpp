#include "NyxPCH.h"
#include "Application.h"
#include "Renderer.h"
#include "Window.h"

#include <chrono>

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
			// Layers release their GPU resources in OnDetach, so the GPU has to be idle and
			// the renderer still alive. Detach in reverse order, like a stack.
			Window->GetRenderer().WaitIdle();
			for (auto layer = Layers.rbegin(); layer != Layers.rend(); ++layer)
			{
				(*layer)->OnDetach();
			}

			// Destroy the layers now, while the window and renderer they use still exist.
			Layers.clear();
		}

		void Application::PushLayer(std::unique_ptr<ILayer> layer)
		{
			layer->OnAttach(*this);
			Layers.push_back(std::move(layer));
		}

		void Application::Run()
		{
			// All layers are attached by now, so the startup banner can make way for the window.
			Window->FinishStartup();

			auto previousFrameTime = std::chrono::steady_clock::now();
			while (!Window->ShouldClose())
			{
				Window->PollEvents();

				const auto frameTime = std::chrono::steady_clock::now();
				const float deltaTime = std::chrono::duration<float>(frameTime - previousFrameTime).count();
				previousFrameTime = frameTime;

				for (const std::unique_ptr<ILayer>& layer : Layers)
				{
					layer->OnUpdate(deltaTime);
				}

				Window->DrawFrame(
					[this]()
					{
						for (const std::unique_ptr<ILayer>& layer : Layers)
						{
							layer->OnUI();
						}
					});
			}
		}
	}

} // Engine
