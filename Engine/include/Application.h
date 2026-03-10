#pragma once
#include "Core.h"

namespace Engine
{
	class ENGINE_API Application
	{
	public:
		Application();
		virtual ~Application();

		void Run();
	};

	// To be defined by CLIENT
	ENGINE_API Application* CreateApplication();
}