// Surface anchors: a frame on an entity's surface that follows the entity as it moves, turns and is
// scaled, made from a world frame and resolved back to it, and what happens without a host.
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "GuidComponent.h"
#include "Log.h"
#include "SurfaceAnchor.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <cmath>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace
{
	using Nyx::EntityGuid;
	using Nyx::SurfaceAnchor;
	using Nyx::SurfaceFrame;
	using Nyx::TransformComponent;

	using Nyx::Tests::Require;

	constexpr float Tolerance = 1e-4f;

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < Tolerance;
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) < Tolerance;
	}

	// Unit normal and tangent at right angles, and the bitangent that makes them right-handed
	void RequireOrthonormal(const SurfaceFrame& frame, const std::string& what)
	{
		Require(Near(glm::length(frame.Normal), 1.0f), what + ": the normal should be unit length");
		Require(Near(glm::length(frame.Tangent), 1.0f), what + ": the tangent should be unit length");
		Require(Near(glm::dot(frame.Normal, frame.Tangent), 0.0f), what + ": the tangent should be at right angles to the normal");
		Require(Near(frame.Bitangent, glm::cross(frame.Tangent, frame.Normal)), what + ": the bitangent should be tangent x normal");

		const glm::mat4 matrix = frame.ToMatrix();
		Require(Near(glm::vec3(matrix * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)), frame.Position), what + ": the matrix should put its origin at the position");
		Require(Near(glm::vec3(matrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)), frame.Normal), what + ": the matrix's y should be the normal");
		Require(Near(glm::determinant(glm::mat3(matrix)), 1.0f), what + ": the matrix should turn without mirroring");
	}

	Nyx::Entity CreateHost(Nyx::Registry& world, EntityGuid guid, const TransformComponent& transform)
	{
		const Nyx::Entity host = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(host, Nyx::GuidComponent{ guid });
		world.Add<TransformComponent>(host, transform);
		return host;
	}

	SurfaceFrame RequireResolved(const Nyx::Registry& world, const SurfaceAnchor& anchor, const std::string& what)
	{
		const std::optional<SurfaceFrame> frame = Nyx::ResolveSurfaceAnchor(world, anchor);
		Require(frame.has_value(), what + ": the anchor should resolve");
		RequireOrthonormal(*frame, what);

		// Looking the host up in a lookup gives the same frame
		const std::optional<SurfaceFrame> fromLookup =
			Nyx::ResolveSurfaceAnchor(world, Nyx::MakeEntityGuidLookup(world), anchor);
		Require(fromLookup.has_value() && Near(fromLookup->Position, frame->Position) &&
				Near(fromLookup->Normal, frame->Normal) && Near(fromLookup->Tangent, frame->Tangent),
			what + ": resolving through a lookup should give the same frame");
		return *frame;
	}

	void TestResolvingFollowsTheHost()
	{
		// A slanted face of the host: spanned by edgeA and edgeB, its outward normal their cross
		const glm::vec3 point(0.5f, -0.25f, 1.0f);
		const glm::vec3 edgeA(1.0f, 1.0f, 0.0f);
		const glm::vec3 edgeB(0.0f, 1.0f, 1.0f);
		const SurfaceAnchor anchor{ .Host = EntityGuid{ 0x51 },
			.LocalPosition = point,
			.LocalNormal = glm::cross(edgeA, edgeB),
			.LocalTangent = edgeA };

		Nyx::Registry world;
		const Nyx::Entity host = CreateHost(world, anchor.Host, TransformComponent{});

		// The same host moved, then turned too, then scaled unevenly too, then mirrored
		TransformComponent moved;
		moved.Position = glm::vec3(3.0f, -2.0f, 5.0f);
		TransformComponent turned = moved;
		turned.Rotation = glm::angleAxis(glm::radians(40.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
		TransformComponent scaled = turned;
		scaled.Scale = glm::vec3(2.0f, 0.5f, 3.0f);
		TransformComponent mirrored = scaled;
		mirrored.Scale.x = -mirrored.Scale.x;

		const std::pair<const char*, TransformComponent> poses[] = {
			{ "Unmoved", TransformComponent{} },
			{ "Moved", moved },
			{ "Turned", turned },
			{ "Scaled", scaled },
			{ "Mirrored", mirrored },
		};

		for (const auto& [name, transform] : poses)
		{
			world.Get<TransformComponent>(host) = transform;
			const SurfaceFrame frame = RequireResolved(world, anchor, name);

			// Where the renderer draws the face
			const glm::mat4 model = transform.ToMatrix();
			const auto toWorld = [&](const glm::vec3& local)
			{
				return glm::vec3(model * glm::vec4(local, 1.0f));
			};
			const glm::vec3 worldEdgeA = toWorld(point + edgeA) - toWorld(point);
			const glm::vec3 worldEdgeB = toWorld(point + edgeB) - toWorld(point);
			const glm::vec3 outward = toWorld(point + anchor.LocalNormal) - toWorld(point);

			Require(Near(frame.Position, toWorld(point)), std::string(name) + ": the position should move with the host");
			Require(Near(glm::dot(frame.Normal, glm::normalize(worldEdgeA)), 0.0f) &&
					Near(glm::dot(frame.Normal, glm::normalize(worldEdgeB)), 0.0f),
				std::string(name) + ": the normal should stay at right angles to the face");
			Require(glm::dot(frame.Normal, outward) > 0.0f, std::string(name) + ": the normal should point out of the face");
			Require(Near(frame.Tangent, glm::normalize(worldEdgeA)), std::string(name) + ": the tangent should run along its edge");
		}
	}

	void TestMakeThenResolveGivesTheFrameBack()
	{
		Nyx::Registry world;
		TransformComponent transform;
		transform.Position = glm::vec3(-4.0f, 1.0f, 2.5f);
		transform.Rotation = glm::angleAxis(glm::radians(-110.0f), glm::normalize(glm::vec3(0.2f, -1.0f, 0.7f)));
		transform.Scale = glm::vec3(0.25f, 4.0f, -1.5f);
		const Nyx::Entity host = CreateHost(world, EntityGuid{ 0x52 }, transform);

		const glm::vec3 position(1.0f, 2.0f, 3.0f);
		const glm::vec3 normal = glm::normalize(glm::vec3(0.3f, 1.0f, -0.2f));
		// Not at right angles to the normal: making the anchor straightens it
		const glm::vec3 tangent(1.0f, 0.5f, 0.0f);
		const glm::vec3 straightTangent = glm::normalize(tangent - normal * glm::dot(normal, tangent));

		const SurfaceAnchor anchor = Nyx::MakeSurfaceAnchor(world, host, position, normal, tangent);
		Require(anchor.Host == EntityGuid{ 0x52 }, "The anchor should name its host by guid");

		const SurfaceFrame frame = RequireResolved(world, anchor, "Made");
		Require(Near(frame.Position, position), "Resolving the anchor should give its position back");
		Require(Near(frame.Normal, normal), "Resolving the anchor should give its normal back");
		Require(Near(frame.Tangent, straightTangent), "Resolving the anchor should give its tangent back, at right angles to the normal");

		// Without a host the anchor is in world space, and still gives the frame back
		const SurfaceAnchor unhosted = Nyx::MakeSurfaceAnchor(world, Nyx::Entity{}, position, normal, tangent);
		Require(!unhosted.Host.IsValid(), "An anchor made without a host should have no host");
		const SurfaceFrame unhostedFrame = RequireResolved(world, unhosted, "Made without a host");
		Require(Near(unhostedFrame.Position, position) && Near(unhostedFrame.Normal, normal) &&
				Near(unhostedFrame.Tangent, straightTangent),
			"An anchor made without a host should give its frame back");
	}

	void TestAMissingHostGivesNothing()
	{
		Nyx::Registry world;
		const SurfaceAnchor nobody{ .Host = EntityGuid{ 0x53 } };
		Require(!Nyx::ResolveSurfaceAnchor(world, nobody).has_value(), "An anchor whose host doesn't exist should resolve to nothing");

		// Its host deleted after the anchor was made, and after the lookup was made
		const Nyx::Entity host = CreateHost(world, EntityGuid{ 0x54 }, TransformComponent{});
		const SurfaceAnchor anchor =
			Nyx::MakeSurfaceAnchor(world, host, glm::vec3(1.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f));
		const Nyx::EntityGuidLookup lookup = Nyx::MakeEntityGuidLookup(world);
		world.DestroyEntity(host);

		Require(!Nyx::ResolveSurfaceAnchor(world, anchor).has_value(), "An anchor whose host was deleted should resolve to nothing");
		Require(!Nyx::ResolveSurfaceAnchor(world, lookup, anchor).has_value(),
			"An anchor whose host was deleted after the lookup was made should resolve to nothing");
	}

	void TestAWorldSpaceAnchorResolvesToItself()
	{
		Nyx::Registry world;
		const SurfaceAnchor anchor{ .LocalPosition = glm::vec3(7.0f, -3.0f, 0.5f),
			.LocalNormal = glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)),
			.LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };

		const SurfaceFrame frame = RequireResolved(world, anchor, "World space");
		Require(Near(frame.Position, anchor.LocalPosition), "A world-space anchor should stay where it is");
		Require(Near(frame.Normal, anchor.LocalNormal), "A world-space anchor should keep its normal");
		Require(Near(frame.Tangent, anchor.LocalTangent), "A world-space anchor should keep its tangent");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestResolvingFollowsTheHost();
		TestMakeThenResolveGivesTheFrameBack();
		TestAMissingHostGivesNothing();
		TestAWorldSpaceAnchorResolvesToItself();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Surface anchor test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All surface anchor tests passed.\n";
	return 0;
}
