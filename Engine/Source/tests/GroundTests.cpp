// The ground (GroundComponent, GroundMesh.h) without a GPU: its height is the same every time, for the
// same values, to the bit, and known values come out as they did when this was written; it changes
// smoothly; another seed is another ground. Its mesh is valid, as big as asked, at the ground's heights,
// its normals up. Steep ground is stone, flat low ground grass, high ground earth. The post-load step,
// as a scene's load, the editor and a live edit run it, makes the mesh, makes nothing again for the
// same values, makes it again with a new revision for new ones, and a Mesh Collider's body follows;
// removing the component takes the mesh away. A character stands on the ground and a crate slides
// down the valley's side. The sample scene loads, and its character and crates stand on the ground;
// its grass, crossed quads scattered round the flat place, swaying in the wind, is there.
// Making the default ground is timed.
//
// NyxGroundTests --write-scene writes the sample scene, Assets/Scenes/GroundTest.nyxscene: this file
// is its source.
#include "CameraComponent.h"
#include "CharacterComponent.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "DirectionalLightComponent.h"
#include "GameWorld.h"
#include "GroundComponent.h"
#include "GroundMesh.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "OrbitCameraComponent.h"
#include "Paths.h"
#include "PhysicsComponents.h"
#include "PreviousTransformComponent.h"
#include "RuntimeMeshComponent.h"
#include "MeshInstancesComponent.h"
#include "ScatterComponent.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"
#include "WindComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

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

	uint32_t Bits(float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		return bits;
	}

	// The ground's post-load step, as a scene's load, the editor's ComponentPostLoadSubscriber and a
	// live edit run it: through the type's ops, with the world and the entity
	void RunPostLoad(Registry& world, Entity entity)
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<GroundComponent>());
		Require(ops && ops->PostLoad, "The ground should have a post-load step");
		ScenePostLoadContext context{ .World = &world, .Entity = entity };
		ops->PostLoad(&world.Get<GroundComponent>(entity), context);
	}

	void TestHeightsAreTheSameEveryTime()
	{
		const GroundComponent ground;
		// Twice over a grid, to the bit
		for (float z = -70.0f; z <= 70.0f; z += 3.7f)
		{
			for (float x = -70.0f; x <= 70.0f; x += 3.3f)
			{
				Require(Bits(SampleGroundHeight(ground, x, z)) == Bits(SampleGroundHeight(ground, x, z)), "The same point should have the same height");
			}
		}

		// As when this was written: a change to the formula, or a machine that rounds otherwise, shows
		const struct
		{
			float X;
			float Z;
			uint32_t HeightBits;
		} known[] = { { 0.0f, 0.0f, 1029559698u }, { 12.5f, -3.25f, 1026279474u }, { -40.0f, 22.0f, 1091345048u }, { 61.0f, 60.5f, 1096407736u } };
		for (const auto& point : known)
		{
			const float height = SampleGroundHeight(ground, point.X, point.Z);
			Require(Bits(height) == point.HeightBits, "The height at " + std::to_string(point.X) + ", " + std::to_string(point.Z) + " is " +
				std::to_string(height) + " (bits " + std::to_string(Bits(height)) + "), not as it was");
		}

		GroundComponent other;
		other.Seed = 2;
		int differ = 0;
		for (float x = -50.0f; x <= 50.0f; x += 10.0f)
		{
			differ += SampleGroundHeight(ground, x, 37.0f) != SampleGroundHeight(other, x, 37.0f) ? 1 : 0;
		}
		Require(differ > 8, "Another seed should make another ground");
	}

	void TestHeightsChangeSmoothly()
	{
		const GroundComponent ground;
		const float step = 0.01f;
		for (float z = -64.0f; z <= 64.0f; z += 1.9f)
		{
			for (float x = -64.0f; x <= 64.0f; x += 1.7f)
			{
				const float here = SampleGroundHeight(ground, x, z);
				// No slope steeper than about 70 degrees, so 1 cm on rises less than 3 cm
				Require(std::abs(SampleGroundHeight(ground, x + step, z) - here) < 3.0f * step &&
							std::abs(SampleGroundHeight(ground, x, z + step) - here) < 3.0f * step,
					"The ground jumps near " + std::to_string(x) + ", " + std::to_string(z));
			}
		}

		// The valley: low and nearly flat in the middle, high at the sides
		Require(std::abs(SampleGroundHeight(ground, 0.0f, 0.0f)) < 1.0f && std::abs(SampleGroundHeight(ground, 3.0f, -4.0f)) < 1.0f,
			"The middle should be nearly flat and low");
		Require(SampleGroundHeight(ground, 63.0f, 0.0f) > 9.0f && SampleGroundHeight(ground, -63.0f, 20.0f) > 9.0f, "The valley's sides should rise");
	}

	void TestTheMesh()
	{
		const GroundComponent ground;
		const MeshData mesh = MakeGroundMesh(ground);
		Require(CheckMeshData(mesh).empty(), "The ground should be a valid mesh: " + CheckMeshData(mesh));
		Require(mesh.Vertices.size() == 129 * 129 && mesh.Indices.size() == 128 * 128 * 6, "128 m at 1 m should be 128 squares a side");
		const MeshBounds bounds = ComputeMeshBounds(mesh);
		Require(bounds.Min.x == -64.0f && bounds.Max.x == 64.0f && bounds.Min.z == -64.0f && bounds.Max.z == 64.0f, "The ground should be 128 m square around its entity");
		for (const Vertex& vertex : mesh.Vertices)
		{
			Require(vertex.Position.y == SampleGroundHeight(ground, vertex.Position.x, vertex.Position.z), "A vertex should be at the ground's height");
			Require(std::abs(glm::length(vertex.Normal) - 1.0f) < 1e-4f && vertex.Normal.y > 0.3f, "A normal should be of unit length and point up");
			Require(vertex.UV == glm::vec2(vertex.Position.x, vertex.Position.z), "UVs should be in metres");
		}
		// Counter-clockwise from above
		const glm::vec3 a = mesh.Vertices[mesh.Indices[0]].Position;
		const glm::vec3 b = mesh.Vertices[mesh.Indices[1]].Position;
		const glm::vec3 c = mesh.Vertices[mesh.Indices[2]].Position;
		Require(glm::cross(b - a, c - a).y > 0.0f, "The ground's triangles should face up");

		// As near the spacing as fits; at most 512 squares a side
		GroundComponent odd;
		odd.Size = glm::vec2(10.0f, 40.0f);
		odd.Spacing = 3.0f;
		const MeshData oddMesh = MakeGroundMesh(odd);
		Require(oddMesh.Vertices.size() == 4 * 14, "10 by 40 m at 3 m should be 3 by 13 squares");
		GroundComponent fine;
		fine.Spacing = 0.01f;
		Require(MakeGroundMesh(fine).Vertices.size() == 513 * 513, "A ground should have at most 512 squares a side");
	}

	void TestColoursBySlopeAndHeight()
	{
		const GroundComponent defaults;
		Require(GetGroundColor(defaults, 0.0f, 5.0f) == defaults.GrassColor, "Low flat ground should be grass");
		Require(GetGroundColor(defaults, 30.0f, 5.0f) == defaults.EarthColor, "High flat ground should be earth");
		Require(GetGroundColor(defaults, 0.0f, 30.0f) == defaults.EarthColor, "Ground between the slopes should be earth");
		Require(GetGroundColor(defaults, 0.0f, 50.0f) == defaults.StoneColor, "Steep ground should be stone");

		// A rough ground: where its mesh is steep it is stone, where flat and low grass
		GroundComponent rough;
		rough.Amplitude = 12.0f;
		rough.Frequency = 0.08f;
		const MeshData mesh = MakeGroundMesh(rough);
		const float blend = rough.SlopeBlendDegrees * 0.5f;
		int stone = 0;
		int grass = 0;
		for (const Vertex& vertex : mesh.Vertices)
		{
			const float slope = glm::degrees(std::acos(vertex.Normal.y));
			if (slope > rough.StoneSlopeDegrees + blend)
			{
				Require(vertex.Color == rough.StoneColor, "A vertex sloping " + std::to_string(slope) + " degrees should be stone");
				++stone;
			}
			else if (slope < rough.EarthSlopeDegrees - blend && vertex.Position.y < rough.GrassThinStart)
			{
				Require(vertex.Color == rough.GrassColor, "A low vertex sloping " + std::to_string(slope) + " degrees should be grass");
				++grass;
			}
		}
		Require(stone > 100 && grass > 100, "The rough ground should have stone and grass: " + std::to_string(stone) + " and " + std::to_string(grass));
	}

	// As a scene's load, the editor and a live edit run it
	void TestThePostLoadStep()
	{
		GameWorld game;
		Steps steps{ game };
		Registry& world = game.GetRegistry();
		const Entity entity = world.CreateEntity();
		world.Add<TransformComponent>(entity, TransformComponent{});
		world.Add<GroundComponent>(entity, GroundComponent{});
		world.Add<MeshColliderComponent>(entity, MeshColliderComponent{});

		RunPostLoad(world, entity);
		Require(world.Has<RuntimeMeshComponent>(entity) && world.Get<RuntimeMeshComponent>(entity).Mesh, "The post-load step should make the ground's mesh");
		const RuntimeMeshComponent first = world.Get<RuntimeMeshComponent>(entity);
		steps.Run(1);
		const BodyHandle firstBody = game.GetBodies().FindBody(entity);
		Require(firstBody.IsValid(), "The ground's Mesh Collider should make a body");

		// Run again for nothing new, e.g. after another component of the entity changed
		RunPostLoad(world, entity);
		Require(world.Get<RuntimeMeshComponent>(entity).Mesh == first.Mesh && world.Get<RuntimeMeshComponent>(entity).Revision == first.Revision,
			"The same values shouldn't make the mesh again");

		// Edited: made again, a new revision, and the body follows
		world.Get<GroundComponent>(entity).Amplitude = 6.0f;
		RunPostLoad(world, entity);
		const RuntimeMeshComponent& edited = world.Get<RuntimeMeshComponent>(entity);
		Require(edited.Mesh != first.Mesh && edited.Revision > first.Revision, "Edited values should make the mesh again with a new revision");
		Require(edited.Mesh->Vertices[1000].Position.y == SampleGroundHeight(world.Get<GroundComponent>(entity), edited.Mesh->Vertices[1000].Position.x, edited.Mesh->Vertices[1000].Position.z),
			"The mesh made again should have the new heights");
		steps.Run(static_cast<int>(PhysicsBodies::MeshSettleSteps) + 1);
		Require(game.GetBodies().FindBody(entity) != firstBody, "The Mesh Collider's body should be made again with the mesh");

		// A copy of the component has made nothing yet (MadeFrom 0): made for its entity
		GroundComponent copied = world.Get<GroundComponent>(entity);
		copied.MadeFrom = 0;
		const Entity copy = world.CreateEntity();
		world.Add<GroundComponent>(copy, copied);
		RunPostLoad(world, copy);
		Require(world.Has<RuntimeMeshComponent>(copy), "A copied ground should make its own mesh");

		// Removed through its ops, as the editor and undo remove it: the mesh goes with it
		ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<GroundComponent>())->Remove(world, entity);
		Require(!world.Has<RuntimeMeshComponent>(entity), "Removing the ground should take its mesh away");

		// A scene's load runs it too: saved without the mesh, loaded with it
		Nyx::Tests::TestDirectory directory("NyxGroundTests");
		const std::filesystem::path path = directory.Path / "Ground.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with a ground failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with a ground failed");
		int grounds = 0;
		loaded.Each<GroundComponent>([&](Entity loadedEntity, GroundComponent& ground)
			{
				++grounds;
				Require(loaded.Has<RuntimeMeshComponent>(loadedEntity) && ground.MadeFrom == HashGroundValues(ground), "A loaded ground should have its mesh");
			});
		Require(grounds == 1, "The scene should keep its ground");
	}

	// The default ground with a Mesh Collider, made as a loaded scene makes it
	Entity AddGround(GameWorld& game)
	{
		Registry& world = game.GetRegistry();
		const Entity ground = world.CreateEntity();
		world.Add<TransformComponent>(ground, TransformComponent{});
		world.Add<GroundComponent>(ground, GroundComponent{});
		world.Add<MeshColliderComponent>(ground, MeshColliderComponent{});
		RunPostLoad(world, ground);
		return ground;
	}

	void TestACharacterStandsAndACrateSlides()
	{
		GameWorld game;
		Steps steps{ game };
		Registry& world = game.GetRegistry();
		const GroundComponent values;
		AddGround(game);

		const Entity character = world.CreateEntity();
		world.Add<TransformComponent>(character, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, -2.0f) });
		world.Add<CharacterComponent>(character, CharacterComponent{});

		// On the valley's side, rising towards +X
		const float startX = 38.0f;
		const float startY = SampleGroundHeight(values, startX, 0.0f) + 0.6f;
		const Entity crate = world.CreateEntity();
		world.Add<TransformComponent>(crate, TransformComponent{ .Position = glm::vec3(startX, startY, 0.0f) });
		world.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.3f) });
		world.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 10.0f });
		world.Add<PhysicsMaterialComponent>(crate, PhysicsMaterialComponent{ .Friction = 0.05f });

		steps.Run(180);
		const glm::vec3 feet = world.Get<TransformComponent>(character).Position;
		Require(world.Get<CharacterComponent>(character).bOnGround && std::abs(feet.y - SampleGroundHeight(values, feet.x, feet.z)) < 0.05f,
			"The character should stand on the ground " + Describe(feet));
		const glm::vec3 slid = world.Get<TransformComponent>(crate).Position;
		Require(slid.x < startX - 2.0f && slid.y < startY - 1.0f, "The crate should slide down the valley's side " + Describe(slid));
		Require(slid.y - SampleGroundHeight(values, slid.x, slid.z) > 0.1f, "The crate should stay on the ground " + Describe(slid));
	}

	void TimeTheDefaultGround()
	{
		const GroundComponent ground;
		const auto start = std::chrono::steady_clock::now();
		const int runs = 5;
		size_t vertices = 0;
		for (int run = 0; run < runs; ++run)
		{
			vertices += MakeGroundMesh(ground).Vertices.size();
		}
		const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / runs;
		std::cout << "The default ground (128 x 128 m at 1 m, " << vertices / runs << " vertices) takes " << milliseconds << " ms to make\n";

		// And its collider's body, from the same triangles
		const MeshData mesh = MakeGroundMesh(ground);
		auto triangles = std::make_shared<TriangleMesh>();
		for (const Vertex& vertex : mesh.Vertices)
		{
			triangles->Vertices.push_back(vertex.Position);
		}
		triangles->Indices = mesh.Indices;
		PhysicsWorld physics;
		const auto bodyStart = std::chrono::steady_clock::now();
		for (int run = 0; run < runs; ++run)
		{
			physics.DestroyBody(physics.CreateBody(BodyDesc{ .Shape = BodyShape{ .Kind = EShapeKind::Mesh, .Mesh = triangles }, .Motion = EBodyMotion::Static }));
		}
		const double bodyMilliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bodyStart).count() / runs;
		std::cout << "Its Mesh Collider's body (" << mesh.Indices.size() / 3 << " triangles) takes " << bodyMilliseconds << " ms to make\n";
	}

	const std::filesystem::path SampleScenePath = "Scenes/GroundTest.nyxscene";

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

	// The scene to try the ground in by hand: the default ground in its vertex colours, with a Mesh
	// Collider, crates on the flat place and up the valley's side, the character and an orbit camera,
	// and grass round the flat place: the built-in crossed quads scattered over the ground, swaying in
	// the world's wind (Materials/GroundTest/Grass.nyxmat, an instance of VertexColorWindTwoSided, and
	// a WindComponent) behind it. The crates and the character have the character test scene's materials.
	void WriteSampleScene()
	{
		const GroundComponent values;
		Registry world;
		const Entity ground = AddNamed(world, "Ground", TransformComponent{});
		world.Add<GroundComponent>(ground, values);
		world.Add<MeshRendererComponent>(ground, DrawnWith("", "Materials/VertexColor.nyxmat"));
		world.Add<MeshColliderComponent>(ground, MeshColliderComponent{});

		const glm::vec2 crates[] = { { -2.0f, -4.0f }, { -1.2f, -4.4f }, { 3.0f, -3.0f }, { 24.0f, -6.0f }, { 30.0f, -2.0f } };
		for (size_t i = 0; i < std::size(crates); ++i)
		{
			const glm::vec3 position(crates[i].x, SampleGroundHeight(values, crates[i].x, crates[i].y) + 0.6f, crates[i].y);
			const Entity crate = AddNamed(world, "Crate " + std::to_string(i + 1), TransformComponent{ .Position = position, .Scale = glm::vec3(0.6f) });
			world.Add<MeshRendererComponent>(crate, DrawnWith("Meshes/Cube.nyxmesh", "Materials/CharacterTest/Crate.nyxmat"));
			world.Add<BoxColliderComponent>(crate, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f) });
			world.Add<RigidBodyComponent>(crate, RigidBodyComponent{ .Mass = 10.0f });
		}

		const Entity grass = AddNamed(world, "Grass", TransformComponent{});
		world.Add<MeshRendererComponent>(grass, DrawnWith("Meshes/CrossedQuads.nyxmesh", "Materials/GroundTest/Grass.nyxmat"));
		ScatterComponent scatter;
		scatter.Surface = world.Get<GuidComponent>(ground).Guid;
		scatter.Size = glm::vec2(28.0f, 28.0f);
		scatter.Density = 5.0f;
		scatter.Spacing = 0.15f;
		scatter.SlopeRange = glm::vec2(0.0f, 30.0f);
		scatter.ScaleRange = glm::vec2(0.3f, 0.55f);
		scatter.AlignToNormal = 0.3f;
		scatter.SinkDepth = 0.02f;
		scatter.bNoiseMask = true;
		scatter.NoiseFrequency = 0.12f;
		scatter.NoiseThreshold = 0.35f;
		scatter.CellSize = 7.0f;
		world.Add<ScatterComponent>(grass, scatter);

		const Entity character = AddNamed(world, "Character", TransformComponent{ .Position = glm::vec3(0.0f, SampleGroundHeight(values, 0.0f, 3.0f) + 0.5f, 3.0f) });
		world.Add<MeshRendererComponent>(character, DrawnWith("Meshes/Capsule.nyxmesh", "Materials/CharacterTest/Character.nyxmat"));
		world.Add<CharacterComponent>(character, CharacterComponent{});

		const Entity camera = AddNamed(world, "Camera",
			TransformComponent{ .Position = glm::vec3(0.0f, 4.0f, 11.0f), .Rotation = glm::angleAxis(glm::radians(-14.0f), glm::vec3(1.0f, 0.0f, 0.0f)) });
		world.Add<CameraComponent>(camera, CameraComponent{});
		world.Add<OrbitCameraComponent>(camera, OrbitCameraComponent{ .Target = world.Get<GuidComponent>(character).Guid });

		const Entity sun = AddNamed(world, "Sun", TransformComponent{ .Position = glm::vec3(0.0f, 5.0f, 0.0f), .Rotation = glm::quat(glm::radians(glm::vec3(-50.0f, 30.0f, 0.0f))) });
		world.Add<DirectionalLightComponent>(sun, DirectionalLightComponent{});

		// The wind the grass sways in, as the world's wind blows by default
		const Entity wind = AddNamed(world, "Wind", TransformComponent{});
		world.Add<WindComponent>(wind, WindComponent{});

		const std::filesystem::path path = Nyx::Paths::GetAssetsDir() / SampleScenePath;
		Require(SceneSerializer::SaveToFile(world, path), "The sample scene can't be saved");
		std::cout << "Wrote " << path.string() << "\n";
	}

	void TestTheSampleSceneStands()
	{
		GameWorld game;
		Steps steps{ game };
		Registry& world = game.GetRegistry();
		Require(SceneSerializer::LoadFromFile(Nyx::Paths::GetAssetsDir() / SampleScenePath, world),
			"The sample scene can't be loaded; NyxGroundTests --write-scene writes it");
		steps.Run(240);

		const GroundComponent* ground = nullptr;
		world.Each<GroundComponent>([&](Entity entity, GroundComponent& component)
			{
				ground = &component;
				Require(world.Has<RuntimeMeshComponent>(entity), "The sample scene's ground should have its mesh once loaded");
			});
		Require(ground != nullptr, "The sample scene should have a ground");

		int characters = 0;
		world.Each<CharacterComponent>([&](Entity entity, CharacterComponent& character)
			{
				++characters;
				const glm::vec3 feet = world.Get<TransformComponent>(entity).Position;
				Require(character.bOnGround && std::abs(feet.y - SampleGroundHeight(*ground, feet.x, feet.z)) < 0.05f,
					"The sample scene's character should stand on the ground " + Describe(feet));
			});
		Require(characters == 1, "The sample scene should have a character");
		int crates = 0;
		world.Each<RigidBodyComponent>([&](Entity entity, RigidBodyComponent&)
			{
				++crates;
				const glm::vec3 position = world.Get<TransformComponent>(entity).Position;
				const float above = position.y - SampleGroundHeight(*ground, position.x, position.z);
				Require(above > 0.1f && above < 0.7f, "The sample scene's crates should rest on the ground " + Describe(position));
			});
		Require(crates == 5, "The sample scene should have its crates");

		// Its grass: the built-in crossed quads over the ground, each copy with its own phase
		int scatters = 0;
		world.Each<ScatterComponent>([&](Entity entity, ScatterComponent&)
			{
				++scatters;
				Require(world.Get<MeshRendererComponent>(entity).Mesh.Path == "Meshes/CrossedQuads.nyxmesh" &&
							world.Get<MeshRendererComponent>(entity).Material.Path == "Materials/GroundTest/Grass.nyxmat",
					"The sample scene's grass should be the crossed quads in its swaying material");
				Require(world.Has<MeshInstancesComponent>(entity), "The sample scene's grass should be scattered once loaded");
				const MeshInstances& instances = *world.Get<MeshInstancesComponent>(entity).Instances;
				Require(instances.Transforms.size() > 1000 && instances.Custom.size() == instances.Transforms.size(),
					"The sample scene's grass should have many copies, each with its own number");
			});
		Require(scatters == 1, "The sample scene should have its grass");
		Require(FindWorldWind(world) != nullptr && game.GetWind().Speed > 0.0f, "The sample scene's grass should have a wind to sway in");
	}
}

int main(int argc, char** argv)
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		if (argc > 1 && std::strcmp(argv[1], "--write-scene") == 0)
		{
			WriteSampleScene();
			return 0;
		}

		TestHeightsAreTheSameEveryTime();
		TestHeightsChangeSmoothly();
		TestTheMesh();
		TestColoursBySlopeAndHeight();
		TestThePostLoadStep();
		TestACharacterStandsAndACrateSlides();
		TestTheSampleSceneStands();
		TimeTheDefaultGround();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All ground tests passed\n";
	return 0;
}
