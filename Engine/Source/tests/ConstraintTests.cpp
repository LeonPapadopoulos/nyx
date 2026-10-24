// Constraints in the game world (ConstraintComponent, PhysicsConstraints): a crate hung by its
// component rests where its frames meet and its state reads its weight; switched off it falls and
// switched on it holds again, without being made again; past its break force it switches itself
// off, says so for one step and mends when switched on; a third entity joins two crates by guid;
// it is made again when its values change, when a body is made again, and goes with its component
// or a body; a motor driven every step is never made again; scene files keep every value and never
// the state; live edits from another world remake it; the same scene gives the same steps for 600.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "ConstraintComponent.h"
#include "EditorLinkMessages.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "LiveEdits.h"
#include "Log.h"
#include "PhysicsComponents.h"
#include "ReflectionUtils.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ 1.0 / 60.0, Next, static_cast<double>(Next) / 60.0 });
			}
		}
	};

	// Calls a function each step, in its stage
	class CallingSystem final : public ISimSystem
	{
	public:
		explicit CallingSystem(std::function<void(const SimContext&)> call)
			: Call(std::move(call))
		{
		}

		const char* GetName() const override { return "Calling"; }
		void Step(const SimContext& context) override { Call(context); }

	private:
		std::function<void(const SimContext&)> Call;
	};

	bool NearVec(const glm::vec3& a, const glm::vec3& b, float tolerance)
	{
		return glm::length(a - b) <= tolerance;
	}

	std::string Text(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	Entity AddCrate(Registry& world, const glm::vec3& position, float mass)
	{
		const Entity crate = world.CreateEntity();
		world.Add<GuidComponent>(crate, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(crate, TransformComponent{ .Position = position });
		world.Add<BoxColliderComponent>(crate, BoxColliderComponent{});
		world.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = mass });
		return crate;
	}

	EntityGuid GuidOf(const Registry& world, Entity entity)
	{
		return world.Get<GuidComponent>(entity).Guid;
	}

	// Its top centre at a point of the world
	ConstraintComponent HangFrom(const glm::vec3& worldPoint)
	{
		return ConstraintComponent{ .Kind = EJointKind::Point, .BodyPoint = glm::vec3(0.0f, 0.5f, 0.0f), .OtherPoint = worldPoint };
	}

	glm::vec3 PositionOf(const GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<TransformComponent>(entity).Position;
	}

	const ConstraintStateComponent& StateOf(const GameWorld& world, Entity holder)
	{
		Require(world.GetRegistry().Has<ConstraintStateComponent>(holder), "A constraint's entity should have its state");
		return world.GetRegistry().Get<ConstraintStateComponent>(holder);
	}

	void TestAHungCrateRestsAndReadsItsWeight()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity crate = AddCrate(registry, glm::vec3(0.0f, 4.2f, 0.0f), 2.0f);
		registry.Add<ConstraintComponent>(crate, HangFrom(glm::vec3(0.0f, 5.0f, 0.0f)));

		steps.Run(1);
		Require(world.GetConstraints().FindConstraint(crate).IsValid() && world.GetConstraints().GetConstraintCount() == 1,
			"The component should make a constraint once the crate has a body");
		Require(world.GetPhysics().GetConstraintCount() == 1, "The physics world should have the one constraint");

		steps.Run(240);
		Require(NearVec(PositionOf(world, crate), glm::vec3(0.0f, 4.5f, 0.0f), 0.02f), "The crate should hang where its frames meet " + Text(PositionOf(world, crate)));
		const ConstraintStateComponent& state = StateOf(world, crate);
		Require(state.bHolding && !state.bBroken, "The state should say it holds");
		Require(NearVec(state.Force, glm::vec3(0.0f, 2.0f * 9.81f, 0.0f), 0.4f), "The state's load should be the crate's weight " + Text(state.Force));
	}

	void TestSwitchingOffAndOn()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity crate = AddCrate(registry, glm::vec3(0.0f, 4.5f, 0.0f), 1.0f);
		registry.Add<ConstraintComponent>(crate, HangFrom(glm::vec3(0.0f, 5.0f, 0.0f)));
		steps.Run(60);
		const ConstraintHandle made = world.GetConstraints().FindConstraint(crate);

		registry.Get<ConstraintComponent>(crate).bEnabled = false;
		steps.Run(45);
		Require(PositionOf(world, crate).y < 3.0f, "Switched off, the crate should fall");
		Require(!StateOf(world, crate).bHolding && StateOf(world, crate).Force == glm::vec3(0.0f), "Off, its state should hold nothing");
		Require(world.GetConstraints().FindConstraint(crate) == made && !world.GetPhysics().IsConstraintEnabled(made),
			"Switched off, the constraint should be kept, off");

		registry.Get<ConstraintComponent>(crate).bEnabled = true;
		steps.Run(180);
		Require(NearVec(PositionOf(world, crate), glm::vec3(0.0f, 4.5f, 0.0f), 0.03f), "Switched on, it should hold the crate at its rest again " + Text(PositionOf(world, crate)));
		Require(StateOf(world, crate).bHolding && world.GetConstraints().FindConstraint(crate) == made, "The same constraint should hold again");
	}

	void TestBreakingSwitchesItOffAndSaysSo()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity crate = AddCrate(registry, glm::vec3(0.0f, 4.5f, 0.0f), 2.0f);
		ConstraintComponent hook = HangFrom(glm::vec3(0.0f, 5.0f, 0.0f));
		hook.BreakForce = 25.0f;
		registry.Add<ConstraintComponent>(crate, hook);

		// Pulls the crate down while asked to; tells which steps saw a break
		bool bPull = false;
		std::vector<uint64_t> stepsWithBreaks;
		world.AddSystem(std::make_unique<CallingSystem>([&](const SimContext& context)
			{
				if (bPull)
				{
					context.Physics.AddForce(context.Bodies.FindBody(crate), glm::vec3(0.0f, -20.0f, 0.0f));
				}
			}));
		world.AddSystem(std::make_unique<CallingSystem>([&](const SimContext& context)
			{
				for (const ConstraintBrokenEvent& broken : context.Constraints.GetBrokenConstraints())
				{
					Require(broken.Holder == crate && broken.Load.Force.y > 25.0f, "The break should name the crate and the load that broke it");
					stepsWithBreaks.push_back(context.Step.StepIndex);
				}
			}),
			1, ESimStage::AfterPhysics);

		steps.Run(60);
		Require(stepsWithBreaks.empty() && registry.Get<ConstraintComponent>(crate).bEnabled, "Under its weight alone it should hold");

		bPull = true;
		steps.Run(3);
		bPull = false;
		Require(stepsWithBreaks.size() == 1 && stepsWithBreaks[0] == 60, "It should break in the first step past its force, and say so once");
		Require(!registry.Get<ConstraintComponent>(crate).bEnabled, "Broken, its component should be switched off");
		Require(StateOf(world, crate).bBroken && !StateOf(world, crate).bHolding, "Its state should say it broke");
		steps.Run(30);
		Require(PositionOf(world, crate).y < 3.5f, "Broken, the crate should fall");

		// Put back where it hangs first: yanked back from below, it would break again at once
		registry.Get<TransformComponent>(crate).Position = glm::vec3(0.0f, 4.5f, 0.0f);
		registry.Get<ConstraintComponent>(crate).bEnabled = true;
		steps.Run(180);
		Require(stepsWithBreaks.size() == 1, "Mended at its rest, it shouldn't break again");
		Require(!StateOf(world, crate).bBroken && StateOf(world, crate).bHolding, "Switched on, it should be mended");
		Require(NearVec(PositionOf(world, crate), glm::vec3(0.0f, 4.5f, 0.0f), 0.03f), "Mended, it should hold the crate at its rest " + Text(PositionOf(world, crate)));
	}

	void TestAThirdEntityJoinsTwoCrates()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity upper = AddCrate(registry, glm::vec3(0.0f, 4.5f, 0.0f), 1.0f);
		const Entity lower = AddCrate(registry, glm::vec3(0.0f, 3.2f, 0.0f), 3.0f);
		registry.Add<ConstraintComponent>(upper, HangFrom(glm::vec3(0.0f, 5.0f, 0.0f)));
		// Neither crate holds the link: a third entity does, naming both
		const Entity link = registry.CreateEntity();
		registry.Add<ConstraintComponent>(link,
			ConstraintComponent{ .Kind = EJointKind::Point,
				.Body = GuidOf(registry, lower),
				.Other = GuidOf(registry, upper),
				.BodyPoint = glm::vec3(0.0f, 0.5f, 0.0f),
				.OtherPoint = glm::vec3(0.0f, -0.5f, 0.0f) });

		steps.Run(300);
		Require(world.GetConstraints().GetConstraintCount() == 2, "Both constraints should be made");
		Require(NearVec(PositionOf(world, lower), glm::vec3(0.0f, 3.5f, 0.0f), 0.03f), "The lower crate should hang from the upper " + Text(PositionOf(world, lower)));
		Require(std::abs(StateOf(world, link).Force.y - 3.0f * 9.81f) < 0.6f, "The link should hold the lower crate's weight " + Text(StateOf(world, link).Force));
		Require(std::abs(StateOf(world, upper).Force.y - 4.0f * 9.81f) < 0.8f, "The top should hold both " + Text(StateOf(world, upper).Force));

		// A value that makes it: made again, at the new rest
		const ConstraintHandle first = world.GetConstraints().FindConstraint(link);
		registry.Get<ConstraintComponent>(link).OtherPoint = glm::vec3(0.0f, -1.0f, 0.0f);
		steps.Run(240);
		Require(world.GetConstraints().FindConstraint(link) != first, "A changed frame should make the constraint again");
		Require(NearVec(PositionOf(world, lower), glm::vec3(0.0f, 3.0f, 0.0f), 0.03f), "The lower crate should hang at the new rest " + Text(PositionOf(world, lower)));

		// A body made again takes its constraints along: they are made again on the new one
		const ConstraintHandle second = world.GetConstraints().FindConstraint(link);
		registry.Get<BoxColliderComponent>(lower).HalfExtents = glm::vec3(0.4f);
		steps.Run(1);
		Require(world.GetConstraints().FindConstraint(link) != second && world.GetPhysics().HasConstraint(world.GetConstraints().FindConstraint(link)),
			"A body made again should have its constraint made again");
		steps.Run(120);
		Require(NearVec(PositionOf(world, lower), glm::vec3(0.0f, 3.0f, 0.0f), 0.05f), "And it should still hang " + Text(PositionOf(world, lower)));

		// The other crate gone: the link holds nothing until it is back, the top is untouched
		registry.DestroyEntity(upper);
		steps.Run(1);
		Require(!world.GetConstraints().FindConstraint(link).IsValid() && !StateOf(world, link).bHolding, "Without its other body the link should hold nothing");
		Require(world.GetPhysics().GetConstraintCount() == 0, "The physics world should have no constraints left");

		// The component gone: so is its state
		registry.Remove<ConstraintComponent>(link);
		steps.Run(1);
		Require(!registry.Has<ConstraintStateComponent>(link), "Without its component the state should go");
	}

	void TestAMotorDrivenEveryStepIsNeverMadeAgain()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		// A door turning about the world's up, its hinge at its left edge
		const Entity door = AddCrate(registry, glm::vec3(0.5f, 1.0f, 0.0f), 2.0f);
		registry.Get<RigidBodyComponent>(door).GravityFactor = 0.0f;
		const glm::quat upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		registry.Add<ConstraintComponent>(door,
			ConstraintComponent{ .Kind = EJointKind::Hinge,
				.BodyPoint = glm::vec3(-0.5f, 0.0f, 0.0f),
				.BodyRotation = upright,
				.OtherPoint = glm::vec3(0.0f, 1.0f, 0.0f),
				.OtherRotation = upright,
				.MotorMode = EJointMotor::Position,
				.MotorFrequency = 3.0f });
		// Opens it a degree further each step, up to 90
		world.AddSystem(std::make_unique<CallingSystem>([&](const SimContext& context)
			{
				ConstraintComponent& hinge = context.World.Get<ConstraintComponent>(door);
				hinge.MotorTarget = std::min(hinge.MotorTarget + 1.0f, 90.0f);
			}));

		steps.Run(1);
		const ConstraintHandle made = world.GetConstraints().FindConstraint(door);
		steps.Run(240);
		Require(world.GetConstraints().FindConstraint(door) == made, "Driving the motor shouldn't make the constraint again");
		const glm::vec3 arm = registry.Get<TransformComponent>(door).Rotation * glm::vec3(1.0f, 0.0f, 0.0f);
		Require(NearVec(arm, glm::vec3(0.0f, 0.0f, -1.0f), 0.05f) || NearVec(arm, glm::vec3(0.0f, 0.0f, 1.0f), 0.05f),
			"The motor should have turned the door a quarter round its hinge " + Text(arm));
		Require(NearVec(PositionOf(world, door), glm::vec3(0.0f, 1.0f, 0.0f) + arm * 0.5f, 0.03f), "The door should stay on its hinge");
	}

	void TestScenesKeepConstraints(const std::filesystem::path& directory)
	{
		Registry world;
		const Entity crate = AddCrate(world, glm::vec3(0.0f, 4.0f, 0.0f), 2.0f);
		const Entity other = AddCrate(world, glm::vec3(0.0f, 6.0f, 0.0f), 1.0f);
		const ConstraintComponent saved{ .Kind = EJointKind::Slider,
			.Body = GuidOf(world, crate),
			.Other = GuidOf(world, other),
			.bEnabled = false,
			.BodyPoint = glm::vec3(0.1f, 0.2f, 0.3f),
			.BodyRotation = glm::angleAxis(0.5f, glm::vec3(0.0f, 1.0f, 0.0f)),
			.OtherPoint = glm::vec3(-1.0f, -2.0f, -3.0f),
			.OtherRotation = glm::angleAxis(0.25f, glm::vec3(1.0f, 0.0f, 0.0f)),
			.bLimits = true,
			.LimitMin = -0.5f,
			.LimitMax = 0.75f,
			.MinDistance = 0.25f,
			.MaxDistance = 2.5f,
			.SpringFrequency = 4.0f,
			.SpringDamping = 0.3f,
			.MotorMode = EJointMotor::Velocity,
			.MotorTarget = 1.5f,
			.MotorFrequency = 5.0f,
			.MotorDamping = 0.7f,
			.MotorMaxForce = 80.0f,
			.BreakForce = 300.0f,
			.BreakTorque = 40.0f,
			.BreakSeconds = 0.5f };
		world.Add<ConstraintComponent>(crate, saved);
		world.Add<ConstraintStateComponent>(crate, ConstraintStateComponent{ .bHolding = true, .bBroken = true, .Force = glm::vec3(1.0f) });

		const std::filesystem::path path = directory / "Constraints.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with a constraint failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with a constraint failed");

		int constraints = 0;
		loaded.Each<ConstraintComponent>([&](Entity entity, ConstraintComponent& c)
			{
				++constraints;
				Require(c.Kind == saved.Kind && c.Body == saved.Body && c.Other == saved.Other && c.bEnabled == saved.bEnabled,
					"The constraint's kind, entities and switch should be kept");
				Require(c.BodyPoint == saved.BodyPoint && c.OtherPoint == saved.OtherPoint && c.BodyRotation == saved.BodyRotation &&
						c.OtherRotation == saved.OtherRotation,
					"The constraint's frames should be kept");
				Require(c.bLimits && c.LimitMin == saved.LimitMin && c.LimitMax == saved.LimitMax && c.MinDistance == saved.MinDistance &&
						c.MaxDistance == saved.MaxDistance && c.SpringFrequency == saved.SpringFrequency && c.SpringDamping == saved.SpringDamping,
					"The constraint's limits and spring should be kept");
				Require(c.MotorMode == saved.MotorMode && c.MotorTarget == saved.MotorTarget && c.MotorFrequency == saved.MotorFrequency &&
						c.MotorDamping == saved.MotorDamping && c.MotorMaxForce == saved.MotorMaxForce,
					"The constraint's motor should be kept");
				Require(c.BreakForce == saved.BreakForce && c.BreakTorque == saved.BreakTorque && c.BreakSeconds == saved.BreakSeconds,
					"The constraint's breaking should be kept");
				Require(!loaded.Has<ConstraintStateComponent>(entity), "The constraint's state should never be saved");
			});
		Require(constraints == 1, "The one constraint should be saved and loaded");
	}

	void TestLiveEditsRemakeIt()
	{
		// The editor's copy and the game's, as the editor link keeps them
		Registry editor;
		const Entity crate = AddCrate(editor, glm::vec3(0.0f, 4.5f, 0.0f), 1.0f);
		editor.Add<ConstraintComponent>(crate, HangFrom(glm::vec3(0.0f, 5.0f, 0.0f)));

		GameWorld game;
		Steps steps{ game };
		const std::optional<CreateEntityMessage> create = MakeCreateEntityMessage(editor, crate);
		Require(create && ApplyLiveEdit(MakeNetMessage(*create), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The crate should reach the game");
		const auto findCrate = [&]()
		{
			std::optional<Entity> found;
			game.GetRegistry().Each<ConstraintComponent>([&](Entity entity, ConstraintComponent&)
				{
					found = entity;
				});
			Require(found.has_value(), "The game should have the crate's constraint");
			return *found;
		};
		steps.Run(120);
		Require(NearVec(PositionOf(game, findCrate()), glm::vec3(0.0f, 4.5f, 0.0f), 0.02f), "The game's crate should hang at its rest");

		// One value edited: the game's constraint is made again at the new rest
		editor.Get<ConstraintComponent>(crate).OtherPoint = glm::vec3(1.0f, 5.0f, 0.0f);
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<ConstraintComponent>());
		const std::optional<size_t> property = Nyx::Reflection::FindPropertyIndexByName(Nyx::Reflection::GetTypeMetadata<ConstraintComponent>(), "OtherPoint");
		Require(ops && property, "The constraint component should be registered, with its other point");
		const std::optional<SetPropertiesMessage> set = MakeSetPropertiesMessage(editor, crate, *ops, { *property });
		Require(set && ApplyLiveEdit(MakeNetMessage(*set), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The edit should reach the game");
		steps.Run(120);
		// Swinging from there, as moved sideways
		const TransformComponent& swinging = game.GetRegistry().Get<TransformComponent>(findCrate());
		const glm::vec3 hung = swinging.Position + swinging.Rotation * glm::vec3(0.0f, 0.5f, 0.0f);
		Require(NearVec(hung, glm::vec3(1.0f, 5.0f, 0.0f), 0.02f), "The game's crate should hang from the edited point " + Text(hung));

		// The whole entity sent again replaces it: its constraint goes with the old one and is made for the new
		editor.Get<ConstraintComponent>(crate).Kind = EJointKind::Distance;
		editor.Get<ConstraintComponent>(crate).MaxDistance = 2.0f;
		const std::optional<CreateEntityMessage> again = MakeCreateEntityMessage(editor, crate);
		Require(again && ApplyLiveEdit(MakeNetMessage(*again), game.GetRegistry(), {}) == ELiveEditResult::Applied, "The crate should be sent again");
		steps.Run(240);
		const Entity replaced = findCrate();
		Require(game.GetConstraints().GetConstraintCount() == 1 && game.GetPhysics().GetConstraintCount() == 1, "The replaced crate should have one constraint");
		const glm::vec3 top = PositionOf(game, replaced) + game.GetRegistry().Get<TransformComponent>(replaced).Rotation * glm::vec3(0.0f, 0.5f, 0.0f);
		Require(std::abs(glm::length(top - glm::vec3(1.0f, 5.0f, 0.0f)) - 2.0f) < 0.05f, "On a 2 m rope, the crate's top should hang 2 m below the point " + Text(top));
	}

	// Three crates in a chain, a door driven round, a drawer shoved: every pose after 600 steps
	std::vector<float> RunConstrainedScene()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity floor = registry.CreateEntity();
		registry.Add<TransformComponent>(floor, TransformComponent{ .Position = glm::vec3(0.0f, -0.5f, 0.0f) });
		registry.Add<BoxColliderComponent>(floor, BoxColliderComponent{ .HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f) });

		std::vector<Entity> moving;
		EntityGuid previous;
		for (int link = 0; link < 3; ++link)
		{
			const Entity crate = AddCrate(registry, glm::vec3(0.5f * static_cast<float>(link), 6.0f - static_cast<float>(link), 0.0f), 1.0f);
			registry.Add<ConstraintComponent>(crate,
				ConstraintComponent{ .Kind = EJointKind::Point,
					.Other = previous,
					.BodyPoint = glm::vec3(0.0f, 0.5f, 0.0f),
					.OtherPoint = previous.IsValid() ? glm::vec3(0.0f, -0.5f, 0.0f) : glm::vec3(0.0f, 7.0f, 0.0f) });
			previous = GuidOf(registry, crate);
			moving.push_back(crate);
		}
		const glm::quat upright = glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 0.0f, 1.0f));
		const Entity door = AddCrate(registry, glm::vec3(4.0f, 1.5f, 0.0f), 2.0f);
		registry.Add<ConstraintComponent>(door,
			ConstraintComponent{ .Kind = EJointKind::Hinge,
				.BodyPoint = glm::vec3(-0.5f, 0.0f, 0.0f),
				.BodyRotation = upright,
				.OtherPoint = glm::vec3(3.5f, 1.5f, 0.0f),
				.OtherRotation = upright,
				.MotorMode = EJointMotor::Velocity,
				.MotorTarget = 90.0f,
				.MotorMaxForce = 30.0f });
		moving.push_back(door);
		const Entity drawer = AddCrate(registry, glm::vec3(-4.0f, 2.0f, 0.0f), 1.0f);
		registry.Add<ConstraintComponent>(drawer,
			ConstraintComponent{ .Kind = EJointKind::Slider, .OtherPoint = glm::vec3(-4.0f, 2.0f, 0.0f), .bLimits = true, .LimitMin = -1.0f, .LimitMax = 1.0f });
		moving.push_back(drawer);
		world.AddSystem(std::make_unique<CallingSystem>([&](const SimContext& context)
			{
				if (context.Step.StepIndex % 90 == 0)
				{
					context.Physics.AddImpulse(context.Bodies.FindBody(drawer), glm::vec3(context.Step.StepIndex % 180 == 0 ? 3.0f : -3.0f, 0.0f, 0.0f));
					context.Physics.AddImpulse(context.Bodies.FindBody(moving[2]), glm::vec3(0.0f, 0.0f, 2.0f));
				}
			}));

		steps.Run(600);
		std::vector<float> poses;
		for (const Entity entity : moving)
		{
			const TransformComponent& transform = registry.Get<TransformComponent>(entity);
			poses.insert(poses.end(), { transform.Position.x, transform.Position.y, transform.Position.z, transform.Rotation.w,
				transform.Rotation.x, transform.Rotation.y, transform.Rotation.z });
		}
		return poses;
	}

	void TestTheSameSceneGivesTheSameStepsFor600()
	{
		Require(RunConstrainedScene() == RunConstrainedScene(), "The same constrained scene should end exactly alike after 600 steps");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxConstraintTests");
		TestAHungCrateRestsAndReadsItsWeight();
		TestSwitchingOffAndOn();
		TestBreakingSwitchesItOffAndSaysSo();
		TestAThirdEntityJoinsTwoCrates();
		TestAMotorDrivenEveryStepIsNeverMadeAgain();
		TestScenesKeepConstraints(directory.Path);
		TestLiveEditsRemakeIt();
		TestTheSameSceneGivesTheSameStepsFor600();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Constraint test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All constraint tests passed.\n";
	return 0;
}
