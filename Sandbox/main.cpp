#include "Engine.h"
#include <iostream>

class Sandbox : public Engine::Application
{
public:
	Sandbox()
	{
		std::printf("Creating Sandbox");
	}

	~Sandbox()
	{
		std::printf("Destroying Sandbox");
	}
};

Engine::Application* Engine::CreateApplication()
{
	return new Sandbox();
}