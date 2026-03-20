#pragma once
#include "Renderer.h"


namespace Nyx
{
	namespace Renderer
	{
		class VulkanRenderer : public IRenderer
		{
		public:
			VulkanRenderer();
			virtual ~VulkanRenderer() = default;

			virtual void Initialize(const char* applicationName, GLFWwindow* window);
			virtual void Shutdown();

			virtual void BeginFrame();
			virtual void EndFrame();

			virtual void OnFramebufferResized();

		private:
		};
	}
}