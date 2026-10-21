#pragma once
#include "Log.h"
#include "Assertions.h"
#include "Profiler.h"
#include "Entity.h"
#include "TypedHandle.h"

#include "Application.h"

//@todo define
//#ifdef ENGINE_PLATFORM_WINDOWS

//extern Nyx::Application* Nyx::CreateApplication(int argc, char** argv);

int main(int argc, char** argv)
{
	NYX_PROFILE_THREAD("Main");
	Nyx::Logger::Get().Init();
	CORE_LOG_WARNING("Initialized Log! {0}", 0);
	LOG_INFO("Initialized Log! {0}", 1);
	ASSERT(true);

	auto app = Nyx::CreateApplication(argc, argv);
	app->Run();
	delete app;
}

//#endif
