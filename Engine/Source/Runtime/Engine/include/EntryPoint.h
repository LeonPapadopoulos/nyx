#pragma once
#include "Log.h"
#include "Assertions.h"
#include "Entity.h"
#include "TypedHandle.h"

#include "Application.h"

//@todo define
//#ifdef ENGINE_PLATFORM_WINDOWS

//extern Nyx::Engine::Application* Nyx::Engine::CreateApplication();

int main(int argc, char** argv)
{
	Nyx::Core::Logger::Get().Init();
	CORE_LOG_WARNING("Initialized Log! {0}", 0);
	LOG_INFO("Initialized Log! {0}", 1);
	ASSERT(true);

	auto app = Nyx::Engine::CreateApplication();
	app->Run();
	delete app;
}

//#endif
