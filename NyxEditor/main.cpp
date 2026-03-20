#include "Engine.h"
#include <iostream>

namespace App
{
	struct Name
	{
		std::string Value;
	};

	struct Transform
	{
		float X = 0.0f;
		float Y = 0.0f;
		float Rotation = 0.0f;
	};

	class Sandbox : public Nyx::Engine::Application
	{
	public:
		Sandbox()
		{
			std::printf("Creating Sandbox");
			std::cout << std::endl;

			std::cout << "TestECS Start" << std::endl;
			{
				TestECS();
			}
			std::cout << "TestECS End" << std::endl;
		}

		~Sandbox()
		{
			std::printf("Destroying Sandbox");
		}

	private:
		void TestECS()
		{
			using namespace Nyx::Engine;

			Registry registry;
			Entity player = registry.CreateEntity();
			Entity enemy = registry.CreateEntity();

			LOG_INFO("Entity: Generation: {0}; Index: {1}", player.Generation(), player.Index());

			registry.Add<Name>(player, Name{ "Player" });
			registry.Add<Transform>(player, Transform{ 100.0f, 200.0f, 0.0f });

			registry.Add<Transform>(enemy, Transform{ 99.9f, 111.1f, 3.1415f });

			if (registry.Has<Transform>(player))
			{
				auto& transform = registry.Get<Transform>(player);
				transform.X += 10.0f;
				LOG_INFO("Entity: Transform: ( {0}, {1}, {2} )", transform.X, transform.Y, transform.Rotation);
			}

			registry.Each<Transform>([](Entity entity, Transform& transform)
				{
					LOG_INFO("Entity {0}:{1} -> Transform({2}, {3}, {4})",
						entity.Generation(),
						entity.Index(),
						transform.X,
						transform.Y,
						transform.Rotation);
				});

			registry.DestroyEntity(player);
			registry.DestroyEntity(enemy);

			player = registry.CreateEntity();
			enemy = registry.CreateEntity();
			LOG_INFO("Player: Generation: {0}; Index: {1}", player.Generation(), player.Index());
			LOG_INFO("Enemy: Generation: {0}; Index: {1}", enemy.Generation(), enemy.Index());
		}
	};
}

Nyx::Engine::Application* Nyx::Engine::CreateApplication()
{
	return new App::Sandbox();
}