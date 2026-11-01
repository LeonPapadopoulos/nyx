// Transform hierarchy: a child placed relative to its parent following it as it moves, turns and is
// scaled, local values made from a place in the world and back, chains whatever order their entities
// were made in, links to nothing and in circles moving nothing, what moved reported, and a child
// saved and loaded with its link.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "Log.h"
#include "ParentComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <vector>

namespace
{
	using Nyx::EntityGuid;
	using Nyx::ParentComponent;
	using Nyx::TransformComponent;

	using Nyx::Tests::Require;

	constexpr float Tolerance = 1e-4f;

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < Tolerance;
	}

	// The same turn: q and -q are one rotation
	bool Near(const glm::quat& a, const glm::quat& b)
	{
		return std::abs(std::abs(glm::dot(a, b)) - 1.0f) < Tolerance;
	}

	bool Near(const TransformComponent& a, const TransformComponent& b)
	{
		return Near(a.Position, b.Position) && Near(a.Rotation, b.Rotation) && Near(a.Scale, b.Scale);
	}

	TransformComponent MakeTransform(const glm::vec3& position, const glm::vec3& eulerDegrees, const glm::vec3& scale)
	{
		TransformComponent transform;
		transform.Position = position;
		transform.Rotation = glm::normalize(glm::quat(glm::radians(eulerDegrees)));
		transform.Scale = scale;
		return transform;
	}

	Nyx::Entity CreateEntity(Nyx::Registry& world, EntityGuid guid, const TransformComponent& transform)
	{
		const Nyx::Entity entity = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(entity, Nyx::GuidComponent{ guid });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	ParentComponent MakeLink(EntityGuid parent, const glm::vec3& position, const glm::vec3& eulerDegrees = glm::vec3(0.0f), const glm::vec3& scale = glm::vec3(1.0f))
	{
		ParentComponent link;
		link.Parent = parent;
		link.LocalPosition = position;
		link.LocalRotation = glm::normalize(glm::quat(glm::radians(eulerDegrees)));
		link.LocalScale = scale;
		return link;
	}

	std::vector<Nyx::Entity> Update(Nyx::Registry& world)
	{
		std::vector<Nyx::Entity> moved;
		Nyx::UpdateTransformHierarchy(world, Nyx::MakeEntityGuidLookup(world), &moved);
		return moved;
	}

	void TestAChildFollowsItsParent()
	{
		Nyx::Registry world;
		const Nyx::Entity parent = CreateEntity(world, EntityGuid{ 1 }, MakeTransform(glm::vec3(10.0f, 0.0f, 0.0f), glm::vec3(0.0f, 90.0f, 0.0f), glm::vec3(2.0f)));
		const Nyx::Entity child = CreateEntity(world, EntityGuid{ 2 }, TransformComponent{});
		world.Add<ParentComponent>(child, MakeLink(EntityGuid{ 1 }, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 30.0f), glm::vec3(0.5f)));

		Update(world);

		// The same as the parent's matrix times the local one, with an even scale
		const ParentComponent& link = world.Get<ParentComponent>(child);
		const TransformComponent localTransform = MakeTransform(link.LocalPosition, glm::vec3(0.0f, 0.0f, 30.0f), link.LocalScale);
		const glm::mat4 expected = world.Get<TransformComponent>(parent).ToMatrix() * localTransform.ToMatrix();
		const TransformComponent& placed = world.Get<TransformComponent>(child);
		Require(Near(placed.Position, glm::vec3(expected[3])), "The child should be placed by its parent's matrix");
		Require(Near(placed.Position, glm::vec3(10.0f, 0.0f, -2.0f)), "Turned 90 degrees round Y and scaled by 2, local X should end up 2 along -Z");
		Require(Near(placed.Scale, glm::vec3(1.0f)), "The scales should multiply");
		const glm::mat4 placedMatrix = placed.ToMatrix();
		for (int column = 0; column < 4; ++column)
		{
			Require(Near(glm::vec3(placedMatrix[column]), glm::vec3(expected[column])), "The child's rotation should be its parent's then its own");
		}

		// The parent moves, turns and grows: the child goes with it
		world.Get<TransformComponent>(parent) = MakeTransform(glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 1.0f, 3.0f));
		Update(world);
		Require(Near(world.Get<TransformComponent>(child).Position, glm::vec3(1.0f, 5.0f, 0.0f)), "The child should follow its parent's move");
		Require(Near(world.Get<TransformComponent>(child).Scale, glm::vec3(0.5f, 0.5f, 1.5f)), "An uneven scale should multiply axis by axis");
	}

	void TestLocalValuesKeepAChildWhereItIs()
	{
		const TransformComponent parentWorld = MakeTransform(glm::vec3(3.0f, -1.0f, 4.0f), glm::vec3(20.0f, 70.0f, -10.0f), glm::vec3(2.0f));
		const TransformComponent childWorld = MakeTransform(glm::vec3(-5.0f, 2.0f, 1.0f), glm::vec3(-40.0f, 10.0f, 80.0f), glm::vec3(3.0f));

		ParentComponent link;
		Nyx::SetLocalTransform(link, parentWorld, childWorld);
		Require(Near(Nyx::GetChildWorldTransform(parentWorld, link), childWorld), "The local values should put the child back where it was");
		Require(Near(link.LocalScale, glm::vec3(1.5f)), "The local scale should be the world one over the parent's");

		// A parent flattened to nothing on an axis: that axis gives 0, never a division by 0
		const TransformComponent flat = MakeTransform(glm::vec3(0.0f), glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 1.0f));
		Nyx::SetLocalTransform(link, flat, childWorld);
		Require(link.LocalPosition.y == 0.0f && link.LocalScale.y == 0.0f, "An axis scaled to 0 should give 0");
		Require(std::isfinite(link.LocalPosition.x) && std::isfinite(link.LocalScale.x), "The other axes should stay numbers");
	}

	void TestChainsWhateverTheOrderTheyWereMadeIn()
	{
		// The grandchild made first, the root last: the world holds them child first
		Nyx::Registry world;
		const Nyx::Entity grandchild = CreateEntity(world, EntityGuid{ 3 }, TransformComponent{});
		const Nyx::Entity child = CreateEntity(world, EntityGuid{ 2 }, TransformComponent{});
		const Nyx::Entity root = CreateEntity(world, EntityGuid{ 1 }, MakeTransform(glm::vec3(100.0f, 0.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f)));
		world.Add<ParentComponent>(grandchild, MakeLink(EntityGuid{ 2 }, glm::vec3(0.0f, 0.0f, 1.0f)));
		world.Add<ParentComponent>(child, MakeLink(EntityGuid{ 1 }, glm::vec3(0.0f, 10.0f, 0.0f)));

		const std::vector<Nyx::Entity> moved = Update(world);
		Require(Near(world.Get<TransformComponent>(grandchild).Position, glm::vec3(100.0f, 10.0f, 1.0f)), "One update should place a whole chain");
		Require(moved == std::vector<Nyx::Entity>{ child, grandchild }, "What moved should be listed parents first");

		// Nothing changed: nothing moves, nothing is written
		Require(Update(world).empty(), "A second update with nothing changed should move nothing");

		// Only the child is moved by hand: only it and below follow
		world.Get<ParentComponent>(child).LocalPosition = glm::vec3(0.0f, 20.0f, 0.0f);
		Require(Update(world) == std::vector<Nyx::Entity>{ child, grandchild }, "A changed link should move it and its children");
		Require(Near(world.Get<TransformComponent>(root).Position, glm::vec3(100.0f, 0.0f, 0.0f)), "A root should never be moved");
	}

	void TestLinksToNothingMoveNothing()
	{
		Nyx::Registry world;
		const TransformComponent where = MakeTransform(glm::vec3(7.0f, 8.0f, 9.0f), glm::vec3(0.0f, 45.0f, 0.0f), glm::vec3(1.0f));
		const Nyx::Entity none = CreateEntity(world, EntityGuid{ 1 }, where);
		world.Add<ParentComponent>(none, MakeLink(EntityGuid{}, glm::vec3(1.0f)));
		const Nyx::Entity missing = CreateEntity(world, EntityGuid{ 2 }, where);
		world.Add<ParentComponent>(missing, MakeLink(EntityGuid{ 0x404 }, glm::vec3(1.0f)));
		const Nyx::Entity itself = CreateEntity(world, EntityGuid{ 3 }, where);
		world.Add<ParentComponent>(itself, MakeLink(EntityGuid{ 3 }, glm::vec3(1.0f)));

		// A parent deleted after its child was placed
		const Nyx::Entity parent = CreateEntity(world, EntityGuid{ 4 }, TransformComponent{});
		const Nyx::Entity orphan = CreateEntity(world, EntityGuid{ 5 }, TransformComponent{});
		world.Add<ParentComponent>(orphan, MakeLink(EntityGuid{ 4 }, glm::vec3(0.0f, 3.0f, 0.0f)));
		Update(world);
		world.DestroyEntity(parent);

		Require(Update(world).empty(), "Links to nothing should move nothing");
		Require(Near(world.Get<TransformComponent>(none), where) && Near(world.Get<TransformComponent>(missing), where) &&
				Near(world.Get<TransformComponent>(itself), where),
			"An entity linked to no parent, a missing one or itself should stay where it is");
		Require(Near(world.Get<TransformComponent>(orphan).Position, glm::vec3(0.0f, 3.0f, 0.0f)), "A child whose parent was deleted should stay where it was last placed");
	}

	void TestCirclesMoveNothing()
	{
		Nyx::Registry world;
		const TransformComponent where = MakeTransform(glm::vec3(1.0f, 2.0f, 3.0f), glm::vec3(0.0f), glm::vec3(1.0f));
		const Nyx::Entity a = CreateEntity(world, EntityGuid{ 1 }, where);
		const Nyx::Entity b = CreateEntity(world, EntityGuid{ 2 }, where);
		const Nyx::Entity c = CreateEntity(world, EntityGuid{ 3 }, where);
		const Nyx::Entity below = CreateEntity(world, EntityGuid{ 4 }, where);
		world.Add<ParentComponent>(a, MakeLink(EntityGuid{ 3 }, glm::vec3(1.0f)));
		world.Add<ParentComponent>(b, MakeLink(EntityGuid{ 1 }, glm::vec3(1.0f)));
		world.Add<ParentComponent>(c, MakeLink(EntityGuid{ 2 }, glm::vec3(1.0f)));
		world.Add<ParentComponent>(below, MakeLink(EntityGuid{ 2 }, glm::vec3(1.0f)));

		// A chain beside it still works
		const Nyx::Entity root = CreateEntity(world, EntityGuid{ 5 }, TransformComponent{});
		const Nyx::Entity child = CreateEntity(world, EntityGuid{ 6 }, TransformComponent{});
		world.Add<ParentComponent>(child, MakeLink(EntityGuid{ 5 }, glm::vec3(0.0f, 1.0f, 0.0f)));

		const std::vector<Nyx::Entity> moved = Update(world);
		Require(moved == std::vector<Nyx::Entity>{ child }, "Only the chain beside the circle should move");
		for (const Nyx::Entity entity : { a, b, c, below })
		{
			Require(Near(world.Get<TransformComponent>(entity), where), "An entity in a circle, or below one, should stay where it is");
		}
		Require(Near(world.Get<TransformComponent>(child).Position, world.Get<TransformComponent>(root).Position + glm::vec3(0.0f, 1.0f, 0.0f)),
			"A chain beside a circle should be placed");
	}

	void TestSavedAndLoaded()
	{
		Nyx::Tests::TestDirectory directory("NyxTransformHierarchyTests");
		const std::filesystem::path scenePath = directory.Path / "Hierarchy.nyxscene";

		Nyx::Registry world;
		CreateEntity(world, EntityGuid{ 1 }, MakeTransform(glm::vec3(0.0f, 2.0f, 0.0f), glm::vec3(0.0f), glm::vec3(1.0f)));
		const Nyx::Entity child = CreateEntity(world, EntityGuid{ 2 }, TransformComponent{});
		const ParentComponent link = MakeLink(EntityGuid{ 1 }, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 30.0f, 0.0f), glm::vec3(2.0f));
		world.Add<ParentComponent>(child, link);
		Update(world);
		const TransformComponent placed = world.Get<TransformComponent>(child);
		Require(Nyx::SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with a child failed");

		Nyx::Registry loaded;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, loaded), "Loading a scene with a child failed");
		std::optional<ParentComponent> read;
		std::optional<TransformComponent> readTransform;
		loaded.Each<ParentComponent>([&](Nyx::Entity entity, const ParentComponent& component)
			{
				read = component;
				readTransform = loaded.Get<TransformComponent>(entity);
			});
		Require(read && read->Parent == link.Parent && read->LocalPosition == link.LocalPosition && read->LocalRotation == link.LocalRotation &&
				read->LocalScale == link.LocalScale,
			"The link should load as saved");

		// The world transform is saved as it was placed: a game reads it as it is
		Require(readTransform && readTransform->Position == placed.Position && readTransform->Rotation == placed.Rotation &&
				readTransform->Scale == placed.Scale,
			"The child's place in the world should load as saved");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestAChildFollowsItsParent();
		TestLocalValuesKeepAChildWhereItIs();
		TestChainsWhateverTheOrderTheyWereMadeIn();
		TestLinksToNothingMoveNothing();
		TestCirclesMoveNothing();
		TestSavedAndLoaded();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All transform hierarchy tests passed\n";
	return 0;
}
