#include "ComponentTypeRegistry.h"
#include "GameWorld.h"
#include "SimSystem.h"
#include "TestLanternComponent.h"

#include <memory>

// The test module's own code, named in nyx_add_game_module(): REGISTER and SYSTEMS
namespace NyxTestModule
{
	namespace
	{
		// A lantern loaded without a label is labelled "Lit": what a game's post-load step does
		void LabelOnLoad(TestLanternComponent& lantern, Nyx::ScenePostLoadContext& /*context*/)
		{
			if (lantern.Label.empty())
			{
				lantern.Label = "Lit";
			}
		}

		// Every lantern grows half a unit brighter each step it runs
		class LanternGlowSystem final : public Nyx::ISimSystem
		{
		public:
			const char* GetName() const override
			{
				return "Lantern Glow";
			}

			void Step(const Nyx::SimContext& context) override
			{
				context.World.Each<TestLanternComponent>(
					[](Nyx::Entity /*entity*/, TestLanternComponent& lantern)
					{
						lantern.Brightness += 0.5f;
					});
			}
		};
	}

	void RegisterModule()
	{
		Nyx::ComponentTypeRegistry::Get().SetPostLoad<TestLanternComponent>(&LabelOnLoad);
	}

	void CreateSystems(Nyx::GameWorld& world)
	{
		world.AddSystem(std::make_unique<LanternGlowSystem>());
	}
}
