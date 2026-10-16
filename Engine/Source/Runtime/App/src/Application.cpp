#include "NyxPCH.h"
#include "Application.h"
#include "ComponentRegistration.h"
#include "EditorRenderer.h"
#include "Window.h"

#include <chrono>

namespace Nyx
{
	Application::Application(const ApplicationSpecs& specs)
	{
		// Saving and loading scenes, and the editor's details panel and undo, look up
		// component types in the ComponentTypeRegistry: fill it before any layer runs.
		RegisterComponentTypes();

		Window = std::unique_ptr<IWindow>(IWindow::Create(specs.Window));
		if (Nyx::Input::IInputBackend* windowInput = Window->GetInputBackend())
		{
			Input.AddBackend(*windowInput);
		}

		Renderer = Nyx::CreateRenderer();
		Renderer->SetVSync(true);
		Window->SetStartupStatus("Starting Vulkan and preparing rendering resources");
		Renderer->Initialize(specs.Window.Title.c_str(), Window->GetNativeWindow());
		Window->SetFramebufferResizedCallback([this]()
			{
				Renderer->OnFramebufferResized();
			});
		Window->StartUIInput();
	}

	Application::~Application()
	{
		// Layers release their GPU resources in OnDetach, so the GPU has to be idle and
		// the renderer still alive. Detach in reverse order, like a stack.
		Renderer->WaitIdle();
		for (auto layer = Layers.rbegin(); layer != Layers.rend(); ++layer)
		{
			(*layer)->OnDetach();
		}

		// Destroy the layers now, while the window and renderer they use still exist.
		Layers.clear();

		// The renderer before the window it draws into
		Window->SetFramebufferResizedCallback({});
		Renderer->Shutdown();
		Renderer.reset();
	}

	IRenderer& Application::GetRenderer()
	{
		return *Renderer;
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
			Window->WaitForNextFrame();
			Window->PollEvents();
			Input.BeginFrame(Nyx::Input::NowUs());

			const auto frameTime = std::chrono::steady_clock::now();
			const float deltaTime = std::chrono::duration<float>(frameTime - previousFrameTime).count();
			previousFrameTime = frameTime;

			for (const std::unique_ptr<ILayer>& layer : Layers)
			{
				layer->OnUpdate(deltaTime);
			}

			DrawFrame();

			// The cursor as the frame's layers asked for it
			Input.EndFrame();
		}
	}

	void Application::DrawFrame()
	{
		// A minimized window has nothing to draw into
		if (Window->WaitIfMinimized())
		{
			return;
		}

		Renderer->DrawFrame([this]()
			{
				Window->DrawUI([this]()
					{
						for (const std::unique_ptr<ILayer>& layer : Layers)
						{
							layer->OnUI();
						}
					});
			});

		// Without its device the renderer draws nothing more: close as if the user had, so the
		// application shuts down the usual way.
		if (Renderer->IsDeviceLost())
		{
			Window->RequestClose();
		}
	}
}
