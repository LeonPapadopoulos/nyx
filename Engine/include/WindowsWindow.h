#pragma once

#include "Window.h"

class GLFWwindow;

namespace Engine
{

	class WindowsWindow : public IWindow
	{
	public:
		WindowsWindow(const WindowSpecs& specs);
		virtual ~WindowsWindow();

		void OnUpdate() override;
		unsigned int GetWidth() override;
		unsigned int GetHeight() override;

		virtual void SetVSync(bool enabled) override;
		virtual bool IsVSync() const override;
		virtual bool ShouldClose() const override;

	private:
		void Initialize(const WindowSpecs& specs);
		void Shutdown();

	private:
		GLFWwindow* Window;

		struct WindowData
		{
			std::string Title;
			unsigned int Width;
			unsigned int Height;
			bool bVSync;
		};

		WindowData Data;
	};

} // Engine
