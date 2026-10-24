// Scattering (ScatterComponent, ScatterInstances.h) without a GPU: the same values give the same
// transforms, to the bit, and as they were when this was written; another seed gives others. The
// density gives about as many instances as the surface's area asks for, the spacing keeps them apart,
// also across the cells' edges, the slope and height limits keep them where asked, and each lies on
// its surface, on a ground and on a ground moved and turned, scaled, turned and sunk as asked. The
// cells cover every instance exactly once, with the bounds of their origins. Blockers (circles,
// boxes, spheres, capsules) keep every instance out of their shape and margin, their falloff thins
// the density as it should, and a scatter that opts out grows through them. A blocker moved, added,
// edited or removed, and a surface changed in one place, make only the cells they reach again, and
// what that makes is exactly what scattering all of it makes. The noise mask is the same every time,
// covers about half at a threshold of 0.5, and inverted gives the complement. The post-load step, as
// a scene's load, the editor and a live edit run it, makes the instances, makes nothing again for
// nothing new, and makes them again when the scatter, its surface's mesh or where the surface lies
// changes; a scatter loaded before its surface finds it, and removing the component takes the
// instances away. Each instance has a number of its own from 0 up to 1 in its Custom.x, the same
// every time and however its cells were made, spread evenly. Blockers and masks are saved and loaded. Scattering about 50,000 instances on the
// default ground, and dragging a blocker over 40,000, are timed.
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "GroundComponent.h"
#include "GroundMesh.h"
#include "GuidComponent.h"
#include "Log.h"
#include "MeshInstancesComponent.h"
#include "PhysicsComponents.h"
#include "RuntimeMeshComponent.h"
#include "ScatterBlockerComponent.h"
#include "ScatterComponent.h"
#include "ScatterInstances.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <set>
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

	uint64_t HashTransform(const glm::mat4& transform)
	{
		MeshInstances one;
		one.Transforms.push_back(transform);
		return HashTransforms(one);
	}

	// Equal to the bit: the transforms, and the cells over them
	void RequireSameInstances(const MeshInstances& made, const MeshInstances& expected, const std::string& why)
	{
		Require(made.Transforms == expected.Transforms,
			why + ": the transforms should be those of a full scatter (" + std::to_string(made.Transforms.size()) + " and " + std::to_string(expected.Transforms.size()) + ")");
		Require(made.Custom == expected.Custom, why + ": each instance's own number should be that of a full scatter");
		Require(made.Cells.size() == expected.Cells.size(), why + ": the cells should be those of a full scatter");
		for (size_t cell = 0; cell < made.Cells.size(); ++cell)
		{
			const MeshInstanceCell& a = made.Cells[cell];
			const MeshInstanceCell& b = expected.Cells[cell];
			Require(a.First == b.First && a.Count == b.Count && a.BoundsMin == b.BoundsMin && a.BoundsMax == b.BoundsMax, why + ": a cell should be that of a full scatter");
		}
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
	// transforms' origins, and within one square of the cell size: the points on the surface are, and
	// the origins are sunk from them by up to `sunk` across
	void RequireCellsCoverAll(const MeshInstances& instances, float cellSize, float sunk)
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
			Require(high.x - low.x <= cellSize + 2.0f * sunk && high.z - low.z <= cellSize + 2.0f * sunk, "A cell should be at most the cell size across");
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
		Require(first.Transforms.size() == 1583 && hash == 3057672828998612062ull,
			"The scatter made " + std::to_string(first.Transforms.size()) + " transforms of hash " + std::to_string(hash) + ", not as it did");

		scatter.Seed = 2;
		const MeshInstances other = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(HashTransforms(other) != hash, "Another seed should give other transforms");
	}

	// Each instance's Custom.x: from 0 up to 1, a hash of where it stands, so the same values give the
	// same numbers, and they spread over the range (each tenth holds about a tenth of them); y, z and
	// w are zero
	void TestEachInstanceHasANumberOfItsOwn()
	{
		const GroundComponent ground;
		const MeshData mesh = MakeGroundMesh(ground);
		ScatterComponent scatter = OnGround();
		scatter.Density = 2.0f;
		scatter.Spacing = 0.3f;
		const MeshInstances first = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		const MeshInstances second = ScatterOnMesh(scatter, mesh, glm::mat4(1.0f));
		Require(first.Custom.size() == first.Transforms.size(), "Every instance should have its custom numbers");
		Require(first.Custom == second.Custom, "The same values should give the same numbers");
		std::array<size_t, 10> tenths{};
		std::set<float> distinct;
		for (const glm::vec4& custom : first.Custom)
		{
			Require(custom.x >= 0.0f && custom.x < 1.0f, "An instance's number should be from 0 up to 1, not " + std::to_string(custom.x));
			Require(custom.y == 0.0f && custom.z == 0.0f && custom.w == 0.0f, "An instance's custom y, z and w should be zero");
			++tenths[std::min<size_t>(static_cast<size_t>(custom.x * 10.0f), 9)];
			distinct.insert(custom.x);
		}
		const double expected = static_cast<double>(first.Custom.size()) / 10.0;
		for (size_t tenth = 0; tenth < tenths.size(); ++tenth)
		{
			Require(std::abs(static_cast<double>(tenths[tenth]) - expected) < expected * 0.25,
				"Tenth " + std::to_string(tenth) + " of the range holds " + std::to_string(tenths[tenth]) + " numbers, not about " + std::to_string(expected));
		}
		Require(distinct.size() > first.Custom.size() * 99 / 100, "The instances' numbers should be nearly all different");
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
			RequireCellsCoverAll(instances, cellSize, scatter.SinkDepth);
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

		// Each cell is sampled on its own, so the cell size changes where the instances are, but not
		// how many there are, give or take the spacing along the cells' edges
		scatter.CellSize = 16.0f;
		const double byCells = static_cast<double>(ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.size());
		scatter.CellSize = 1000.0f;
		const double inOne = static_cast<double>(ScatterOnMesh(scatter, mesh, glm::mat4(1.0f)).Transforms.size());
		Require(std::abs(byCells - inOne) < 0.03 * inOne, "The cell size shouldn't change how many instances there are: " + std::to_string(byCells) + " and " + std::to_string(inOne));

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

		// A copy of the component has made nothing yet (MadeFrom 0): made for its entity, with cells
		// of its own from those it shares, which stay as they were
		ScatterComponent copied = world.Get<ScatterComponent>(plain);
		copied.MadeFrom = 0;
		const std::shared_ptr<ScatterCells> shared = copied.Cells;
		const MeshInstances plainInstances = CollectScatterInstances(*shared);
		const Entity copy = world.CreateEntity();
		world.Add<ScatterComponent>(copy, copied);
		world.Get<ScatterComponent>(copy).Size = glm::vec2(20.0f, 20.0f);
		RunPostLoad<ScatterComponent>(world, copy);
		Require(world.Has<MeshInstancesComponent>(copy) && !world.Get<MeshInstancesComponent>(copy).Instances->Transforms.empty(),
			"A copied scatter should make its own instances");
		Require(world.Get<ScatterComponent>(copy).Cells != shared && world.Get<ScatterComponent>(plain).Cells == shared,
			"A copied scatter should make cells of its own");
		RequireSameInstances(CollectScatterInstances(*shared), plainInstances, "The cells a copy shared should stay as they were");
		RequireSameInstances(*world.Get<MeshInstancesComponent>(copy).Instances, ScatterOnPlane(world.Get<ScatterComponent>(copy)), "A copied scatter");

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

	Entity AddBlocker(Registry& world, const ScatterBlockerComponent& blocker, const TransformComponent& transform, EntityGuid guid = EntityGuid::Generate())
	{
		const Entity entity = world.CreateEntity();
		world.Add<GuidComponent>(entity, GuidComponent{ guid });
		world.Add<TransformComponent>(entity, transform);
		world.Add<ScatterBlockerComponent>(entity, blocker);
		return entity;
	}

	// How far a point of the world is from each kind of shape, worked out here on its own: 0 inside
	float DistanceToBox(const glm::vec3& point, const TransformComponent& transform, const glm::vec3& halfExtents)
	{
		const glm::vec3 local = glm::inverse(transform.Rotation) * (point - transform.Position);
		return glm::length(glm::max(glm::abs(local) - halfExtents, glm::vec3(0.0f)));
	}

	float DistanceToCapsule(const glm::vec3& point, const TransformComponent& transform, float radius, float halfHeight)
	{
		glm::vec3 local = glm::inverse(transform.Rotation) * (point - transform.Position);
		local.y -= std::clamp(local.y, -halfHeight, halfHeight);
		return std::max(glm::length(local) - radius, 0.0f);
	}

	float DistanceAcross(const glm::vec3& point, const glm::vec3& center)
	{
		return glm::length(glm::vec2(point.x - center.x, point.z - center.z));
	}

	void TestBlockersKeepCopiesAway()
	{
		Registry world;
		AddGround(world);
		ScatterComponent values = OnGround();
		values.Size = glm::vec2(48.0f, 48.0f);
		values.Density = 6.0f;
		values.Spacing = 0.0f;
		values.SlopeRange = glm::vec2(0.0f, 90.0f);
		values.CellSize = 8.0f;
		// Moved and turned, so the blockers are found in its space
		const TransformComponent scatterTransform{ .Position = glm::vec3(1.5f, 0.0f, -2.0f), .Rotation = glm::angleAxis(glm::radians(20.0f), glm::vec3(0.0f, 1.0f, 0.0f)) };
		const Entity scatter = AddScatter(world, values, scatterTransform);
		const size_t unblocked = world.Get<MeshInstancesComponent>(scatter).Instances->Transforms.size();

		// A circle, scaled: 1.5 m times 2
		const TransformComponent circleAt{ .Position = glm::vec3(-10.0f, 0.0f, -8.0f), .Scale = glm::vec3(2.0f, 1.0f, 2.0f) };
		AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 1.5f, .Margin = 0.5f }, circleAt);
		// A box collider, turned and tilted, scaled along X as the physics scales it
		const TransformComponent boxAt{ .Position = glm::vec3(8.0f, 1.0f, 6.0f),
			.Rotation = glm::angleAxis(glm::radians(35.0f), glm::vec3(0.0f, 1.0f, 0.0f)) * glm::angleAxis(glm::radians(10.0f), glm::vec3(1.0f, 0.0f, 0.0f)),
			.Scale = glm::vec3(1.5f, 1.0f, 1.0f) };
		const Entity box = AddBlocker(world, ScatterBlockerComponent{ .Margin = 0.3f }, boxAt);
		world.Add<BoxColliderComponent>(box, BoxColliderComponent{ .HalfExtents = glm::vec3(2.0f, 1.5f, 1.0f) });
		const glm::vec3 boxHalfExtents = glm::vec3(2.0f, 1.5f, 1.0f) * boxAt.Scale;
		// A sphere collider
		const TransformComponent sphereAt{ .Position = glm::vec3(-6.0f, 0.5f, 9.0f) };
		const Entity sphere = AddBlocker(world, ScatterBlockerComponent{ .Margin = 0.25f }, sphereAt);
		world.Add<SphereColliderComponent>(sphere, SphereColliderComponent{ .Radius = 2.0f });
		// A capsule collider, leaning over
		const TransformComponent capsuleAt{ .Position = glm::vec3(10.0f, 0.5f, -10.0f), .Rotation = glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 0.0f, 1.0f)) };
		const Entity capsule = AddBlocker(world, ScatterBlockerComponent{ .Margin = 0.4f }, capsuleAt);
		world.Add<CapsuleColliderComponent>(capsule, CapsuleColliderComponent{ .Radius = 0.6f, .HalfHeight = 1.5f });
		// Told to take its collider, but it has none: the circle of its radius
		const TransformComponent plainAt{ .Position = glm::vec3(0.0f, 0.0f, 12.0f) };
		AddBlocker(world, ScatterBlockerComponent{ .Radius = 1.0f, .Margin = 0.2f }, plainAt);

		std::vector<ScatterBlocker::EKind> kinds;
		for (const ScatterBlocker& blocker : GatherScatterBlockers(world, scatterTransform.ToMatrix()))
		{
			kinds.push_back(blocker.Kind);
		}
		std::sort(kinds.begin(), kinds.end());
		Require(kinds == std::vector<ScatterBlocker::EKind>{ ScatterBlocker::EKind::Circle, ScatterBlocker::EKind::Circle, ScatterBlocker::EKind::Box,
							 ScatterBlocker::EKind::Sphere, ScatterBlocker::EKind::Capsule },
			"Each blocker should take its collider's shape, or the circle");

		// Added after the scatter: the next frame keeps the copies away
		UpdateScatters(world);
		const MeshInstances& blocked = *world.Get<MeshInstancesComponent>(scatter).Instances;
		Require(blocked.Transforms.size() < unblocked && blocked.Transforms.size() > unblocked / 2, "The blockers should take some copies away");
		// The blockers' rotations are taken to about a tenth of a degree: a few millimetres over their size
		const float rounding = 0.01f;
		int near[5] = {};
		for (const glm::mat4& transform : blocked.Transforms)
		{
			const glm::vec3 point(scatterTransform.ToMatrix() * transform[3]);
			const float distances[5] = { DistanceAcross(point, circleAt.Position) - 3.0f, DistanceToBox(point, boxAt, boxHalfExtents),
				std::max(glm::length(point - sphereAt.Position) - 2.0f, 0.0f), DistanceToCapsule(point, capsuleAt, 0.6f, 1.5f), DistanceAcross(point, plainAt.Position) - 1.0f };
			const float margins[5] = { 0.5f, 0.3f, 0.25f, 0.4f, 0.2f };
			const char* names[5] = { "circle", "box", "sphere", "capsule", "circle of a collider blocker without a collider" };
			for (int shape = 0; shape < 5; ++shape)
			{
				Require(distances[shape] > margins[shape] - rounding, std::string("A copy lies within the ") + names[shape] + " and its margin at " + Describe(point));
				if (distances[shape] < margins[shape] + 0.5f)
				{
					++near[shape];
				}
			}
		}
		for (int shape = 0; shape < 5; ++shape)
		{
			Require(near[shape] > 0, "Copies should grow right up to each blocker's margin");
		}
	}

	void TestTheFalloffThinsTheDensity()
	{
		Registry world;
		AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 1.0f, .Margin = 1.0f, .Falloff = 6.0f }, TransformComponent{});
		const std::vector<ScatterBlocker> blockers = GatherScatterBlockers(world, glm::mat4(1.0f));
		Require(blockers.size() == 1, "The world should have one blocker");
		Require(GetBlockerDensity(blockers[0], glm::vec3(1.99f, 0.0f, 0.0f)) == 0.0f && GetBlockerDensity(blockers[0], glm::vec3(8.5f, 0.0f, 0.0f)) == 1.0f &&
				std::abs(GetBlockerDensity(blockers[0], glm::vec3(0.0f, 0.0f, 5.0f)) - 0.5f) < 1e-5f,
			"The density should be 0 within the margin, half halfway through the falloff, 1 past it");

		ScatterComponent values;
		values.Size = glm::vec2(40.0f, 40.0f);
		values.Density = 40.0f;
		values.Spacing = 0.0f;
		values.CellSize = 8.0f;
		const MeshInstances instances = ScatterOnPlane(values, blockers);

		// Counted in rings a metre wide, against the density times the ring's area, thinned by the
		// falloff: from 2 m (radius and margin) to 8 m
		std::vector<double> counts(12, 0.0);
		for (const glm::mat4& transform : instances.Transforms)
		{
			const float distance = glm::length(glm::vec2(transform[3].x, transform[3].z));
			if (distance < 12.0f)
			{
				counts[static_cast<size_t>(distance)] += 1.0;
			}
		}
		for (size_t ring = 0; ring < counts.size(); ++ring)
		{
			double expected = 0.0;
			const int steps = 200;
			for (int step = 0; step < steps; ++step)
			{
				const double radius = static_cast<double>(ring) + (step + 0.5) / steps;
				const double t = std::clamp((radius - 2.0) / 6.0, 0.0, 1.0);
				expected += 2.0 * 3.14159265358979 * radius * (t * t * (3.0 - 2.0 * t)) / steps;
			}
			expected *= values.Density;
			Require(std::abs(counts[ring] - expected) <= 0.06 * expected + 4.0 * std::sqrt(expected) + 1.0,
				"From " + std::to_string(ring) + " to " + std::to_string(ring + 1) + " m out, " + std::to_string(counts[ring]) + " copies, not about " + std::to_string(expected));
		}
	}

	void TestEditsMakeOnlyTheirCellsAgain()
	{
		Registry world;
		const Entity ground = AddGround(world);
		ScatterComponent values = OnGround();
		values.Size = glm::vec2(96.0f, 96.0f);
		values.CellSize = 8.0f;
		values.Density = 3.0f;
		values.Spacing = 0.3f;
		values.SlopeRange = glm::vec2(0.0f, 40.0f);
		const TransformComponent scatterTransform{ .Position = glm::vec3(0.5f, 0.0f, -0.25f) };
		const Entity scatter = AddScatter(world, values, scatterTransform);
		const ScatterCells& cells = *world.Get<ScatterComponent>(scatter).Cells;
		const uint32_t cellCount = cells.Columns * cells.Rows;
		Require(cellCount == 144, "96 m in cells of 8 m should be 12 by 12 cells");

		const auto scatterAll = [&]()
		{
			const glm::mat4 toWorld = world.Get<TransformComponent>(scatter).ToMatrix();
			const glm::mat4 surfaceToScatter = glm::inverse(toWorld) * world.Get<TransformComponent>(ground).ToMatrix();
			return ScatterOnMesh(world.Get<ScatterComponent>(scatter), *world.Get<RuntimeMeshComponent>(ground).Mesh, surfaceToScatter, GatherScatterBlockers(world, toWorld));
		};
		const auto instances = [&]() -> const MeshInstances&
		{
			return *world.Get<MeshInstancesComponent>(scatter).Instances;
		};
		const auto thrown = [&]()
		{
			return world.Get<ScatterComponent>(scatter).Cells->CellsThrown;
		};
		RequireSameInstances(instances(), scatterAll(), "The first scatter");

		// Added: only the cells it reaches
		const Entity blocker = AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 1.0f, .Margin = 0.5f, .Falloff = 1.0f },
			TransformComponent{ .Position = glm::vec3(5.0f, 0.0f, 5.0f) });
		UpdateScatters(world);
		Require(thrown() >= 1 && thrown() <= 4, "An added blocker should make only the cells it reaches again, made " + std::to_string(thrown()));
		RequireSameInstances(instances(), scatterAll(), "A blocker added");

		// Moved 3 m: the cells it reached before and after, and those after them within the spacing;
		// every other copy keeps its transform
		const MeshInstances before = instances();
		world.Get<TransformComponent>(blocker).Position.x += 3.0f;
		UpdateScatters(world);
		const ScatterCells& moved = *world.Get<ScatterComponent>(scatter).Cells;
		Require(moved.CellsThrown >= 1 && moved.CellsThrown <= 6 && moved.CellsKeptApart <= 4 * moved.CellsThrown,
			"A moved blocker should make only the cells it reaches again, made " + std::to_string(moved.CellsThrown) + " and kept apart " + std::to_string(moved.CellsKeptApart));
		RequireSameInstances(instances(), scatterAll(), "A blocker moved");
		std::set<uint64_t> after;
		for (const glm::mat4& transform : instances().Transforms)
		{
			after.insert(HashTransform(transform));
		}
		size_t far = 0;
		for (const glm::mat4& transform : before.Transforms)
		{
			const glm::vec3 point(scatterTransform.ToMatrix() * transform[3]);
			if (std::min(DistanceAcross(point, glm::vec3(5.0f, 0.0f, 5.0f)), DistanceAcross(point, glm::vec3(8.0f, 0.0f, 5.0f))) > 12.0f)
			{
				Require(after.count(HashTransform(transform)) == 1, "A copy far from the moved blocker should keep its transform at " + Describe(point));
				++far;
			}
		}
		Require(far > 10000, "Most copies should be far from the blocker");

		// Moved less than a centimetre: nothing made again
		const uint64_t revision = world.Get<MeshInstancesComponent>(scatter).Revision;
		world.Get<TransformComponent>(blocker).Position.x += 0.002f;
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(scatter).Revision == revision, "A blocker moved less than a centimetre shouldn't make anything again");

		// A run of edits, each the same as a full scatter: blockers moved, added, edited, turned and
		// removed, as components and with their entities
		uint32_t state = 12345u;
		const auto next = [&state]()
		{
			state = state * 1664525u + 1013904223u;
			return static_cast<float>(state >> 8) / 16777216.0f;
		};
		std::vector<Entity> blockers{ blocker };
		for (int edit = 0; edit < 30; ++edit)
		{
			const float pick = next();
			const size_t which = std::min(static_cast<size_t>(next() * static_cast<float>(blockers.size())), blockers.size() - 1);
			if (pick < 0.35f)
			{
				TransformComponent& transform = world.Get<TransformComponent>(blockers[which]);
				transform.Position += glm::vec3(next() * 12.0f - 6.0f, 0.0f, next() * 12.0f - 6.0f);
			}
			else if (pick < 0.55f)
			{
				const Entity added = AddBlocker(world, ScatterBlockerComponent{ .Radius = 0.5f + next() * 2.0f, .Margin = next(), .Falloff = next() * 2.0f },
					TransformComponent{ .Position = glm::vec3(next() * 80.0f - 40.0f, 0.0f, next() * 80.0f - 40.0f) });
				if (next() < 0.5f)
				{
					world.Add<BoxColliderComponent>(added, BoxColliderComponent{ .HalfExtents = glm::vec3(0.5f + next(), 1.0f, 0.5f + next()) });
				}
				blockers.push_back(added);
			}
			else if (pick < 0.7f)
			{
				ScatterBlockerComponent& edited = world.Get<ScatterBlockerComponent>(blockers[which]);
				edited.Radius = 0.5f + next() * 2.0f;
				edited.Margin = next();
				edited.Falloff = next() * 2.0f;
			}
			else if (pick < 0.85f && blockers.size() > 1)
			{
				if (next() < 0.5f)
				{
					world.Remove<ScatterBlockerComponent>(blockers[which]);
				}
				else
				{
					world.DestroyEntity(blockers[which]);
				}
				blockers.erase(blockers.begin() + static_cast<std::ptrdiff_t>(which));
			}
			else
			{
				world.Get<TransformComponent>(blockers[which]).Rotation = glm::angleAxis(next() * 6.0f, glm::vec3(0.0f, 1.0f, 0.0f));
			}
			UpdateScatters(world);
			RequireSameInstances(instances(), scatterAll(), "Edit " + std::to_string(edit) + " of the blockers");
			Require(thrown() < cellCount / 4, "An edit of a blocker should make only some cells again, made " + std::to_string(thrown()));
		}

		// The surface changed in one place: only the cells of the triangles that changed
		MeshData raised = *world.Get<RuntimeMeshComponent>(ground).Mesh;
		for (Vertex& vertex : raised.Vertices)
		{
			if (glm::length(glm::vec2(vertex.Position.x + 20.0f, vertex.Position.z + 20.0f)) < 2.5f)
			{
				vertex.Position.y += 0.5f;
			}
		}
		SetRuntimeMesh(world.Get<RuntimeMeshComponent>(ground), std::make_shared<const MeshData>(std::move(raised)));
		UpdateScatters(world);
		Require(thrown() >= 1 && thrown() <= 4, "A surface changed in one place should make only its cells again, made " + std::to_string(thrown()));
		RequireSameInstances(instances(), scatterAll(), "The surface changed in one place");

		// The scatter moved: everything, still as a full scatter
		world.Get<TransformComponent>(scatter).Position.z += 1.0f;
		UpdateScatters(world);
		Require(thrown() == cellCount, "A moved scatter should make every cell again");
		RequireSameInstances(instances(), scatterAll(), "The scatter moved");
	}

	void TestTheNoiseMask()
	{
		ScatterComponent mask;
		mask.bNoiseMask = true;
		mask.NoiseFrequency = 0.5f;
		mask.NoiseSoftness = 0.0f;

		// The same noise every time, and as when this was written, in Debug and Release
		uint64_t hash = 14695981039346656037ull;
		for (int z = 0; z < 16; ++z)
		{
			for (int x = 0; x < 16; ++x)
			{
				const float value = SampleScatterNoise(mask, static_cast<float>(x) * 0.73f - 5.0f, static_cast<float>(z) * 0.61f + 3.0f);
				Require(value == SampleScatterNoise(mask, static_cast<float>(x) * 0.73f - 5.0f, static_cast<float>(z) * 0.61f + 3.0f), "The same point should give the same noise");
				unsigned char bytes[sizeof(value)];
				std::memcpy(bytes, &value, sizeof(value));
				for (const unsigned char byte : bytes)
				{
					hash = (hash ^ byte) * 1099511628211ull;
				}
			}
		}
		Require(hash == 10707370372966230081ull, "The noise has hash " + std::to_string(hash) + ", not as it had");
		ScatterComponent reseeded = mask;
		reseeded.NoiseSeed = 2;
		Require(SampleScatterNoise(mask, 1.3f, 2.7f) != SampleScatterNoise(reseeded, 1.3f, 2.7f), "Another seed should give other noise");

		// About half kept at a threshold of 0.5, over some 55 patches a side; more below, less above
		const auto share = [&](float threshold)
		{
			mask.NoiseThreshold = threshold;
			int kept = 0;
			const int side = 300;
			for (int z = 0; z < side; ++z)
			{
				for (int x = 0; x < side; ++x)
				{
					const float px = static_cast<float>(x) * 0.37f;
					const float pz = static_cast<float>(z) * 0.37f;
					const float value = SampleScatterNoise(mask, px, pz);
					Require(value > -0.05f && value < 1.05f, "The noise should be about 0 to 1, not " + std::to_string(value));
					kept += GetNoiseMaskDensity(mask, px, pz) > 0.5f ? 1 : 0;
				}
			}
			return static_cast<double>(kept) / (side * side);
		};
		const double half = share(0.5f);
		Require(std::abs(half - 0.5) < 0.04, "A threshold of 0.5 should keep about half, kept " + std::to_string(half));
		Require(share(0.3f) > 0.62 && share(0.7f) < 0.38, "A lower threshold should keep more, a higher one less");

		// A scatter: copies only where the noise is above the threshold, about half of them
		ScatterComponent drifts;
		drifts.Size = glm::vec2(64.0f, 64.0f);
		drifts.Density = 20.0f;
		drifts.Spacing = 0.0f;
		drifts.bNoiseMask = true;
		drifts.NoiseFrequency = 0.15f;
		drifts.NoiseSoftness = 0.0f;
		const MeshInstances masked = ScatterOnPlane(drifts);
		for (const glm::mat4& transform : masked.Transforms)
		{
			Require(SampleScatterNoise(drifts, transform[3].x, transform[3].z) >= drifts.NoiseThreshold, "A copy lies where the mask leaves them out");
		}
		drifts.bNoiseMask = false;
		const double kept = static_cast<double>(masked.Transforms.size()) / static_cast<double>(ScatterOnPlane(drifts).Transforms.size());
		Require(kept > 0.35 && kept < 0.65, "The mask should keep about half the copies, kept " + std::to_string(kept));

		// Soft edges: none well below the threshold, all well above, some between
		drifts.bNoiseMask = true;
		drifts.NoiseSoftness = 0.4f;
		bool bBetween = false;
		for (int sample = 0; sample < 4000; ++sample)
		{
			const float x = static_cast<float>(sample % 64) - 32.0f;
			const float z = static_cast<float>(sample / 64) - 32.0f;
			const float noise = SampleScatterNoise(drifts, x, z);
			const float density = GetNoiseMaskDensity(drifts, x, z);
			Require((noise > 0.3f || density == 0.0f) && (noise < 0.7f || density == 1.0f) && density >= 0.0f && density <= 1.0f, "The soft edge should be within the softness");
			bBetween |= density > 0.05f && density < 0.95f;
		}
		Require(bBetween, "A soft edge should thin the density gradually");
	}

	void TestTheInvertedMaskIsTheComplement()
	{
		ScatterComponent values;
		values.Size = glm::vec2(48.0f, 48.0f);
		values.Density = 10.0f;
		values.Spacing = 0.0f;
		values.CellSize = 8.0f;
		values.bNoiseMask = true;
		values.NoiseFrequency = 0.15f;
		values.NoiseThreshold = 0.45f;
		values.NoiseSoftness = 0.0f;
		const MeshInstances drifts = ScatterOnPlane(values);
		values.bNoiseInvert = true;
		const MeshInstances clearings = ScatterOnPlane(values);
		values.bNoiseMask = false;
		const MeshInstances all = ScatterOnPlane(values);
		Require(!drifts.Transforms.empty() && !clearings.Transforms.empty() && drifts.Transforms.size() + clearings.Transforms.size() == all.Transforms.size(),
			"The mask and its inverse should share the copies out between them");
		std::set<uint64_t> both;
		for (const MeshInstances* half : { &drifts, &clearings })
		{
			for (const glm::mat4& transform : half->Transforms)
			{
				Require(both.insert(HashTransform(transform)).second, "A copy should be kept by the mask or its inverse, not both");
			}
		}
		for (const glm::mat4& transform : all.Transforms)
		{
			Require(both.count(HashTransform(transform)) == 1, "Every copy should be kept by the mask or its inverse");
		}

		// Soft: the two densities add up to 1 everywhere
		values.bNoiseMask = true;
		values.NoiseSoftness = 0.3f;
		ScatterComponent inverted = values;
		inverted.bNoiseInvert = false;
		values.bNoiseInvert = true;
		for (int sample = 0; sample < 1000; ++sample)
		{
			const float x = static_cast<float>(sample % 40) * 0.9f;
			const float z = static_cast<float>(sample / 40) * 1.1f;
			Require(std::abs(GetNoiseMaskDensity(values, x, z) + GetNoiseMaskDensity(inverted, x, z) - 1.0f) < 1e-6f, "A soft mask and its inverse should add up to 1");
		}
	}

	void TestAScatterCanIgnoreBlockers()
	{
		Registry world;
		const Entity ground = AddGround(world);
		ScatterComponent values = OnGround();
		values.Size = glm::vec2(32.0f, 32.0f);
		values.Density = 4.0f;
		values.Spacing = 0.0f;
		values.bUseBlockers = false;
		const Entity blocker = AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 4.0f, .Margin = 0.5f }, TransformComponent{});
		const Entity optedOut = AddScatter(world, values);
		values.bUseBlockers = true;
		const Entity keeping = AddScatter(world, values);
		UpdateScatters(world);

		values.bUseBlockers = false;
		const MeshInstances& through = *world.Get<MeshInstancesComponent>(optedOut).Instances;
		RequireSameInstances(through, ScatterOnMesh(values, *world.Get<RuntimeMeshComponent>(ground).Mesh, glm::mat4(1.0f)), "A scatter that opts out of the blockers");
		Require(std::any_of(through.Transforms.begin(), through.Transforms.end(), [](const glm::mat4& transform)
					{
						return DistanceAcross(glm::vec3(transform[3]), glm::vec3(0.0f)) < 4.0f;
					}),
			"A scatter that opts out should grow through a blocker");
		for (const glm::mat4& transform : world.Get<MeshInstancesComponent>(keeping).Instances->Transforms)
		{
			Require(DistanceAcross(glm::vec3(transform[3]), glm::vec3(0.0f)) > 4.5f - 1e-3f, "A scatter that keeps to the blockers should keep out of one");
		}

		// Moving the blocker makes only the other scatter again
		const uint64_t optedOutRevision = world.Get<MeshInstancesComponent>(optedOut).Revision;
		const uint64_t keepingRevision = world.Get<MeshInstancesComponent>(keeping).Revision;
		world.Get<TransformComponent>(blocker).Position.x = 3.0f;
		UpdateScatters(world);
		Require(world.Get<MeshInstancesComponent>(optedOut).Revision == optedOutRevision, "A moved blocker shouldn't make a scatter that opts out again");
		Require(world.Get<MeshInstancesComponent>(keeping).Revision > keepingRevision, "A moved blocker should make a scatter that keeps to it again");
	}

	void TestBlockersAndMasksAreSaved()
	{
		Registry world;
		AddGround(world);
		ScatterComponent values = OnGround();
		values.Size = glm::vec2(40.0f, 40.0f);
		values.Density = 3.0f;
		values.bNoiseMask = true;
		values.NoiseFrequency = 0.08f;
		values.NoiseThreshold = 0.4f;
		values.NoiseSoftness = 0.15f;
		values.NoiseSeed = 9;
		values.bNoiseInvert = true;
		const Entity masked = AddScatter(world, values);
		ScatterComponent opted = values;
		opted.Seed = 3;
		opted.bUseBlockers = false;
		AddScatter(world, opted);
		constexpr EntityGuid CircleGuid{ 0x5CA77E2000000002ull };
		constexpr EntityGuid SphereGuid{ 0x5CA77E2000000003ull };
		AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 1.25f, .Margin = 0.4f, .Falloff = 0.7f },
			TransformComponent{ .Position = glm::vec3(3.0f, 0.0f, -2.0f) }, CircleGuid);
		const Entity sphere = AddBlocker(world, ScatterBlockerComponent{ .Margin = 0.2f }, TransformComponent{ .Position = glm::vec3(-4.0f, 0.0f, 5.0f) }, SphereGuid);
		world.Add<SphereColliderComponent>(sphere, SphereColliderComponent{ .Radius = 1.5f });
		UpdateScatters(world);
		const uint64_t made = HashTransforms(*world.Get<MeshInstancesComponent>(masked).Instances);

		Nyx::Tests::TestDirectory directory("NyxScatterBlockerTests");
		const std::filesystem::path path = directory.Path / "Blockers.nyxscene";
		Require(SceneSerializer::SaveToFile(world, path), "Saving a scene with blockers failed");
		Registry loaded;
		Require(SceneSerializer::LoadFromFile(path, loaded), "Loading a scene with blockers failed");
		int blockers = 0;
		loaded.Each<ScatterBlockerComponent>([&](Entity entity, ScatterBlockerComponent& blocker)
			{
				++blockers;
				const EntityGuid guid = loaded.Get<GuidComponent>(entity).Guid;
				if (guid == CircleGuid)
				{
					Require(blocker.Shape == EScatterBlockerShape::Circle && blocker.Radius == 1.25f && blocker.Margin == 0.4f && blocker.Falloff == 0.7f, "The circle blocker's values should be saved");
				}
				else
				{
					Require(guid == SphereGuid && blocker.Shape == EScatterBlockerShape::Collider && blocker.Margin == 0.2f && blocker.Falloff == 0.0f, "The collider blocker's values should be saved");
				}
			});
		Require(blockers == 2, "The scene should have its two blockers");
		Entity loadedMasked{};
		int scatters = 0;
		loaded.Each<ScatterComponent>([&](Entity entity, ScatterComponent& scatter)
			{
				++scatters;
				Require(scatter.bNoiseMask && scatter.NoiseFrequency == 0.08f && scatter.NoiseThreshold == 0.4f && scatter.NoiseSoftness == 0.15f && scatter.NoiseSeed == 9 &&
						scatter.bNoiseInvert,
					"The noise mask's values should be saved");
				if (scatter.Seed == values.Seed)
				{
					Require(scatter.bUseBlockers, "Keeping to the blockers should be saved");
					loadedMasked = entity;
				}
				else
				{
					Require(!scatter.bUseBlockers, "Opting out of the blockers should be saved");
				}
			});
		Require(scatters == 2 && loaded.IsAlive(loadedMasked), "The scene should have its two scatters");
		// Loaded before the blockers, the first frame finds them
		UpdateScatters(loaded);
		Require(HashTransforms(*loaded.Get<MeshInstancesComponent>(loadedMasked).Instances) == made, "The loaded scatter should make the same copies with the loaded blockers");
	}

	void TimeDraggingABlocker()
	{
		Registry world;
		AddGround(world);
		ScatterComponent values = OnGround();
		values.Size = glm::vec2(116.0f, 116.0f);
		values.Density = 4.0f;
		values.Spacing = 0.25f;
		values.SlopeRange = glm::vec2(0.0f, 35.0f);
		values.AlignToNormal = 0.3f;
		const Entity scatter = AddScatter(world, values);
		const Entity blocker = AddBlocker(world, ScatterBlockerComponent{ .Shape = EScatterBlockerShape::Circle, .Radius = 1.0f, .Margin = 0.2f, .Falloff = 0.5f },
			TransformComponent{ .Position = glm::vec3(-10.0f, 0.0f, 3.0f) });
		UpdateScatters(world);
		const size_t copies = world.Get<MeshInstancesComponent>(scatter).Instances->Transforms.size();
		const uint32_t cells = world.Get<ScatterComponent>(scatter).Cells->Columns * world.Get<ScatterComponent>(scatter).Cells->Rows;

		// 5 cm a frame, as the gizmo drags it; then the same with every cell made again each frame,
		// as before the cells were kept
		const int frames = 20;
		const auto drag = [&](bool bAllCells)
		{
			double milliseconds = 0.0;
			double thrown = 0.0;
			for (int frame = 0; frame < frames; ++frame)
			{
				world.Get<TransformComponent>(blocker).Position.x += 0.05f;
				if (bAllCells)
				{
					world.Get<ScatterComponent>(scatter).Cells.reset();
				}
				const auto start = std::chrono::steady_clock::now();
				UpdateScatters(world);
				milliseconds += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
				thrown += world.Get<ScatterComponent>(scatter).Cells->CellsThrown;
			}
			return std::make_pair(milliseconds / frames, thrown / frames);
		};
		const auto [localMilliseconds, localThrown] = drag(false);
		const auto [allMilliseconds, allThrown] = drag(true);
		std::cout << "Dragging a blocker over " << copies << " copies in " << cells << " cells takes " << localMilliseconds << " ms a frame, making about " << localThrown
				  << " cells again; making all of them again takes " << allMilliseconds << " ms a frame\n";
		Require(copies > 38000 && copies < 42000, "The dragged scatter should have about 40,000 copies, has " + std::to_string(copies));
		Require(localThrown < cells / 8.0 && allThrown == cells, "A dragged blocker should make only a few cells again");
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
		TestEachInstanceHasANumberOfItsOwn();
		TestTheDensity();
		TestTheSpacing();
		TestTheSlopeAndHeightLimits();
		TestInstancesLieOnTheSurface();
		TestTheCells();
		TestThePostLoadStep();
		TestASurfaceLoadedLaterIsFound();
		TestBlockersKeepCopiesAway();
		TestTheFalloffThinsTheDensity();
		TestEditsMakeOnlyTheirCellsAgain();
		TestTheNoiseMask();
		TestTheInvertedMaskIsTheComplement();
		TestAScatterCanIgnoreBlockers();
		TestBlockersAndMasksAreSaved();
		TimeFiftyThousandInstances();
		TimeDraggingABlocker();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All scatter tests passed\n";
	return 0;
}
