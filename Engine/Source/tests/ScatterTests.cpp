// Scattering (ScatterComponent, ScatterInstances.h) without a GPU: the same values give the same
// transforms, to the bit, and as they were when this was written; another seed gives others. The
// density gives about as many instances as the surface's area asks for, the spacing keeps them apart,
// the slope and height limits keep them where asked, and each lies on its surface, on a ground and on
// a ground moved and turned, scaled, turned and sunk as asked. The cells cover every instance exactly
// once, with the bounds of their origins. The post-load step, as a scene's load, the editor and a live
// edit run it, makes the instances, makes nothing again for nothing new, and makes them again when the
// scatter, its surface's mesh or where the surface lies changes; a scatter loaded before its surface
// finds it, and removing the component takes the instances away. Scattering about 50,000 instances on
// the default ground is timed.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "GroundComponent.h"
#include "GroundMesh.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "RuntimeMeshComponent.h"
#include "ScatterComponent.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
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

	constexpr EntityGuid GroundGuid{ 0x5CA77E2000000001ull };

	std::string Describe(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	glm::vec3 OriginOf(const glm::mat4& transform)
	{
		return glm::vec3(transform[3]);
	}

	uint64_t HashTransforms(const MeshInstances& instances)
	{
		uint64_t hash = 14695981039346656037ull;
		for (const glm::mat4& transform : instances.Transforms)
		{
			unsigned char bytes[sizeof(glm::mat4)];
			std::memcpy(bytes, &transform, sizeof(bytes));
			for (const unsigned char byte : bytes)
			{
				hash = (hash ^ byte) * 1099511628211ull;
			}
		}
		return hash;
	}

	// A step's ops, as a scene's load, the editor's ComponentPostLoadSubscriber and a live edit run them
	template<typename T>
	const ComponentTypeOps& GetOps()
	{
		const ComponentTypeOps* ops = ComponentTypeRegistry::Get().FindByTypeMetadata(Nyx::Reflection::GetTypeMetadata<T>());
		Require(ops && ops->PostLoad, "The component should have a post-load step");
		return *ops;
	}

	template<typename T>
	void RunPostLoad(Registry& world, Entity entity)
	{
		ScenePostLoadContext context{ .World = &world, .Entity = entity };
		GetOps<T>().PostLoad(&world.Get<T>(entity), context);
	}

	Entity AddGround(Registry& world, const TransformComponent& transform = {})
	{
		const Entity ground = world.CreateEntity();
		world.Add<GuidComponent>(ground, GuidComponent{ GroundGuid });
		world.Add<TransformComponent>(ground, transform);
		world.Add<GroundComponent>(ground, GroundComponent{});
		RunPostLoad<GroundComponent>(world, ground);
		return ground;
	}

	Entity AddScatter(Registry& world, const ScatterComponent& scatter, const TransformComponent& transform = {})
	{
		const Entity entity = world.CreateEntity();
		world.Add<GuidComponent>(entity, GuidComponent{ EntityGuid::Generate() });
		world.Add<TransformComponent>(entity, transform);
		world.Add<ScatterComponent>(entity, scatter);
		RunPostLoad<ScatterComponent>(world, entity);
		return entity;
	}

	ScatterComponent OnGround()
	{
		ScatterComponent scatter;
		scatter.Surface = GroundGuid;
		return scatter;
	}

	// The cells cover every transform exactly once, one after another, each with the bounds of its
	// transforms' origins, and within one square of the cell size
	void RequireCellsCoverAll(const MeshInstances& instances, float cellSize)
	{
		uint32_t next = 0;
		for (const MeshInstanceCell& cell : instances.Cells)
		{
			Require(cell.Count > 0 && cell.First == next, "The cells should cover the transforms one after another, none empty");
			glm::vec3 low = OriginOf(instances.Transforms[cell.First]);
			glm::vec3 high = low;
			for (uint32_t instance = cell.First; instance < cell.First + cell.Count; ++instance)
			{
				low = glm::min(low, OriginOf(instances.Transforms[instance]));
				high = glm::max(high, OriginOf(instances.Transforms[instance]));
			}
			Require(cell.BoundsMin == low && cell.BoundsMax == high, "A cell's bounds should be those of its origins");
			Require(high.x - low.x <= cellSize && high.z - low.z <= cellSize, "A cell should be at most the cell size across");
			next += cell.Count;
		}
		Require(next == instances.Transforms.size(), "The cells should cover every transform");
	}

	void TestTheSameValuesGiveTheSameTransforms()
	{
		const GroundComponent ground;
		const MeshData mesh = MakeGroundMesh(ground);
		ScatterComponent scatter = OnGround();
		scatter.Density = 2.0f;
		scatter.Spacing = 0.3f;
		scatter.AlignToNormal = 0.5f;
		scatter.SinkDepth = 0.05f;
		const MeshInstances first = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		const MeshInstances second = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(first.Transforms.size() > 1000, "The scatter should make instances, made " + std::to_string(first.Transforms.size()));
		Require(first.Transforms.size() == second.Transforms.size() && HashTransforms(first) == HashTransforms(second),
			"The same values should give the same transforms");

		// As when this was written, in Debug and Release: a change to the sampling, or a machine
		// that rounds otherwise, shows
		const uint64_t hash = HashTransforms(first);
		Require(first.Transforms.size() == 1600 && hash == 13094620248440165283ull,
			"The scatter made " + std::to_string(first.Transforms.size()) + " transforms of hash " + std::to_string(hash) + ", not as it did");

		scatter.Seed = 2;
		const MeshInstances other = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(HashTransforms(other) != hash, "Another seed should give other transforms");
	}

	// The area of the triangles' parts in the scatter's area, cut along X and Z: the ground's squares
	// are a metre, so with an area of whole metres every triangle is in or out
	double GetAreaWithin(const MeshData& mesh, const glm::vec2& halfSize)
	{
		double area = 0.0;
		for (size_t index = 0; index + 2 < mesh.Indices.size(); index += 3)
		{
			const glm::vec3 a = mesh.Vertices[mesh.Indices[index]].Position;
			const glm::vec3 b = mesh.Vertices[mesh.Indices[index + 1]].Position;
			const glm::vec3 c = mesh.Vertices[mesh.Indices[index + 2]].Position;
			const glm::vec3 middle = (a + b + c) / 3.0f;
			if (std::abs(middle.x) < halfSize.x && std::abs(middle.z) < halfSize.y)
			{
				area += 0.5 * glm::length(glm::cross(b - a, c - a));
			}
		}
		return area;
	}

	void TestTheDensity()
	{
		// On the plane, every point thrown lies in the area and is kept
		ScatterComponent plane;
		plane.Density = 2.0f;
		plane.Spacing = 0.0f;
		Require(ScatterOnPlane(plane).Transforms.size() == 2048, "2 a square metre on 32 by 32 m should be 2048");

		// On the ground, as many as its area in the scatter's area asks for: the triangles on the
		// area's edges, which only touch it, take a few
		const MeshData mesh = MakeGroundMesh(GroundComponent{});
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(64.0f, 40.0f);
		scatter.Spacing = 0.0f;
		scatter.SlopeRange = glm::vec2(0.0f, 90.0f);
		for (const float density : { 0.5f, 3.0f })
		{
			scatter.Density = density;
			const double expected = density * GetAreaWithin(mesh, scatter.Size * 0.5f);
			const double count = static_cast<double>(ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.size());
			Require(std::abs(count - expected) < 0.03 * expected, "A density of " + std::to_string(density) + " should give about " + std::to_string(expected) + " instances, not " + std::to_string(count));
		}

		// A mesh that isn't valid loses only the triangles that name no vertex
		MeshData broken;
		for (const glm::vec3& corner : { glm::vec3(-16.0f, 0.0f, -16.0f), glm::vec3(16.0f, 0.0f, -16.0f), glm::vec3(16.0f, 0.0f, 16.0f), glm::vec3(-16.0f, 0.0f, 16.0f) })
		{
			broken.Vertices.push_back(Vertex{ .Position = corner, .Normal = glm::vec3(0.0f, 1.0f, 0.0f) });
		}
		broken.Indices = { 0, 3, 2, 2, 1, 0, 0, 1, 99 };
		Require(ScatterOnMesh(plane, broken, glm::mat4(1.0f)).Transforms.size() == 2048, "A triangle that names no vertex should be passed over");

		scatter.Density = 0.0f;
		Require(ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.empty(), "No density should give no instances");
		scatter.Density = 1e9f;
		Require(ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.size() <= MaxScatterInstances, "A scatter should have at most MaxScatterInstances");
	}

	void TestTheSpacing()
	{
		const MeshData mesh = MakeGroundMesh(GroundComponent{});
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(48.0f, 48.0f);
		scatter.Density = 8.0f;
		scatter.Spacing = 0.6f;
		scatter.SlopeRange = glm::vec2(0.0f, 90.0f);
		const MeshInstances instances = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		// 8 a square metre asked for; at 0.6 m apart, darts fill a plane to about 2 a square metre
		Require(instances.Transforms.size() > 3000, "The spacing should keep many instances, kept " + std::to_string(instances.Transforms.size()));
		// Every pair closer than the spacing along X, found by sorting along X
		std::vector<glm::vec3> origins;
		for (const glm::mat4& transform : instances.Transforms)
		{
			origins.push_back(OriginOf(transform));
		}
		std::sort(origins.begin(), origins.end(), [](const glm::vec3& a, const glm::vec3& b)
			{
				return a.x < b.x;
			});
		for (size_t one = 0; one < origins.size(); ++one)
		{
			for (size_t other = one + 1; other < origins.size() && origins[other].x - origins[one].x < scatter.Spacing; ++other)
			{
				const glm::vec3 between = origins[other] - origins[one];
				Require(glm::dot(between, between) >= scatter.Spacing * scatter.Spacing, "Two instances are closer than the spacing at " + Describe(origins[one]));
			}
		}
	}

	void TestTheSlopeAndHeightLimits()
	{
		GroundComponent rough;
		rough.Amplitude = 12.0f;
		rough.Frequency = 0.08f;
		const MeshData mesh = MakeGroundMesh(rough);
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(120.0f, 120.0f);
		scatter.Density = 1.0f;
		scatter.Spacing = 0.0f;
		scatter.SlopeRange = glm::vec2(0.0f, 90.0f);
		// Along the normal, so each instance's up is the surface's normal there
		scatter.AlignToNormal = 1.0f;
		scatter.ScaleRange = glm::vec2(1.0f, 1.0f);
		const size_t unlimited = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.size();

		scatter.SlopeRange = glm::vec2(10.0f, 25.0f);
		scatter.HeightRange = glm::vec2(-2.0f, 6.0f);
		const MeshInstances limited = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(!limited.Transforms.empty() && limited.Transforms.size() < unlimited / 2,
			"The limits should keep some instances and take many away: " + std::to_string(limited.Transforms.size()) + " of " + std::to_string(unlimited));
		for (const glm::mat4& transform : limited.Transforms)
		{
			const glm::vec3 up = glm::normalize(glm::vec3(transform[1]));
			const float slope = glm::degrees(std::acos(std::clamp(up.y, -1.0f, 1.0f)));
			const glm::vec3 origin = OriginOf(transform);
			Require(slope >= 10.0f - 0.01f && slope <= 25.0f + 0.01f, "An instance stands on a slope of " + std::to_string(slope) + " degrees at " + Describe(origin));
			Require(origin.y >= -2.0f && origin.y <= 6.0f, "An instance is out of the height range at " + Describe(origin));
		}
	}

	void TestInstancesLieOnTheSurface()
	{
		const GroundComponent ground;
		const MeshData mesh = MakeGroundMesh(ground);
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(128.0f, 128.0f);
		scatter.SlopeRange = glm::vec2(0.0f, 90.0f);
		scatter.ScaleRange = glm::vec2(0.5f, 2.0f);
		scatter.YawRange = 90.0f;
		const MeshInstances instances = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(instances.Transforms.size() > 5000, "The scatter should cover the ground");
		float lowestScale = 10.0f;
		float highestScale = 0.0f;
		for (const glm::mat4& transform : instances.Transforms)
		{
			// The mesh runs straight between heights a metre apart: within a few centimetres of them
			const glm::vec3 origin = OriginOf(transform);
			Require(std::abs(origin.y - SampleGroundHeight(ground, origin.x, origin.z)) < 0.05f, "An instance should lie on the ground at " + Describe(origin));
			Require(std::abs(origin.x) <= 64.0f && std::abs(origin.z) <= 64.0f, "An instance should lie in the area at " + Describe(origin));

			// Upright, scaled within the range, turned at most 45 degrees either way about Y
			const float scale = glm::length(glm::vec3(transform[1]));
			lowestScale = std::min(lowestScale, scale);
			highestScale = std::max(highestScale, scale);
			Require(std::abs(transform[1].x) < 1e-5f && std::abs(transform[1].z) < 1e-5f && scale >= 0.5f - 1e-5f && scale <= 2.0f + 1e-5f,
				"An instance should stand upright, scaled within the range");
			Require(std::abs(glm::length(glm::vec3(transform[0])) - scale) < 1e-4f && std::abs(glm::length(glm::vec3(transform[2])) - scale) < 1e-4f,
				"An instance should be scaled the same along every axis");
			Require(transform[0].x / scale >= std::sqrt(0.5f) - 1e-4f, "An instance should be turned at most 45 degrees");
			const glm::vec3 handed = glm::cross(glm::vec3(transform[0]), glm::vec3(transform[1]));
			Require(glm::dot(handed, glm::vec3(transform[2])) > 0.0f, "An instance's axes should be right-handed");
		}
		Require(lowestScale < 0.6f && highestScale > 1.9f, "The scales should spread over the range");

		// Sunk along their up
		scatter.SinkDepth = 0.25f;
		const MeshInstances sunk = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(sunk.Transforms.size() == instances.Transforms.size(), "Sinking shouldn't change which instances are kept");
		for (const glm::mat4& transform : sunk.Transforms)
		{
			const glm::vec3 origin = OriginOf(transform);
			Require(std::abs(origin.y + 0.25f - SampleGroundHeight(ground, origin.x, origin.z)) < 0.05f, "An instance should be sunk 25 cm at " + Describe(origin));
		}

		// The ground and the scatter each moved and turned, through the post-load step: instances lie
		// on the ground where it is, in the scatter's space
		Registry world;
		const TransformComponent groundTransform{ .Position = glm::vec3(5.0f, -2.0f, 3.0f), .Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)) };
		const TransformComponent scatterTransform{ .Position = glm::vec3(-4.0f, 1.0f, 7.0f), .Rotation = glm::angleAxis(glm::radians(-50.0f), glm::vec3(0.0f, 1.0f, 0.0f)) };
		AddGround(world, groundTransform);
		ScatterComponent moved = OnGround();
		moved.Size = glm::vec2(60.0f, 60.0f);
		const Entity entity = AddScatter(world, moved, scatterTransform);
		const MeshInstances& placed = *world.Get<MeshInstancesComponent>(entity).Instances;
		Require(placed.Transforms.size() > 500, "The moved scatter should make instances on the moved ground");
		const glm::mat4 groundFromWorld = glm::inverse(groundTransform.ToMatrix());
		for (const glm::mat4& transform : placed.Transforms)
		{
			const glm::vec3 inWorld = glm::vec3(scatterTransform.ToMatrix() * transform[3]);
			const glm::vec3 onGround = glm::vec3(groundFromWorld * glm::vec4(inWorld, 1.0f));
			Require(std::abs(onGround.y - SampleGroundHeight(ground, onGround.x, onGround.z)) < 0.05f, "An instance should lie on the moved ground at " + Describe(inWorld));
			Require(std::abs(transform[1].x) < 1e-5f && std::abs(transform[1].z) < 1e-5f, "An instance should stand upright in the scatter's space");
		}
	}

	void TestTheCells()
	{
		const MeshData mesh = MakeGroundMesh(GroundComponent{});
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(100.0f, 70.0f);
		scatter.Density = 1.5f;
		scatter.SinkDepth = 0.1f;
		scatter.AlignToNormal = 1.0f;
		for (const float cellSize : { 16.0f, 7.0f, 1000.0f })
		{
			scatter.CellSize = cellSize;
			const MeshInstances instances = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
			Require(!instances.Transforms.empty(), "The scatter should make instances");
			RequireCellsCoverAll(instances, cellSize);
			// 100 by 70 m in squares of 16 m: 7 by 5, at most
			if (cellSize == 16.0f)
			{
				Require(instances.Cells.size() > 20 && instances.Cells.size() <= 35, "100 by 70 m should be in up to 35 cells of 16 m, not " + std::to_string(instances.Cells.size()));
			}
			if (cellSize == 1000.0f)
			{
				Require(instances.Cells.size() == 1, "One cell bigger than the area should hold them all");
			}
		}

		// The same instances whatever the cells, only in another order
		scatter.CellSize = 16.0f;
		std::vector<glm::mat4> byCells = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms;
		scatter.CellSize = 1000.0f;
		std::vector<glm::mat4> inOne = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms;
		const auto before = [](const glm::mat4& a, const glm::mat4& b)
		{
			return a[3].x != b[3].x ? a[3].x < b[3].x : a[3].z < b[3].z;
		};
		std::sort(byCells.begin(), byCells.end(), before);
		std::sort(inOne.begin(), inOne.end(), before);
		Require(byCells == inOne, "The cell size should only change the order of the instances");

		// An empty scatter has no cells
		scatter.HeightRange = glm::vec2(500.0f, 600.0f);
		const MeshInstances none = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(none.Transforms.empty() && none.Cells.empty(), "No instances should be in no cells");
	}

	void TestThePostLoadStep()
	{
		Registry world;
		const Entity ground = AddGround(world);

		// A scatter added as the editor adds one, with its defaults: on the plane through it
		const Entity plain = AddScatter(world, ScatterComponent{});
		Require(world.Has<MeshInstancesComponent>(plain) && world.Get<MeshInstancesComponent>(plain).Instances &&
				world.Get<MeshInstancesComponent>(plain).Instances->Transforms.size() > 100,
			"A scatter with its defaults should show instances straight away");
		for (const glm::mat4& transform : world.Get<MeshInstancesComponent>(plain).Instances->Transforms)
		{
			Require(transform[3].y == 0.0f, "A scatter without a surface should be on the plane through it");
		}

		const Entity entity = AddScatter(world, OnGround());
		const MeshInstancesComponent first = world.Get<MeshInstancesComponent>(entity);
		Require(first.Instances && !first.Instances->Transforms.empty() && first.Revision > 0, "The post-load step should make the instances");
		const auto requireUnchanged = [&](const std::string& why)
		{
			const MeshInstancesComponent& now = world.Get<MeshInstancesComponent>(entity);
			Require(now.Instances == first.Instances && now.Revision == first.Revision, why);
		};

		// Run again for nothing new: the step, after another component of the entity changed, and
		// every frame
		RunPostLoad<ScatterComponent>(world, entity);
		requireUnchanged("The same values shouldn't make the instances again");
		for (int frame = 0; frame < 3; ++frame)
		{
			UpdateScatters(world);
		}
		requireUnchanged("A frame with nothing changed shouldn't make the instances again");
		// The ground's own step for nothing new, e.g. its transform edited without moving it
		RunPostLoad<GroundComponent>(world, ground);
		UpdateScatters(world);
		requireUnchanged("The ground's step making nothing new shouldn't make the instances again");

		// The scatter edited
		world.Get<ScatterComponent>(entity).Density = 0.25f;
		RunPostLoad<ScatterComponent>(world, entity);
		uint64_t revision = world.Get<MeshInstancesComponent>(entity).Revision;
		Require(revision > first.Revision && world.Get<MeshInstancesComponent>(entity).Instances->Transforms.size() < first.Instances->Transforms.size(),
			"An edited scatter should make its instances again");

		// The ground edited: its step makes a new mesh, and the next frame scatters on it
		world.Get<GroundComponent>(ground).Amplitude = 8.0f;
		RunPostLoad<GroundComponent>(world, ground);
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(entity).Revision > revision, "An edited ground should make the scatter's instances again");
		revision = world.Get<MeshInstancesComponent>(entity).Revision;
		for (const glm::mat4& transform : world.Get<MeshInstancesComponent>(entity).Instances->Transforms)
		{
			Require(std::abs(transform[3].y - SampleGroundHeight(world.Get<GroundComponent>(ground), transform[3].x, transform[3].z)) < 0.1f,
				"The instances should lie on the edited ground");
		}

		// The ground moved
		world.Get<TransformComponent>(ground).Position.y = 3.0f;
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(entity).Revision > revision, "A moved ground should make the scatter's instances again");
		revision = world.Get<MeshInstancesComponent>(entity).Revision;

		// The ground's mesh gone, with its component: no instances, and none drawn
		GetOps<GroundComponent>().Remove(world, ground);
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(entity).Revision > revision && world.Get<MeshInstancesComponent>(entity).Instances->Transforms.empty(),
			"A surface without a mesh should leave no instances");

		// A copy of the component has made nothing yet (MadeFrom 0): made for its entity
		ScatterComponent copied = world.Get<ScatterComponent>(plain);
		copied.MadeFrom = 0;
		const Entity copy = world.CreateEntity();
		world.Add<ScatterComponent>(copy, copied);
		RunPostLoad<ScatterComponent>(world, copy);
		Require(world.Has<MeshInstancesComponent>(copy) && !world.Get<MeshInstancesComponent>(copy).Instances->Transforms.empty(),
			"A copied scatter should make its own instances");

		// Removed through its ops, as the editor and undo remove it: the instances go with it
		GetOps<ScatterComponent>().Remove(world, plain);
		Require(!world.Has<MeshInstancesComponent>(plain), "Removing the scatter should take its instances away");
	}

	void TestASurfaceLoadedLaterIsFound()
	{
		// The scatter made, and so saved and loaded, before its surface
		Registry world;
		ScatterComponent values = OnGround();
		values.Seed = 7;
		values.YawRange = 30.0f;
		const Entity scatter = AddScatter(world, values);
		Require(world.Get<MeshInstancesComponent>(scatter).Instances->Transforms.empty(), "A scatter whose surface isn't there yet should have no instances");
		AddGround(world);
		UpdateScatters(world);
		const std::shared_ptr<const MeshInstances> made = world.Get<MeshInstancesComponent>(scatter).Instances;
		Require(!made->Transforms.empty(), "Once its surface has a mesh, the next frame should scatter on it");

		// Saved without the instances, loaded with the scatter's values; its step runs before the
		// ground is loaded, the next frame finds the ground
		Nyx::Tests::TestDirectory directory("NyxScatterTests");
		const std::filesystem::path path = directory.Path / "Scatter.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with a scatter failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with a scatter failed");
		Entity loadedScatter{};
		int scatters = 0;
		loaded.Each<ScatterComponent>([&](Entity entity, ScatterComponent& component)
			{
				loadedScatter = entity;
				++scatters;
				Require(component.Surface == GroundGuid && component.Seed == 7 && component.YawRange == 30.0f, "The scatter's values should be saved");
			});
		Require(scatters == 1, "The scene should have its scatter");
		Require(loaded.Has<MeshInstancesComponent>(loadedScatter) && loaded.Get<MeshInstancesComponent>(loadedScatter).Instances->Transforms.empty(),
			"The scatter, loaded before its ground, should have no instances yet");
		UpdateScatters(loaded);
		const MeshInstances& resolved = *loaded.Get<MeshInstancesComponent>(loadedScatter).Instances;
		Require(HashTransforms(resolved) == HashTransforms(*made) && resolved.Transforms.size() == made->Transforms.size(),
			"The loaded scatter should find its ground and make the same instances");
	}

	void TimeFiftyThousandInstances()
	{
		const MeshData mesh = MakeGroundMesh(GroundComponent{});
		ScatterComponent scatter = OnGround();
		scatter.Size = glm::vec2(128.0f, 128.0f);
		scatter.Density = 4.0f;
		scatter.Spacing = 0.25f;
		scatter.SlopeRange = glm::vec2(0.0f, 35.0f);
		scatter.AlignToNormal = 0.3f;
		const int runs = 5;
		size_t instances = 0;
		size_t cells = 0;
		const auto start = std::chrono::steady_clock::now();
		for (int run = 0; run < runs; ++run)
		{
			const MeshInstances made = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
			instances = made.Transforms.size();
			cells = made.Cells.size();
		}
		const double milliseconds = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() / runs;
		std::cout << "Scattering " << instances << " instances in " << cells << " cells on the default ground (128 x 128 m, " << mesh.Indices.size() / 3
				  << " triangles) takes " << milliseconds << " ms\n";
		Require(instances > 40000 && instances < 60000, "The timed scatter should make about 50,000 instances, made " + std::to_string(instances));
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();

		TestTheSameValuesGiveTheSameTransforms();
		TestTheDensity();
		TestTheSpacing();
		TestTheSlopeAndHeightLimits();
		TestInstancesLieOnTheSurface();
		TestTheCells();
		TestThePostLoadStep();
		TestASurfaceLoadedLaterIsFound();
		TimeFiftyThousandInstances();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scatter tests passed\n";
	return 0;
}
