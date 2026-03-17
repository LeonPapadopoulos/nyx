#pragma once
#include <string>
#include "Core.h"

namespace Engine 
{
	struct WindowSpecs
	{
		std::string Title = "Nyx Engine";
		unsigned int Width = 1280;
		unsigned int Height = 720;
		bool bUseCustomTitlebar = true;
		bool bResizable = true;
	};

	// Platform Abstraction
	class ENGINE_API IWindow
	{
	public:
		virtual ~IWindow()
		{
		}

		virtual void OnUpdate() = 0;

		virtual unsigned int GetWidth() = 0;
		virtual unsigned int GetHeight() = 0;

		virtual void SetVSync(bool enabled) = 0;
		virtual bool IsVSync() const = 0;
		virtual bool ShouldClose() const = 0;

		static IWindow* Create(const WindowSpecs& specs = WindowSpecs());
	};

} // Engine

