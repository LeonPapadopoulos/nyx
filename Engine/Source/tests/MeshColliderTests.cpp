// Mesh colliders (MeshColliderComponent): static bodies of the triangles of an entity's mesh made at
// run time (RuntimeMeshComponent), with its scale. A box dropped on a mesh slope slides down it and
// comes to rest on the mesh's floor; a ray hits the face it points at, with that face's normal, the
// scale applied; a character walks up a mesh ramp less steep than its max slope and not up a steeper
// one, and climbs mesh steps as it climbs boxes; changing the mesh and raising its revision makes the
// body again once, and nothing else does; a rigid body doesn't make it move; the debug view draws its
// triangles' edges, and a mesh of more triangles than it draws as its bounds. The sample ground is
// valid, made once, and made again when its values change; the sample scene loads, and its character
// and crates stand on the ground.
//
// NyxMeshColliderTests --write-scene writes the sample scene, Assets/Scenes/MeshTest.nyxscene: this
// file is its source.
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "DebugDraw.h"
#include "DirectionalLightComponent.h"
#include "GameWorld.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "OrbitCameraComponent.h"
#include "Paths.h"
#include "PhysicsComponents.h"
#include "PhysicsPick.h"
#include "PreviousTransformComponent.h"
#include "RuntimeMeshComponent.h"
#include "SampleGround.h"
#include "SampleGroundComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float StepSeconds = 1.0f / 60.0f;

	struct Steps
	{
		GameWorld& World;
		uint64_t Next = 0;

		void Run(int count)
		{
			for (int i = 0; i < count; ++i, ++Next)
			{
				World.Step(StepTime{ StepSeconds, Next, static_cast<double>(Next) * StepSeconds });
			}
		}
	};

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	// A quad through four corners, counter-clockwise seen from the side that collides, at the end of
	// the mesh
	void AddQuad(MeshData& mesh, const glm::vec3& a, const glm::vec3& b, const glm::vec3& c, const glm::vec3& d)
	{
		const uint32_t first = static_cast<uint32_t>(mesh.Vertices.size());
		const glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
		for (const glm::vec3& corner : { a, b, c, d })
		{
			mesh.Vertices.push_back(Vertex{ .Position = corner, .Normal = normal });
		}
		mesh.Indices.insert(mesh.Indices.end(), { first, first + 1, first + 2, first + 2, first + 3, first });
	}

	// A flat square at height `y`, facing up, from `min` to `max` in x and z
	void AddFloorQuad(MeshData& mesh, float y, const glm::vec2& min, const glm::vec2& max)
	{
		AddQuad(mesh, glm::vec3(min.x, y, max.y), glm::vec3(max.x, y, max.y), glm::vec3(max.x, y, min.y), glm::vec3(min.x, y, min.y));
	}

	// A closed box between two corners, its faces outwards
	void AddBox(MeshData& mesh, const glm::vec3& min, const glm::vec3& max)
	{
		const glm::vec3 p[8] = { { min.x, min.y, min.z }, { max.x, min.y, min.z }, { max.x, max.y, min.z }, { min.x, max.y, min.z },
			{ min.x, min.y, max.z }, { max.x, min.y, max.z }, { max.x, max.y, max.z }, { min.x, max.y, max.z } };
		AddQuad(mesh, p[4], p[5], p[6], p[7]); // +Z
		AddQuad(mesh, p[1], p[0], p[3], p[2]); // -Z
		AddQuad(mesh, p[5], p[1], p[2], p[6]); // +X
		AddQuad(mesh, p[0], p[4], p[7], p[3]); // -X
		AddQuad(mesh, p[7], p[6], p[2], p[3]); // +Y
		AddQuad(mesh, p[0], p[1], p[5], p[4]); // -Y
	}

	// An entity whose mesh, made in code, is its static collider
	Entity AddMeshCollider(Registry& world, std::shared_ptr<const MeshData> mesh, const TransformComponent& transform = {})
	{
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, transform);
		RuntimeMeshComponent runtimeMesh;
		SetRuntimeMesh(runtimeMesh, std::move(mesh));
		world.Add<RuntimeMeshComponent>(entity, runtimeMesh);
		world.Add<MeshColliderComponent>(entity, MeshColliderComponent{});
		return entity;
	}

	glm::vec3 PositionOf(GameWorld& world, Entity entity)
	{
		return world.GetRegistry().Get<TransformComponent>(entity).Position;
	}

	// A slope of 35 degrees falling towards +X from x = -4 to 4, then a floor to x = 30: a box
	// dropped on the slope lands on it, slides down onto the floor and comes to rest there
	void TestABoxSlidesDownAMeshSlope()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		const float drop = std::tan(glm::radians(35.0f));
		const float floorY = -4.0f * drop;
		auto mesh = std::make_shared<MeshData>();
		AddQuad(*mesh, glm::vec3(-4.0f, 4.0f * drop, 3.0f), glm::vec3(4.0f, floorY, 3.0f), glm::vec3(4.0f, floorY, -3.0f), glm::vec3(-4.0f, 4.0f * drop, -3.0f));
		AddFloorQuad(*mesh, floorY, glm::vec2(4.0f, -3.0f), glm::vec2(30.0f, 3.0f));
		AddMeshCollider(registry, mesh);

		const Entity box = registry.CreateEntity();
		registry.Add<TransformComponent>(box, TransformComponent{ .Position = glm::vec3(-2.0f, 2.0f * drop + 1.0f, 0.0f) });
		registry.Add<BoxColliderComponent>(box, BoxColliderComponent{ .HalfExtents = glm::vec3(0.25f) });
		registry.Add<RigidBodyComponent>(box, RigidBodyComponent{});
		registry.Add<PhysicsMaterialComponent>(box, PhysicsMaterialComponent{ .Friction = 0.2f });

		// Until it touches the slope, then a little longer: on it, not through it
		bool bLanded = false;
		for (int step = 0; step < 60 && !bLanded; ++step)
		{
			steps.Run(1);
			const glm::vec3 position = PositionOf(world, box);
			const float above = (position.y + position.x * drop) * std::cos(glm::radians(35.0f));
			bLanded = above < 0.4f;
		}
		Require(bLanded, "The box should land on the slope " + Describe(PositionOf(world, box)));
		const glm::vec3 landed = PositionOf(world, box);
		steps.Run(30);
		const glm::vec3 sliding = PositionOf(world, box);
		const float above = (sliding.y + sliding.x * drop) * std::cos(glm::radians(35.0f));
		// From half its side, lying on a face, to half its diagonal, on an edge
		Require(above > 0.2f && above < 0.4f, "The box should lie on the slope, not sink into it " + Describe(sliding));
		Require(sliding.x > landed.x + 0.1f, "The box should slide down the slope " + Describe(landed) + " " + Describe(sliding));

		steps.Run(600);
		const glm::vec3 rest = PositionOf(world, box);
		Require(rest.x > 4.0f && std::abs(rest.y - (floorY + 0.25f)) < 0.03f, "The box should come to rest on the floor below the slope " + Describe(rest));
		Require(!registry.Has<PreviousTransformComponent>(box), "The box should be at rest");
	}

	// A roof of two faces, ridge along Z at x = 0 and 1 m high, eaves at x = -1 and 1, put 1 m up and
	// stretched twice along X: a ray down hits the face beneath it, where it is, with its normal
	void TestARayHitsTheFaceItPointsAt()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		auto mesh = std::make_shared<MeshData>();
		AddQuad(*mesh, glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(0.0f, 1.0f, -1.0f), glm::vec3(-1.0f, 0.0f, -1.0f));
		AddQuad(*mesh, glm::vec3(0.0f, 1.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, -1.0f), glm::vec3(0.0f, 1.0f, -1.0f));
		const Entity roof = AddMeshCollider(registry, mesh, TransformComponent{ .Position = glm::vec3(0.0f, 1.0f, 0.0f), .Scale = glm::vec3(2.0f, 1.0f, 1.0f) });
		steps.Run(1);
		Require(world.GetBodies().FindBody(roof).IsValid(), "The mesh collider should make a body");

		// Stretched, the faces rise 1 m over 2 m: their normals lean 1 in 2 off the vertical
		const glm::vec3 leftNormal = glm::normalize(glm::vec3(-1.0f, 2.0f, 0.0f));
		const glm::vec3 rightNormal = glm::normalize(glm::vec3(1.0f, 2.0f, 0.0f));
		const glm::vec3 down(0.0f, -1.0f, 0.0f);
		const std::optional<PhysicsPick> left = PickPhysicsBody(world, Ray{ glm::vec3(-1.0f, 5.0f, 0.3f), down }, 10.0f);
		Require(left && left->HitEntity == roof, "A ray down onto the roof's left face should pick the roof");
		Require(glm::length(left->Point - glm::vec3(-1.0f, 1.5f, 0.3f)) < 1e-3f, "The ray should hit the left face halfway up " + Describe(left->Point));
		Require(glm::length(left->Normal - leftNormal) < 1e-3f, "The left face's normal should lean left " + Describe(left->Normal));

		const std::optional<PhysicsPick> right = PickPhysicsBody(world, Ray{ glm::vec3(1.5f, 5.0f, -0.6f), down }, 10.0f);
		Require(right && right->HitEntity == roof, "A ray down onto the roof's right face should pick the roof");
		Require(glm::length(right->Point - glm::vec3(1.5f, 1.25f, -0.6f)) < 1e-3f, "The ray should hit the right face a quarter up " + Describe(right->Point));
		Require(glm::length(right->Normal - rightNormal) < 1e-3f, "The right face's normal should lean right " + Describe(right->Normal));

		Require(!PickPhysicsBody(world, Ray{ glm::vec3(2.5f, 5.0f, 0.0f), down }, 10.0f), "A ray past the stretched eaves should miss the roof");
		// From below, through the back of a face: one-sided, as a ground is
		Require(!PickPhysicsBody(world, Ray{ glm::vec3(-1.0f, 0.0f, 0.0f), -down }, 10.0f), "A ray from below should pass through the roof");
	}

	// Walks a character along +X for 1.5 s towards a mesh ramp of `degrees` rising from x = 2 for 8 m,
	// on a mesh floor
	glm::vec3 WalkAtARamp(float degrees)
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		auto mesh = std::make_shared<MeshData>();
		AddFloorQuad(*mesh, 0.0f, glm::vec2(-30.0f), glm::vec2(30.0f));
		const glm::vec3 top(2.0f + 8.0f * std::cos(glm::radians(degrees)), 8.0f * std::sin(glm::radians(degrees)), 0.0f);
		AddQuad(*mesh, glm::vec3(2.0f, 0.0f, 2.0f), top + glm::vec3(0.0f, 0.0f, 2.0f), top + glm::vec3(0.0f, 0.0f, -2.0f), glm::vec3(2.0f, 0.0f, -2.0f));
		AddMeshCollider(registry, mesh);
		const Entity character = registry.CreateEntity();
		registry.Add<TransformComponent>(character, TransformComponent{});
		registry.Add<CharacterComponent>(character, CharacterComponent{});
		steps.Run(10);
		registry.Get<CharacterComponent>(character).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		steps.Run(90);
		return PositionOf(world, character);
	}

	// Walks a character along +X for 1.2 s towards a mesh block of `height` from x = 2, on a mesh floor
	glm::vec3 WalkAtAStep(float height)
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		auto mesh = std::make_shared<MeshData>();
		AddFloorQuad(*mesh, 0.0f, glm::vec2(-30.0f), glm::vec2(30.0f));
		AddBox(*mesh, glm::vec3(2.0f, 0.0f, -2.0f), glm::vec3(8.0f, height, 2.0f));
		AddMeshCollider(registry, mesh);
		const Entity character = registry.CreateEntity();
		registry.Add<TransformComponent>(character, TransformComponent{});
		registry.Add<CharacterComponent>(character, CharacterComponent{});
		steps.Run(10);
		registry.Get<CharacterComponent>(character).DesiredMove = glm::vec3(1.0f, 0.0f, 0.0f);
		steps.Run(72);
		return PositionOf(world, character);
	}

	void TestTheCharacterWalksOnMeshes()
	{
		// The default max slope is 45 degrees
		const glm::vec3 gentle = WalkAtARamp(25.0f);
		Require(gentle.y > 1.0f && gentle.x > 4.0f, "The character should walk up a mesh ramp of 25 degrees " + Describe(gentle));
		const glm::vec3 steep = WalkAtARamp(50.0f);
		Require(steep.y < 0.3f && steep.x < 2.5f, "The character shouldn't walk up a mesh ramp of 50 degrees " + Describe(steep));

		// As on boxes (NyxCharacterTests): the default step height is 0.3 m
		const glm::vec3 low = WalkAtAStep(0.25f);
		Require(std::abs(low.y - 0.25f) < 0.03f && low.x > 3.5f, "It should climb a mesh step lower than its step height and walk on " + Describe(low));
		const glm::vec3 high = WalkAtAStep(0.45f);
		Require(std::abs(high.y) < 0.03f && high.x < 2.0f - 0.25f, "A mesh step higher than its step height should stop it " + Describe(high));
	}

	// The body is made once, kept while nothing changes, and made again once for each new revision,
	// new data or new scale; what rays hit follows
	void TestChangesMakeTheBodyAgainOnce()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		auto mesh = std::make_shared<MeshData>();
		AddFloorQuad(*mesh, 0.0f, glm::vec2(-2.0f), glm::vec2(2.0f));
		const Entity ground = AddMeshCollider(registry, mesh);
		const Ray down{ glm::vec3(0.0f, 5.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f) };

		steps.Run(1);
		const BodyHandle first = world.GetBodies().FindBody(ground);
		Require(first.IsValid() && world.GetPhysics().GetBodyCount() == 1, "The mesh collider should make one body");
		steps.Run(10);
		Require(world.GetBodies().FindBody(ground) == first, "An unchanged mesh's body should be kept");

		// Raised 1 m where it is: the old body until the revision says so, then a new one, once
		for (Vertex& vertex : mesh->Vertices)
		{
			vertex.Position.y += 1.0f;
		}
		steps.Run(3);
		Require(world.GetBodies().FindBody(ground) == first, "A mesh changed without a new revision shouldn't make its body again");
		MarkRuntimeMeshChanged(registry.Get<RuntimeMeshComponent>(ground));
		steps.Run(1);
		const BodyHandle second = world.GetBodies().FindBody(ground);
		Require(second.IsValid() && second != first && world.GetPhysics().GetBodyCount() == 1, "A new revision should make the body again, once");
		steps.Run(10);
		Require(world.GetBodies().FindBody(ground) == second, "The body made again should be kept while nothing changes");
		const std::optional<PhysicsPick> raised = PickPhysicsBody(world, down, 10.0f);
		Require(raised && std::abs(raised->Point.y - 1.0f) < 1e-3f, "A ray should hit the mesh where it is now");

		// New data, then the entity's scale
		auto lower = std::make_shared<MeshData>();
		AddFloorQuad(*lower, -1.0f, glm::vec2(-2.0f), glm::vec2(2.0f));
		SetRuntimeMesh(registry.Get<RuntimeMeshComponent>(ground), lower);
		steps.Run(2);
		const BodyHandle third = world.GetBodies().FindBody(ground);
		Require(third != second, "New data should make the body again");
		registry.Get<TransformComponent>(ground).Scale = glm::vec3(1.0f, 2.0f, 1.0f);
		steps.Run(2);
		Require(world.GetBodies().FindBody(ground) != third, "A new scale should make the body again");
		const std::optional<PhysicsPick> scaled = PickPhysicsBody(world, down, 10.0f);
		Require(scaled && std::abs(scaled->Point.y + 2.0f) < 1e-3f, "The scale should be applied to the mesh");

		// Data that isn't a valid mesh makes no body, and isn't tried again every step
		auto broken = std::make_shared<MeshData>(*lower);
		broken->Indices[2] = 40;
		SetRuntimeMesh(registry.Get<RuntimeMeshComponent>(ground), broken);
		steps.Run(3);
		Require(!world.GetBodies().FindBody(ground).IsValid() && world.GetPhysics().GetBodyCount() == 0, "A mesh that isn't valid shouldn't make a body");

		// Without its mesh, or its collider, the body goes
		SetRuntimeMesh(registry.Get<RuntimeMeshComponent>(ground), lower);
		steps.Run(1);
		Require(world.GetPhysics().GetBodyCount() == 1, "A valid mesh again should make the body again");
		registry.Remove<RuntimeMeshComponent>(ground);
		steps.Run(1);
		Require(world.GetPhysics().GetBodyCount() == 0, "Without its mesh, the collider should have no body");
		registry.Add<RuntimeMeshComponent>(ground, RuntimeMeshComponent{ .Mesh = lower, .Revision = 1 });
		steps.Run(1);
		registry.Remove<MeshColliderComponent>(ground);
		steps.Run(1);
		Require(world.GetPhysics().GetBodyCount() == 0, "Without its collider, the body should go");
	}

	// Mesh shapes are static only: a rigid body beside one is left still, and is not a dynamic body
	void TestItIsStaticOnly()
	{
		GameWorld world;
		Steps steps{ world };
		Registry& registry = world.GetRegistry();
		auto mesh = std::make_shared<MeshData>();
		AddBox(*mesh, glm::vec3(-0.5f), glm::vec3(0.5f));
		const Entity block = AddMeshCollider(registry, mesh, TransformComponent{ .Position = glm::vec3(0.0f, 3.0f, 0.0f) });
		registry.Add<RigidBodyComponent>(block, RigidBodyComponent{});
		steps.Run(30);
		Require(world.GetBodies().FindBody(block).IsValid(), "A mesh collider with a rigid body should still make a body");
		Require(PositionOf(world, block) == glm::vec3(0.0f, 3.0f, 0.0f), "A mesh collider with a rigid body should stay still");

		PhysicsWorld physics;
		BodyDesc desc;
		desc.Shape = BodyShape{ .Kind = EShapeKind::Mesh, .Mesh = std::make_shared<TriangleMesh>(TriangleMesh{ .Vertices = { glm::vec3(0.0f), glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f) }, .Indices = { 0, 1, 2 } }) };
		desc.Motion = EBodyMotion::Dynamic;
		Require(!physics.CreateBody(desc).IsValid(), "PhysicsWorld shouldn't make a dynamic mesh body");
		desc.Motion = EBodyMotion::Static;
		Require(physics.CreateBody(desc).IsValid(), "PhysicsWorld should make a static mesh body");
	}

	// Each triangle's three edges, in the static colour; a mesh of more triangles than the view draws,
	// its bounds' 12 edges
	void TestTheDebugViewDrawsItsEdges()
	{
		if constexpr (!DebugDraw::Enabled)
		{
			return;
		}
		PhysicsWorld physics;
		auto quad = std::make_shared<TriangleMesh>();
		quad->Vertices = { glm::vec3(-1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, 1.0f), glm::vec3(1.0f, 0.0f, -1.0f), glm::vec3(-1.0f, 0.0f, -1.0f) };
		quad->Indices = { 0, 1, 2, 2, 3, 0 };
		BodyDesc desc;
		desc.Shape = BodyShape{ .Kind = EShapeKind::Mesh, .Mesh = quad };
		desc.Motion = EBodyMotion::Static;
		desc.Position = glm::vec3(0.0f, 2.0f, 0.0f);
		physics.CreateBody(desc);

		const PhysicsDebugOptions options;
		DebugDraw drawing;
		physics.DrawDebug(drawing, options);
		Require(drawing.GetLines().size() == 6, "Two triangles should be drawn as 6 lines, not " + std::to_string(drawing.GetLines().size()));
		for (const DebugLine& line : drawing.GetLines())
		{
			Require(line.Color == options.StaticColor, "A mesh body should be drawn in the static colour");
			Require(std::abs(line.From.y - 2.0f) < 1e-4f && std::abs(line.To.y - 2.0f) < 1e-4f, "The edges should be where the body is");
			Require(glm::length(line.From) < 2.5f && glm::length(line.To) < 2.5f, "The edges should be the quad's");
		}

		PhysicsDebugOptions fewer;
		fewer.MaxMeshTriangles = 1;
		drawing.Clear();
		physics.DrawDebug(drawing, fewer);
		Require(drawing.GetLines().size() == 12, "A mesh of more triangles than drawn should be drawn as its bounds");
	}

	void TestTheSampleGround()
	{
		const SampleGroundComponent values{ .Size = 10.0f, .Cells = 20, .BumpHeight = 0.5f, .BumpSpacing = 4.0f };
		const MeshData mesh = MakeSampleGroundMesh(values);
		Require(CheckMeshData(mesh).empty(), "The sample ground should be a valid mesh: " + CheckMeshData(mesh));
		Require(mesh.Vertices.size() == 21 * 21 && mesh.Indices.size() == 20 * 20 * 6, "The sample ground should have its cells");
		const MeshBounds bounds = ComputeMeshBounds(mesh);
		Require(std::abs(bounds.Min.x + 5.0f) < 1e-4f && std::abs(bounds.Max.z - 5.0f) < 1e-4f, "The sample ground should be as wide as its size");
		Require(bounds.Min.y >= -0.5f - 1e-4f && bounds.Max.y <= 0.5f + 1e-4f && bounds.Max.y - bounds.Min.y > 0.5f, "The sample ground's bumps should be as high as asked");
		for (const Vertex& vertex : mesh.Vertices)
		{
			Require(std::abs(vertex.Position.y - GetSampleGroundHeight(values, vertex.Position.x, vertex.Position.z)) < 1e-5f, "A vertex should be at the ground's height");
			Require(vertex.Normal.y > 0.7f && std::abs(glm::length(vertex.Normal) - 1.0f) < 1e-4f, "The ground's normals should face up");
		}
		// Counter-clockwise from above
		const glm::vec3 a = mesh.Vertices[mesh.Indices[0]].Position;
		const glm::vec3 b = mesh.Vertices[mesh.Indices[1]].Position;
		const glm::vec3 c = mesh.Vertices[mesh.Indices[2]].Position;
		Require(glm::cross(b - a, c - a).y > 0.0f, "The ground's triangles should face up");

		// Made once by the system, again when its values change
		GameWorld world;
		Steps steps{ world };
		world.AddSystem(std::make_unique<SampleGroundSystem>());
		Registry& registry = world.GetRegistry();
		const Entity ground = registry.CreateEntity();
		registry.Add<SampleGroundComponent>(ground, values);
		steps.Run(1);
		Require(registry.Has<RuntimeMeshComponent>(ground), "The sample ground's system should give it its mesh");
		const RuntimeMeshComponent made = registry.Get<RuntimeMeshComponent>(ground);
		steps.Run(5);
		Require(registry.Get<RuntimeMeshComponent>(ground).Mesh == made.Mesh && registry.Get<RuntimeMeshComponent>(ground).Revision == made.Revision,
			"The sample ground should be made only once");
		registry.Get<SampleGroundComponent>(ground).BumpHeight = 1.0f;
		steps.Run(1);
		Require(registry.Get<RuntimeMeshComponent>(ground).Revision > made.Revision && ComputeMeshBounds(*registry.Get<RuntimeMeshComponent>(ground).Mesh).Max.y > 0.6f,
			"The sample ground should be made again when its values change");
	}

	const std::filesystem::path SampleScenePath = "Scenes/MeshTest.nyxscene";

	Entity AddNamed(Registry& world, const std::string& name, const TransformComponent& transform)
	{
		const Entity entity = world.CreateEntity();
		world.Add<NameComponent>(entity, NameComponent{ name });
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, transform);
		return entity;
	}

	MeshRendererComponent DrawnWith(const std::string& mesh, const std::string& material)
	{
		return MeshRendererComponent{ .Mesh = mesh.empty() ? AssetReference{} : AssetReference{ .Type = "Mesh", .Path = mesh },
			.Material = AssetReference{ .Type = "Material", .Path = material },
			.bVisible = true };
	}

	// The scene to try mesh colliders in by hand: the sample ground, bumpy, with crates on it, the
	// character and an orbit camera behind it. Its materials are the character test scene's.
	void WriteSampleScene()
	{
		const glm::quat upright(1.0f, 0.0f, 0.0f, 0.0f);
		Registry world;
		const Entity ground = AddNamed(world, "Sample Ground", TransformComponent{});
		world.Add<SampleGroundComponent>(ground, SampleGroundComponent{});
		world.Add<MeshRendererComponent>(ground, DrawnWith("", "Materials/Textured.nyxmat"));
		world.Add<MeshColliderComponent>(ground, MeshColliderComponent{});

		const glm::vec3 crates[] = { { -2.0f, 1.5f, -3.0f }, { -1.2f, 1.5f, -3.4f }, { 2.5f, 1.5f, -2.0f }, { 3.0f, 2.5f, -2.2f }, { 0.5f, 1.5f, -6.0f } };
		for (size_t i = 0; i < std::size(crates); ++i)
		{
			const Entity crate = AddNamed(world, "Crate " + std::to_string(i + 1), TransformComponent{ .Position = crates[i], .Rotation = upright, .Scale = glm::vec3(0.6f) });
			world.Add<MeshRendererComponent>(crate, DrawnWith("Meshes/Cube.nyxmesh", "Materials/CharacterTest/Crate.nyxmat"));
			world.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f) });
			world.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 10.0f });
		}

		const Entity character = AddNamed(world, "Character", TransformComponent{ .Position = glm::vec3(0.0f, 1.0f, 3.0f) });
		world.Add<MeshRendererComponent>(character, DrawnWith("Meshes/Capsule.nyxmesh", "Materials/CharacterTest/Character.nyxmat"));
		world.Add<CharacterComponent>(character, CharacterComponent{});

		const Entity camera = AddNamed(world, "Camera",
			TransformComponent{ .Position = glm::vec3(0.0f, 3.0f, 9.0f), .Rotation = glm::angleAxis(glm::radians(-12.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{});
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });

		const Entity sun = AddNamed(world, "Sun", TransformComponent{ .Position = glm::vec3(0.0f, 5.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / SampleScenePath;
		Require(SceneSerializer::SaveToFile(world, path), "The sample scene can't be saved");
		std::cout << "Wrote " << path.string() << "\n";
	}

	void TestTheSampleSceneStands()
	{
		GameWorld world;
		Steps steps{ world };
		world.AddSystem(std::make_unique<SampleGroundSystem>());
		Registry& registry = world.GetRegistry();
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / SampleScenePath, registry),
			"The sample scene can't be loaded; NyxMeshColliderTests --write-scene writes it");
		steps.Run(240);

		const SampleGroundComponent* ground = nullptr;
		registry.Each<SampleGroundComponent>([&](Entity, SampleGroundComponent& component)
			{
				ground = &component;
			});
		Require(ground != nullptr, "The sample scene should have the sample ground");

		int characters = 0;
		registry.Each<CharacterComponent>([&](Entity entity, CharacterComponent& character)
			{
				++characters;
				const glm::vec3 feet = registry.Get<TransformComponent>(entity).Position;
				Require(character.bOnGround && std::abs(feet.y - GetSampleGroundHeight(*ground, feet.x, feet.z)) < 0.05f,
					"The sample scene's character should stand on the ground " + Describe(feet));
			});
		Require(characters == 1, "The sample scene should have a character");
		int crates = 0;
		registry.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&)
			{
				++crates;
				const glm::vec3 position = registry.Get<TransformComponent>(entity).Position;
				const float above = position.y - GetSampleGroundHeight(*ground, position.x, position.z);
				Require(above > 0.1f && above < 0.6f, "The sample scene's crates should rest on the ground " + Describe(position));
			});
		Require(crates == 5, "The sample scene should have its crates");
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Logger::Get().Init();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			Nyx::RegisterComponentTypes();
			WriteSampleScene();
			return 0;
		}

		TestABoxSlidesDownAMeshSlope();
		TestARayHitsTheFaceItPointsAt();
		TestTheCharacterWalksOnMeshes();
		TestChangesMakeTheBodyAgainOnce();
		TestItIsStaticOnly();
		TestTheDebugViewDrawsItsEdges();
		TestTheSampleGround();
		Nyx::RegisterComponentTypes();
		TestTheSampleSceneStands();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All mesh collider tests passed\n";
	return 0;
}
