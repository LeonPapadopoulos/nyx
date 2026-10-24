#include "ComponentTypeRegistry.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "InputSystem.h"
#include "PlayerController.h"
#include "ReflectedAssetTypes.h"
#include "SimSystem.h"
#include "TestCommands.h"
#include "TestCreatureAsset.h"
#include "TestLanternComponent.h"
#include "TestLanternGlowComponent.h"

#include <memory>
#include <vector>

// The test module's own code, named in nyx_add_game_module(): REGISTER, SYSTEMS and PLAYER
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

		// The player's commands, before anything else of the step: what a game's systems do with theirs
		class LanternCommandSystem final : public Nyx::ISimSystem
		{
		public:
			const char* GetName() const override
			{
				return "Lantern Commands";
			}

			void Step(const Nyx::SimContext& context) override
			{
				context.Commands.ForEach<TestBrightenCommand>(
					[&](const TestBrightenCommand& command)
					{
						const std::optional<Nyx::Entity> lantern = Nyx::FindEntityByGuid(context.World, context.Guids, command.Lantern);
						if (lantern && context.World.Has<TestLanternComponent>(*lantern))
						{
							context.World.Get<TestLanternComponent>(*lantern).Brightness += command.Amount;
						}
					});
				context.Commands.ForEach<TestRelabelCommand>(
					[&](const TestRelabelCommand& command)
					{
						context.World.Each<TestLanternComponent>(
							[&](Nyx::Entity /*entity*/, TestLanternComponent& lantern)
							{
								lantern.Label = command.Label;
							});
					});
			}
		};

		// B brightens the first lantern by 2; while the cursor points into the world, every lantern is
		// labelled "Pointed". It only looks at the world; the commands change it.
		class TestPlayer final : public Nyx::IPlayerController
		{
		public:
			explicit TestPlayer(Nyx::Input::InputSystem& input)
			{
				using namespace Nyx::Input;
				Brighten = input.AddAction({ "Brighten", EActionValue::Button });
				const ContextHandle context = input.AddContext(
					{ .Name = "Test Player", .Priority = 100, .Bindings = { InputBinding::ForControl(Brighten, Key(EKey::B), ETriggerKind::Pressed) } });
				input.GetUser().SetContextActive(context, true);
			}

			void Update(const Nyx::PlayerFrame& frame) override
			{
				if (frame.Input.GetUser().ConsumeTriggered(Brighten))
				{
					const Nyx::Registry& world = frame.World.GetRegistry();
					bool bQueued = false;
					world.Each<TestLanternComponent>(
						[&](Nyx::Entity entity, const TestLanternComponent& /*lantern*/)
						{
							if (!bQueued && world.Has<Nyx::GuidComponent>(entity))
							{
								frame.Commands.Add(TestBrightenCommand{ .Lantern = world.Get<Nyx::GuidComponent>(entity).Guid, .Amount = 2.0f, .LeftBehind = 7.0f });
								bQueued = true;
							}
						});
				}
				if (frame.CursorRay)
				{
					frame.Commands.Add(TestRelabelCommand{ .Label = "Pointed" });
				}
			}

		private:
			Nyx::Input::ActionHandle Brighten;
		};

		// Every lantern grows half a unit brighter each step it runs, and its glow, a run-time
		// component, says how bright and for how many steps
		class LanternGlowSystem final : public Nyx::ISimSystem
		{
		public:
			const char* GetName() const override
			{
				return "Lantern Glow";
			}

			void Step(const Nyx::SimContext& context) override
			{
				std::vector<Nyx::Entity> lanterns;
				context.World.Each<TestLanternComponent>(
					[&](Nyx::Entity entity, TestLanternComponent& lantern)
					{
						lantern.Brightness += 0.5f;
						lanterns.push_back(entity);
					});
				for (const Nyx::Entity entity : lanterns)
				{
					if (!context.World.Has<TestLanternGlowComponent>(entity))
					{
						context.World.Add<TestLanternGlowComponent>(entity, TestLanternGlowComponent{});
					}
					TestLanternGlowComponent& glow = context.World.Get<TestLanternGlowComponent>(entity);
					glow.Glow = context.World.Get<TestLanternComponent>(entity).Brightness;
					++glow.Steps;
				}
			}
		};
	}

	void RegisterModule()
	{
		Nyx::ComponentTypeRegistry::Get().SetPostLoad<TestLanternComponent>(&LabelOnLoad);
		Nyx::ComponentTypeRegistry::Get().SetRuntime<TestLanternGlowComponent>();

		// The module's own asset type, which the editor opens, makes and sends to the games
		Nyx::ReflectedAssetTypes::Get().Register(Nyx::ReflectedAssetDesc<TestCreatureData>{
			.TypeId = "TestCreature",
			.DisplayName = "Test Creature",
			.Extension = TestCreatureExtension,
			.Description = "A creature's stats, as a game's own asset type",
			.Validate = [](const TestCreatureData& creature, std::vector<Nyx::PropertyProblem>& outProblems)
			{
				if (creature.Speed < 0.0f)
				{
					outProblems.push_back({ "Speed", "can't be below 0" });
				}
				for (const TestCreatureRange& range : creature.Ranges)
				{
					if (range.Min > range.Max)
					{
						outProblems.push_back({ "Ranges." + range.Name + ".Min", "is above Max" });
					}
				}
			} });
	}

	void CreateSystems(Nyx::GameWorld& world)
	{
		world.AddSystem(std::make_unique<LanternCommandSystem>());
		world.AddSystem(std::make_unique<LanternGlowSystem>());
	}

	std::unique_ptr<Nyx::IPlayerController> CreatePlayer(Nyx::Input::InputSystem& input)
	{
		return std::make_unique<TestPlayer>(input);
	}
}
