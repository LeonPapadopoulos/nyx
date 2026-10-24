// Physics materials (PhysicsMaterialAsset, PhysicsMaterials): a .nyxphysmat file read and written,
// its comments, unknown lines and a game's own extras kept, values that aren't numbers listed;
// colliders naming one in a game world, a missing one the engine's; friction from the material
// deciding whether a box slides down a slope, changed live without making the body again; density
// giving the mass unless a mass is given, made again when it changes; a game reading its own numbers
// (GetPhysicsMaterial(), GetExtra()); scene files keeping the reference.
#include "ComponentRegistration.h"
#include "GameWorld.h"
#include "Log.h"
#include "PhysicsComponents.h"
#include "PhysicsMaterialAsset.h"
#include "PhysicsMaterials.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
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

	bool Near(float a, float b, float tolerance)
	{
		return std::abs(a - b) <= tolerance;
	}

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
		Require(static_cast<bool>(stream), "Can't write " + file.string());
	}

	std::string ReadFile(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	const char* WoodText = "# Planks and crates\n"
						   "Friction = 0.6\n"
						   "Density = 600\n"
						   "Grain = along\n"
						   "\n"
						   "Extra.Flammability = 0.8\n"
						   "Extra.Hardness = 2\n";

	void TestAFileIsReadAndWritten(const std::filesystem::path& directory)
	{
		PhysicsMaterialAsset asset;
		std::string error;
		Require(asset.Parse(WoodText, error), "The wood should be read: " + error);
		std::vector<std::string> problems;
		const PhysicsMaterialData wood = asset.Read(&problems);
		Require(problems.empty(), "The wood should have no problems");
		Require(wood.Friction == 0.6f && wood.Restitution == 0.0f && wood.Density == 600.0f, "The wood's values, the engine's where it sets none");
		Require(wood.Extras.size() == 2 && wood.Extras[0].Name == "Flammability" && wood.Extras[1].Name == "Hardness", "The game's extras, in the file's order");
		Require(GetExtra(wood, "Flammability", 0.0f) == 0.8f && GetExtra(wood, "Hardness", 0.0f) == 2.0f, "Its extras should be served by name");
		Require(GetExtra(wood, "Wetness", 0.25f) == 0.25f, "An extra it doesn't have should be the fallback");
		Require(asset.ToText() == WoodText, "Read and written unchanged, the text should be the same");

		// Values changed: their own lines change, an extra gone loses its line, a new one gets one;
		// the comment, the blank line and the line Nyx doesn't know stay
		PhysicsMaterialData changed = wood;
		changed.Friction = 0.3f;
		changed.Restitution = 0.25f;
		changed.Extras.erase(changed.Extras.begin() + 1);
		changed.Extras.push_back(PhysicsMaterialExtra{ .Name = "Smoke", .Value = 0.5f });
		changed.Extras.push_back(PhysicsMaterialExtra{ .Name = "Not a name", .Value = 1.0f });
		asset.Write(changed);
		const std::string expected = "# Planks and crates\n"
									 "Friction = 0.3\n"
									 "Density = 600\n"
									 "Grain = along\n"
									 "\n"
									 "Extra.Flammability = 0.8\n"
									 "Restitution = 0.25\n"
									 "Extra.Smoke = 0.5\n";
		Require(asset.ToText() == expected, "Only the changed values' lines should change:\n" + asset.ToText());
		Require(asset.FindValue("Grain") && *asset.FindValue("Grain") == "along", "A line Nyx doesn't know should be kept");

		// Written to a file and read back
		const std::filesystem::path file = directory / "Wood.nyxphysmat";
		Require(asset.Save(file, error), "The wood should be saved: " + error);
		PhysicsMaterialAsset loaded;
		Require(loaded.Load(file, error) && loaded.ToText() == expected, "The wood should load as saved");

		// What can't be read
		PhysicsMaterialAsset broken;
		Require(!broken.Parse("Friction = 0.5\nnot a line\n", error) && error.find("line 2") != std::string::npos, "A line that isn't Name = value should be refused: " + error);
		Require(broken.Parse("Friction = slippery\nExtra.Heat = 3\n", error), "A value that isn't a number is still a line");
		problems.clear();
		const PhysicsMaterialData slippery = broken.Read(&problems);
		Require(problems.size() == 1 && slippery.Friction == 0.5f && GetExtra(slippery, "Heat", 0.0f) == 3.0f,
			"A value that isn't a number should be listed and left at the engine's");
	}

	// A static slope turned 30 degrees about Z, and a box resting on it, both in the game world
	struct Slope
	{
		Entity Ground;
		Entity Box;
		glm::vec3 Start{ 0.0f };
	};

	Slope AddSlope(Registry& world, const std::string& boxMaterial)
	{
		const glm::quat tilt = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::vec3 up = tilt * glm::vec3(0.0f, 1.0f, 0.0f);
		Slope slope;
		slope.Ground = world.CreateEntity();
		world.Add<TransformComponent>(slope.Ground, TransformComponent{ .Rotation = tilt });
		world.Add<BoxColliderComponent>(slope.Ground, BoxColliderComponent{ .HalfExtents = glm::vec3(10.0f, 0.5f, 4.0f) });

		slope.Start = up * 1.02f;
		slope.Box = world.CreateEntity();
		world.Add<TransformComponent>(slope.Box, TransformComponent{ .Position = slope.Start, .Rotation = tilt });
		world.Add<BoxColliderComponent>(slope.Box, BoxColliderComponent{ .Material = AssetReference{ .Type = "PhysicsMaterial", .Path = boxMaterial } });
		world.Add<RigidBodyComponent>(slope.Box, RigidBodyComponent{});
		return slope;
	}

	float SlidOf(const GameWorld& world, const Slope& slope)
	{
		return glm::length(world.GetRegistry().Get<TransformComponent>(slope.Box).Position - slope.Start);
	}

	void TestFrictionDecidesWhetherABoxSlides(const std::filesystem::path& assets)
	{
		// tan 30 degrees is 0.58; Jolt takes the square root of the two frictions' product, and the
		// slope has the engine's 0.5: 0.9 holds the box (0.67), 0.1 doesn't (0.22)
		WriteFile(assets / "PhysicsMaterials" / "Rubber.nyxphysmat", "Friction = 0.9\n");
		WriteFile(assets / "PhysicsMaterials" / "Ice.nyxphysmat", "Friction = 0.1\n");

		GameWorld world;
		world.GetPhysicsMaterials().SetSearchDirs({ assets });
		Steps steps{ world };
		const Slope rubber = AddSlope(world.GetRegistry(), "PhysicsMaterials/Rubber.nyxphysmat");
		Slope ice = AddSlope(world.GetRegistry(), "PhysicsMaterials/Ice.nyxphysmat");
		// Side by side along the slope's width
		for (const Entity entity : { ice.Ground, ice.Box })
		{
			world.GetRegistry().Get<TransformComponent>(entity).Position.z += 10.0f;
		}
		ice.Start.z += 10.0f;

		steps.Run(120);
		Require(world.GetPhysics().GetFriction(world.GetBodies().FindBody(rubber.Box)) == 0.9f, "The rubber box's body should have its material's friction");
		Require(SlidOf(world, rubber) < 0.05f, "The rubber box should hold on the slope (" + std::to_string(SlidOf(world, rubber)) + " m)");
		Require(SlidOf(world, ice) > 1.0f, "The ice box should slide down the slope (" + std::to_string(SlidOf(world, ice)) + " m)");

		// The rubber turned to ice while the game runs: the same body slides now
		const BodyHandle body = world.GetBodies().FindBody(rubber.Box);
		const float held = SlidOf(world, rubber);
		Require(world.GetPhysicsMaterials().SetText("PhysicsMaterials/Rubber.nyxphysmat", "Friction = 0.05\n"), "The live values should be taken");
		steps.Run(60);
		Require(world.GetBodies().FindBody(rubber.Box) == body, "A changed friction shouldn't make the body again");
		Require(world.GetPhysics().GetFriction(body) == 0.05f, "The body should have the live friction");
		Require(SlidOf(world, rubber) > held + 0.5f, "With the live friction the box should slide (" + std::to_string(SlidOf(world, rubber)) + " m)");

		// The file read again: what it says, again
		Require(world.GetPhysicsMaterials().Reload("PhysicsMaterials/Rubber.nyxphysmat"), "The file should be read again");
		steps.Run(1);
		Require(world.GetPhysics().GetFriction(body) == 0.9f, "Read again, the body should have the file's friction");
	}

	void TestDensityGivesTheMass(const std::filesystem::path& assets)
	{
		WriteFile(assets / "PhysicsMaterials" / "Pine.nyxphysmat", "Density = 600\nExtra.Flammability = 0.8\n");

		GameWorld world;
		world.GetPhysicsMaterials().SetSearchDirs({ assets });
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const AssetReference pine{ .Type = "PhysicsMaterial", .Path = "PhysicsMaterials/Pine.nyxphysmat" };
		// A cubic metre of pine, one of the engine's material, and one of pine given its mass
		const Entity crate = registry.CreateEntity();
		registry.Add<TransformComponent>(crate, TransformComponent{ .Position = glm::vec3(0.0f, 5.0f, 0.0f) });
		registry.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .Material = pine });
		registry.Add<RigidBodyComponent>(crate, RigidBodyComponent{});
		const Entity water = registry.CreateEntity();
		registry.Add<TransformComponent>(water, TransformComponent{ .Position = glm::vec3(3.0f, 5.0f, 0.0f) });
		registry.Add<BoxColliderComponent>(water, BoxColliderComponent{});
		registry.Add<RigidBodyComponent>(water, RigidBodyComponent{});
		const Entity given = registry.CreateEntity();
		registry.Add<TransformComponent>(given, TransformComponent{ .Position = glm::vec3(6.0f, 5.0f, 0.0f) });
		registry.Add<BoxColliderComponent>(given, BoxColliderComponent{ .Material = pine });
		registry.Add<RigidBodyComponent>(given, RigidBodyComponent{ .Mass = 5.0f });

		steps.Run(1);
		const PhysicsWorld& physics = world.GetPhysics();
		Require(Near(physics.GetMass(world.GetBodies().FindBody(crate)), 600.0f, 1.0f), "A cubic metre of pine should weigh 600 kg (" + std::to_string(physics.GetMass(world.GetBodies().FindBody(crate))) + ")");
		Require(Near(physics.GetMass(world.GetBodies().FindBody(water)), 1000.0f, 1.0f), "Without a material it should weigh as water");
		Require(Near(physics.GetMass(world.GetBodies().FindBody(given)), 5.0f, 1e-3f), "A mass given should win over the density");

		// Denser live: the crate's body is made again with the new mass; the one given its mass stays
		const BodyHandle crateBody = world.GetBodies().FindBody(crate);
		const BodyHandle givenBody = world.GetBodies().FindBody(given);
		world.GetPhysicsMaterials().SetText(pine.Path, "Density = 2500\nExtra.Flammability = 0.8\n");
		steps.Run(1);
		Require(world.GetBodies().FindBody(crate) != crateBody && Near(physics.GetMass(world.GetBodies().FindBody(crate)), 2500.0f, 2.0f),
			"A changed density should make the body again with its mass");
		Require(world.GetBodies().FindBody(given) == givenBody, "A body given its mass shouldn't be made again for a density");
	}

	void TestAGameReadsItsOwnNumbers(const std::filesystem::path& assets)
	{
		WriteFile(assets / "PhysicsMaterials" / "Straw.nyxphysmat", "# Burns at once\nExtra.Flammability = 0.95\nExtra.Ignition = 0.2\n");

		GameWorld world;
		world.GetPhysicsMaterials().SetSearchDirs({ assets });
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const Entity bale = registry.CreateEntity();
		registry.Add<TransformComponent>(bale, TransformComponent{});
		registry.Add<SphereColliderComponent>(bale, SphereColliderComponent{ .Material = AssetReference{ .Type = "PhysicsMaterial", .Path = "PhysicsMaterials/Straw.nyxphysmat" } });
		const Entity plain = registry.CreateEntity();
		registry.Add<CapsuleColliderComponent>(plain, CapsuleColliderComponent{});
		const Entity lost = registry.CreateEntity();
		registry.Add<BoxColliderComponent>(lost, BoxColliderComponent{ .Material = AssetReference{ .Type = "PhysicsMaterial", .Path = "PhysicsMaterials/Gone.nyxphysmat" } });
		const Entity nothing = registry.CreateEntity();

		// A system reads them as a game's would, through its context
		float baleFlammability = -1.0f;
		world.AddSystem(std::make_unique<CallingSystem>([&](const SimContext& context)
			{
				baleFlammability = GetExtra(GetPhysicsMaterial(context, bale), "Flammability", 0.0f);
			}));
		steps.Run(1);
		Require(baleFlammability == 0.95f, "A system should read the bale's flammability");
		Require(GetExtra(world.GetPhysicsMaterial(bale), "Ignition", 1.0f) == 0.2f, "The game world should serve the bale's other extra");
		Require(GetExtra(world.GetPhysicsMaterial(plain), "Flammability", 0.1f) == 0.1f && world.GetPhysicsMaterial(plain).Friction == 0.5f,
			"A collider without a material should have the engine's, without extras");
		Require(world.GetPhysicsMaterial(lost).Extras.empty() && world.GetPhysicsMaterial(lost).Density == 1000.0f,
			"A material that isn't there should have the engine's values");
		Require(&world.GetPhysicsMaterial(nothing) == &PhysicsMaterials::GetDefault(), "An entity without a collider should have the engine's");

		// The editor's values for it, an extra of a game's the engine has never heard of among them
		const uint64_t revision = world.GetPhysicsMaterials().GetRevision();
		Require(world.GetPhysicsMaterials().SetText("PhysicsMaterials/Straw.nyxphysmat", "Extra.Flammability = 0.5\nExtra.Glow = 3\n"), "The live values should be taken");
		Require(world.GetPhysicsMaterials().GetRevision() > revision, "Live values should count a revision");
		Require(GetExtra(world.GetPhysicsMaterial(bale), "Glow", 0.0f) == 3.0f && GetExtra(world.GetPhysicsMaterial(bale), "Flammability", 0.0f) == 0.5f,
			"The live values should be served");
		Require(!world.GetPhysicsMaterials().SetText("PhysicsMaterials/Straw.nyxphysmat", "broken"), "Text that can't be read should be refused");
		Require(GetExtra(world.GetPhysicsMaterial(bale), "Glow", 0.0f) == 3.0f, "Refused text should change nothing");
	}

	void TestScenesKeepTheReference(const std::filesystem::path& directory)
	{
		Registry world;
		const Entity crate = world.CreateEntity();
		world.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .Material = AssetReference{ .Type = "PhysicsMaterial", .Path = "PhysicsMaterials/Wood.nyxphysmat" } });
		const std::filesystem::path path = directory / "PhysicsMaterials.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with a physics material failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with a physics material failed");
		int found = 0;
		loaded.Each<BoxColliderComponent>([&](Entity, BoxColliderComponent& box)
			{
				found += box.Material.Path == "PhysicsMaterials/Wood.nyxphysmat" && box.Material.Type == "PhysicsMaterial" ? 1 : 0;
			});
		Require(found == 1, "The collider's physics material should be kept");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	Nyx::RegisterComponentTypes();

	try
	{
		Nyx::Tests::TestDirectory directory("NyxPhysicsMaterialTests");
		TestAFileIsReadAndWritten(directory.Path);
		TestFrictionDecidesWhetherABoxSlides(directory.Path);
		TestDensityGivesTheMass(directory.Path);
		TestAGameReadsItsOwnNumbers(directory.Path);
		TestScenesKeepTheReference(directory.Path);
	}
	catch (const std::exception& error)
	{
		std::cerr << "Physics material test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All physics material tests passed.\n";
	return 0;
}
