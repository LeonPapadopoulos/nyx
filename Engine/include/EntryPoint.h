#pragma once
#include "Log.h"
#include "Assertions.h"
#include "Entity.h"
#include "TypedHandle.h"

//@todo define
//#ifdef ENGINE_PLATFORM_WINDOWS

extern Engine::Application* Engine::CreateApplication();

int main(int argc, char** argv)
{
	Engine::Log::Init();
	CORE_LOG_WARNING("Initialized Log! {0}", 0);
	LOG_INFO("Initialized Log! {0}", 1);
	ASSERT(true);
	ASSERT(false && "Default false");

	auto app = Engine::CreateApplication();
	app->Run();
	delete app;
}

//#endif
