#pragma once

#include "Window.h"

struct GLFWwindow;

namespace Nyx::Engine
{
	class VulkanUtil;
	class ImGuiVulkanUtil;
}

namespace Nyx
{
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

			bool IsTitleBarHovered() const { return bTitlebarHovered; }

		private:
			void Initialize(const WindowSpecs& specs);
			void Shutdown();
			void SetupVulkan();

			static void ApplyRoundedCorners(GLFWwindow* window);

			// @todo: Is there a better place to put this?
			void DrawUserInterface();
			void DrawTitlebar(float titlebarHeight);
			void DrawMenubar();

			bool IsMaximized() const;

		private:
			GLFWwindow* Window = nullptr;
			Nyx::Engine::VulkanUtil* VulkanHandler = nullptr;
			Nyx::Engine::ImGuiVulkanUtil* ImGui = nullptr;

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
		};
	}

} // Engine
