#pragma once

#include "Window.h"

struct GLFWwindow;

namespace Nyx
{
	class IRenderer;

	namespace Editor
	{
		class EditorLayer;
	}
}

namespace Nyx
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

		bool IsTitleBarHovered() const { return bTitlebarHovered; }

		void SetDrawDockedPanelsCallback(std::function<void()> callback);

	private:
		void Initialize(const WindowSpecs& specs);
		void Shutdown();

		static void ApplyRoundedCorners(GLFWwindow* window);

		// @todo: Is there a better place to put this?
		void DrawUserInterface();
		void DrawTitlebar(float titlebarHeight);
		void DrawMenubar();

		bool IsMaximized() const;

		static void GLFW_ScrollCallback(GLFWwindow* window, double xOffset, double yOffset);

		void DrawDockspaceHost();

	private:
		GLFWwindow* Window = nullptr;
		std::unique_ptr<IRenderer> Renderer = nullptr;

		struct WindowData
		{
			std::string Title;
			unsigned int Width;
			unsigned int Height;
			bool bCustomTitlebar;
			bool bResizable;
			bool bVSync;
		};

		WindowData Data;
		bool bTitlebarHovered = false;

		std::unique_ptr<Nyx::Editor::EditorLayer> MainEditorLayer;
		std::function<void()> OnFrame;

		std::function<void()> DrawDockedPanelsCallback;
	};
}
