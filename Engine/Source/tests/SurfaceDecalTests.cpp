// Surface decals without a GPU: a decal's quad follows its host as it moves, turns and is scaled,
// lifted a little off the surface and keeping its size; on a host drawn between two steps, it is
// where the host is drawn, and picked and outlined where the host is; what the renderer extracts
// of a world's decals, and what it leaves out; a decal made at run time with only its material's
// path, drawn with that material; a decal saved in a scene and loaded with its material
// resolved; the time a game's world's decals pulse by, paused and stepped.
#include "ComponentRegistration.h"
#include "DrawnWorldTime.h"
#include "Entity.h"
#include "EntityGuidLookup.h"
#include "FixedStepClock.h"
#include "GuidComponent.h"
#include "IAssetResolver.h"
#include "Log.h"
#include "Material.h"
#include "PreviousTransformComponent.h"
#include "SceneSerializer.h"
#include "SurfaceAnchor.h"
#include "SurfaceDecalComponent.h"
#include "SurfaceDecals.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <array>
#include <cmath>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
	using Nyx::EntityGuid;
	using Nyx::SurfaceAnchor;
	using Nyx::SurfaceDecalComponent;
	using Nyx::SurfaceFrame;
	using Nyx::TransformComponent;

	using Nyx::Tests::Require;

	constexpr float Tolerance = 1e-4f;

	bool Near(const glm::vec3& a, const glm::vec3& b)
	{
		return glm::length(a - b) < Tolerance;
	}

	std::string Text(const glm::vec3& v)
	{
		return "(" + std::to_string(v.x) + ", " + std::to_string(v.y) + ", " + std::to_string(v.z) + ")";
	}

	Nyx::Entity CreateHost(Nyx::Registry& world, EntityGuid guid, const TransformComponent& transform)
	{
		const Nyx::Entity host = world.CreateEntity();
		world.Add<Nyx::GuidComponent>(host, Nyx::GuidComponent{ guid });
		world.Add<TransformComponent>(host, transform);
		return host;
	}

	// Resolves the paths it has materials for, and nothing else, and counts the asks by path
	class MaterialsByPath final : public Nyx::IAssetResolver
	{
	public:
		Nyx::Mesh* ResolveMesh(const std::string&) override
		{
			return nullptr;
		}

		Nyx::Material* ResolveMaterial(const std::string& path) override
		{
			++Asks[path];
			const auto found = Materials.find(path);
			return found != Materials.end() ? found->second : nullptr;
		}

		std::map<std::string, Nyx::Material*> Materials;
		std::map<std::string, int> Asks;
	};

	// What the renderer extracts of the world's decals, its material paths resolved by `assets`
	std::vector<Nyx::RenderDecal> Extract(const Nyx::Registry& world, std::optional<float> interpolation, Nyx::IAssetResolver& assets, Nyx::DecalMaterialCache& materials)
	{
		std::vector<Nyx::RenderDecal> drawn;
		Nyx::ExtractSurfaceDecals(world, interpolation, assets, materials, drawn);
		return drawn;
	}

	// The same for decals that name no material paths
	std::vector<Nyx::RenderDecal> Extract(const Nyx::Registry& world, std::optional<float> interpolation)
	{
		MaterialsByPath assets;
		Nyx::DecalMaterialCache materials;
		return Extract(world, interpolation, assets, materials);
	}

	// The quad's corners in the order of the plane mesh's UVs, from the plane mesh's corners
	std::array<glm::vec3, 4> CornersOf(const glm::mat4& model)
	{
		const glm::vec3 plane[4] = { { -0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, -0.5f }, { 0.5f, 0.0f, 0.5f }, { -0.5f, 0.0f, 0.5f } };
		std::array<glm::vec3, 4> corners;
		for (size_t corner = 0; corner < corners.size(); ++corner)
		{
			corners[corner] = glm::vec3(model * glm::vec4(plane[corner], 1.0f));
		}
		return corners;
	}

	// A decal on the middle of a cube's top face, its tangent along the cube's x: wherever the cube
	// is, the quad lies on that face, 3 mm above it, its width along the face's x and its height
	// along the face's z, as big as it was asked to be
	void TestCornersFollowTheHost()
	{
		const glm::vec2 size(0.4f, 0.2f);
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0xDECA1 }, .LocalPosition = glm::vec3(0.0f, 0.5f, 0.0f), .LocalNormal = glm::vec3(0.0f, 1.0f, 0.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = size;

		Nyx::Registry world;
		const Nyx::Entity host = CreateHost(world, decal.Anchor.Host, TransformComponent{});
		Nyx::Material material;
		decal.MaterialAsset = &material;
		world.Add<SurfaceDecalComponent>(world.CreateEntity(), decal);

		TransformComponent moved;
		moved.Position = glm::vec3(3.0f, -2.0f, 5.0f);
		TransformComponent turned = moved;
		turned.Rotation = glm::angleAxis(glm::radians(40.0f), glm::normalize(glm::vec3(1.0f, 2.0f, 0.5f)));
		TransformComponent scaled = turned;
		scaled.Scale = glm::vec3(2.0f, 0.5f, 3.0f);

		const std::pair<const char*, TransformComponent> poses[] = {
			{ "Unmoved", TransformComponent{} },
			{ "Moved", moved },
			{ "Turned", turned },
			{ "Scaled", scaled },
		};
		for (const auto& [name, transform] : poses)
		{
			world.Get<TransformComponent>(host) = transform;
			const std::string what = name;

			// The face as the cube's own transform puts it in the world
			const glm::mat4 cube = transform.ToMatrix();
			const glm::vec3 middle = glm::vec3(cube * glm::vec4(0.0f, 0.5f, 0.0f, 1.0f));
			const glm::vec3 along = glm::normalize(glm::vec3(cube * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
			const glm::vec3 across = glm::normalize(glm::vec3(cube * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f)));
			const glm::vec3 up = glm::normalize(glm::cross(across, along));
			const glm::vec3 lifted = middle + up * Nyx::SurfaceDecalLift;
			const std::array<glm::vec3, 4> expected = {
				lifted - along * size.x * 0.5f - across * size.y * 0.5f,
				lifted + along * size.x * 0.5f - across * size.y * 0.5f,
				lifted + along * size.x * 0.5f + across * size.y * 0.5f,
				lifted - along * size.x * 0.5f + across * size.y * 0.5f,
			};

			const std::optional<SurfaceFrame> frame = Nyx::ResolveSurfaceAnchor(world, decal.Anchor);
			Require(frame.has_value(), what + ": the decal's anchor should resolve");
			const std::array<glm::vec3, 4> corners = Nyx::GetSurfaceDecalCorners(*frame, size);

			const std::vector<Nyx::RenderDecal> drawn = Extract(world, std::nullopt);
			Require(drawn.size() == 1, what + ": the decal should be drawn");
			const std::array<glm::vec3, 4> drawnCorners = CornersOf(drawn[0].Model);

			for (size_t corner = 0; corner < corners.size(); ++corner)
			{
				const std::string which = what + ", corner " + std::to_string(corner);
				Require(Near(corners[corner], expected[corner]), which + " is at " + Text(corners[corner]) + ", expected " + Text(expected[corner]));
				Require(Near(drawnCorners[corner], expected[corner]), which + ": the renderer draws it at " + Text(drawnCorners[corner]) + ", expected " + Text(expected[corner]));
			}
		}
	}

	// A decal on the front face of a cube that the last step moved and turned, as PhysicsBodies leaves
	// a dynamic body: half a step on, the quad is on the face of the cube drawn halfway (as the
	// renderer draws its mesh); drawn without a factor, on the cube where it is. Picking and the
	// selection's outline go by the cube where it is, as for meshes.
	void TestCornersFollowTheHostBetweenSteps()
	{
		const glm::vec2 size(0.6f, 0.3f);
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0xB0D7 }, .LocalPosition = glm::vec3(0.0f, 0.0f, 0.5f), .LocalNormal = glm::vec3(0.0f, 0.0f, 1.0f), .LocalTangent = glm::vec3(1.0f, 0.0f, 0.0f) };
		decal.Size = size;
		Nyx::Material material;
		decal.MaterialAsset = &material;

		Nyx::Registry world;
		const glm::vec3 axis = glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f));
		TransformComponent now;
		now.Position = glm::vec3(2.0f, 1.0f, -1.0f);
		now.Rotation = glm::angleAxis(glm::radians(60.0f), axis);
		now.Scale = glm::vec3(1.5f, 0.5f, 2.0f);
		const Nyx::Entity host = CreateHost(world, decal.Anchor.Host, now);
		Nyx::SetPreviousTransform(world.Add<Nyx::PreviousTransformComponent>(host), glm::vec3(0.0f, 1.0f, -3.0f), glm::angleAxis(glm::radians(20.0f), axis), now);
		world.Add<SurfaceDecalComponent>(world.CreateEntity(), decal);

		// The quad's corners on the front face of a cube with this transform, worked out from the
		// cube's own matrix
		const auto cornersOnFace = [&](const TransformComponent& cube)
		{
			const glm::mat4 matrix = cube.ToMatrix();
			const glm::vec3 middle = glm::vec3(matrix * glm::vec4(0.0f, 0.0f, 0.5f, 1.0f));
			const glm::vec3 along = glm::normalize(glm::vec3(matrix * glm::vec4(1.0f, 0.0f, 0.0f, 0.0f)));
			const glm::vec3 up = glm::normalize(glm::vec3(matrix * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
			const glm::vec3 out = glm::normalize(glm::cross(along, up));
			// The bitangent, tangent x normal, runs down the face
			const glm::vec3 down = glm::cross(along, out);
			const glm::vec3 lifted = middle + out * Nyx::SurfaceDecalLift;
			return std::array<glm::vec3, 4>{
				lifted - along * size.x * 0.5f - down * size.y * 0.5f,
				lifted + along * size.x * 0.5f - down * size.y * 0.5f,
				lifted + along * size.x * 0.5f + down * size.y * 0.5f,
				lifted - along * size.x * 0.5f + down * size.y * 0.5f,
			};
		};
		TransformComponent halfway = now;
		halfway.Position = glm::vec3(1.0f, 1.0f, -2.0f);
		halfway.Rotation = glm::angleAxis(glm::radians(40.0f), axis);

		const std::pair<std::optional<float>, TransformComponent> cases[] = {
			{ 0.5f, halfway },
			{ std::nullopt, now },
		};
		for (const auto& [factor, cube] : cases)
		{
			const std::string what = factor ? "Half a step on" : "Without a factor";
			const std::vector<Nyx::RenderDecal> drawn = Extract(world, factor);
			Require(drawn.size() == 1, what + ": the decal should be drawn");
			const std::array<glm::vec3, 4> corners = CornersOf(drawn[0].Model);
			const std::array<glm::vec3, 4> expected = cornersOnFace(cube);
			for (size_t corner = 0; corner < corners.size(); ++corner)
			{
				Require(Near(corners[corner], expected[corner]), what + ", corner " + std::to_string(corner) + " is drawn at " + Text(corners[corner]) + ", expected " + Text(expected[corner]));
			}

			const std::array<glm::vec3, 4> picked = CornersOf(drawn[0].WorldModel);
			const std::array<glm::vec3, 4> whereItIs = cornersOnFace(now);
			for (size_t corner = 0; corner < picked.size(); ++corner)
			{
				const std::string which = what + ", corner " + std::to_string(corner);
				Require(Near(picked[corner], whereItIs[corner]), which + " is picked at " + Text(picked[corner]) + ", expected " + Text(whereItIs[corner]));
				Require(Near(drawn[0].WorldCorners[corner], whereItIs[corner]), which + " is outlined at " + Text(drawn[0].WorldCorners[corner]) + ", expected " + Text(whereItIs[corner]));
			}
		}
	}

	// What the renderer draws of a world's decals: its values in the custom data, the reveal and
	// pulse within 0 and 1; nothing of a decal hidden, without a material, unrevealed, or whose host
	// is gone. A decal without a host is in world space.
	void TestExtraction()
	{
		Nyx::Registry world;
		Nyx::Material material;
		const Nyx::Entity host = CreateHost(world, EntityGuid{ 0x40 }, TransformComponent{ .Position = glm::vec3(1.0f, 2.0f, 3.0f) });

		const auto addDecal = [&](EntityGuid hostGuid, const std::function<void(SurfaceDecalComponent&)>& change)
		{
			SurfaceDecalComponent decal;
			decal.Anchor.Host = hostGuid;
			decal.MaterialAsset = &material;
			if (change)
			{
				change(decal);
			}
			const Nyx::Entity entity = world.CreateEntity();
			world.Add<SurfaceDecalComponent>(entity, decal);
			return entity;
		};
		const Nyx::Entity onHost = addDecal(EntityGuid{ 0x40 }, [](SurfaceDecalComponent& decal)
			{
				decal.Color = glm::vec3(0.25f, 0.5f, 1.0f);
				decal.Intensity = 6.0f;
				decal.Reveal = 2.0f;
				decal.Pulse = -1.0f;
			});
		const Nyx::Entity inWorld = addDecal(EntityGuid{}, [](SurfaceDecalComponent& decal)
			{
				decal.Anchor.LocalPosition = glm::vec3(0.0f, 1.0f, 0.0f);
			});
		addDecal(EntityGuid{ 0x40 }, [](SurfaceDecalComponent& decal)
			{
				decal.bVisible = false;
			});
		addDecal(EntityGuid{ 0x40 }, [](SurfaceDecalComponent& decal)
			{
				decal.MaterialAsset = nullptr;
			});
		addDecal(EntityGuid{ 0x40 }, [](SurfaceDecalComponent& decal)
			{
				decal.Reveal = 0.0f;
			});
		addDecal(EntityGuid{ 0x41 }, nullptr);

		std::vector<Nyx::RenderDecal> drawn = Extract(world, std::nullopt);
		Require(drawn.size() == 2, "Two of the decals should be drawn, not " + std::to_string(drawn.size()));
		Require(drawn[0].SourceEntity == onHost && drawn[1].SourceEntity == inWorld, "The decals should be drawn in the world's order");
		Require(drawn[0].MaterialAsset == &material, "The decal should be drawn with its material");
		Require(drawn[0].CustomData == glm::vec4(0.25f, 0.5f, 1.0f, 6.0f), "The decal's colour and intensity should be in its custom data");
		Require(drawn[0].CustomData2 == glm::vec2(1.0f, 0.0f), "The decal's reveal and pulse should be within 0 and 1");
		Require(Near(glm::vec3(drawn[0].Model[3]), glm::vec3(1.0f, 2.0f + Nyx::SurfaceDecalLift, 3.0f)), "The decal should be on its host");
		Require(Near(glm::vec3(drawn[1].Model[3]), glm::vec3(0.0f, 1.0f + Nyx::SurfaceDecalLift, 0.0f)), "The decal without a host should be in world space");

		// Its host gone, the decal on it isn't drawn
		world.DestroyEntity(host);
		drawn = Extract(world, std::nullopt);
		Require(drawn.size() == 1 && drawn[0].SourceEntity == inWorld, "A decal whose host is gone shouldn't be drawn");
	}

	// A game's world drawn between its steps: its decals pulse by its time at the moment its poses are
	// drawn at. Paused, the time stands still, however long the frames take; Step moves it on by
	// one step.
	void TestTimeOfAWorldDrawnBetweenSteps()
	{
		const auto near = [](float a, double b)
		{
			return std::abs(static_cast<double>(a) - b) < 1e-5;
		};
		Nyx::FixedStepClock clock;
		clock.SetStepSeconds(0.1);

		// Two steps run, half of the next one passed: drawn halfway through the second step
		Require(clock.Advance(0.25) == 2, "The clock should run two steps");
		Require(near(static_cast<float>(clock.GetDrawFactor()), 0.5) && near(Nyx::GetDrawnWorldTime(clock), 0.15),
			"Half a step after the second, the world should be drawn at 0.15 s, halfway, not " + std::to_string(Nyx::GetDrawnWorldTime(clock)) + " s");

		// Paused: where the last step left it, at its end, for as long as it stays paused
		clock.SetPaused(true);
		for (int frame = 0; frame < 3; ++frame)
		{
			Require(clock.Advance(1.0) == 0, "Paused, no steps should run");
			Require(near(Nyx::GetDrawnWorldTime(clock), 0.2),
				"Paused, the world should be drawn at 0.2 s, the last step's end, not " + std::to_string(Nyx::GetDrawnWorldTime(clock)) + " s");
		}

		// Step: one step on, and still again
		clock.StepOnce();
		Require(clock.Advance(0.0) == 1, "Step should run one step");
		Require(near(Nyx::GetDrawnWorldTime(clock), 0.3), "Stepped, the world should be drawn at 0.3 s");
		Require(clock.Advance(1.0) == 0 && near(Nyx::GetDrawnWorldTime(clock), 0.3), "After the step, the time should stand still again");
	}

	// A decal added while the game runs, with only its material's path, as a system adds one: drawn
	// with the path's material from the next extraction on, the path asked for once. A path that
	// names nothing isn't drawn and isn't asked for again; a path changed is followed.
	void TestMaterialsByPath()
	{
		Nyx::Material ring;
		Nyx::Material plain;
		MaterialsByPath assets;
		assets.Materials["Materials/Ring.nyxmat"] = &ring;
		assets.Materials["Materials/Plain.nyxmat"] = &plain;
		Nyx::DecalMaterialCache materials;

		Nyx::Registry world;
		SurfaceDecalComponent decal;
		decal.Material = Nyx::AssetReference{ .Type = "Material", .Path = "Materials/Ring.nyxmat" };
		const Nyx::Entity entity = world.CreateEntity();
		world.Add<SurfaceDecalComponent>(entity, decal);
		Require(!world.Get<SurfaceDecalComponent>(entity).MaterialAsset, "The test needs a decal without a resolved material");

		for (int frame = 0; frame < 3; ++frame)
		{
			const std::vector<Nyx::RenderDecal> drawn = Extract(world, std::nullopt, assets, materials);
			Require(drawn.size() == 1 && drawn[0].MaterialAsset == &ring, "A decal with only its material's path should be drawn with that material");
		}
		Require(assets.Asks["Materials/Ring.nyxmat"] == 1, "The path should be resolved once, not " + std::to_string(assets.Asks["Materials/Ring.nyxmat"]) + " times");

		// A path that names nothing: not drawn, asked for once
		world.Get<SurfaceDecalComponent>(entity).Material.Path = "Materials/Nothing.nyxmat";
		for (int frame = 0; frame < 3; ++frame)
		{
			Require(Extract(world, std::nullopt, assets, materials).empty(), "A decal whose material's path names nothing shouldn't be drawn");
		}
		Require(assets.Asks["Materials/Nothing.nyxmat"] == 1, "A path that names nothing should be asked for once");

		// Swapped for another path, even with the old material resolved into the component
		SurfaceDecalComponent& component = world.Get<SurfaceDecalComponent>(entity);
		component.MaterialAsset = &ring;
		component.Material.Path = "Materials/Plain.nyxmat";
		const std::vector<Nyx::RenderDecal> drawn = Extract(world, std::nullopt, assets, materials);
		Require(drawn.size() == 1 && drawn[0].MaterialAsset == &plain, "A decal whose path changed should be drawn with the new path's material");
	}

	// Gives one material for every path, and counts the asks
	class TestAssetResolver final : public Nyx::IAssetResolver
	{
	public:
		Nyx::Mesh* ResolveMesh(const std::string&) override
		{
			return nullptr;
		}

		Nyx::Material* ResolveMaterial(const std::string& path) override
		{
			Paths.push_back(path);
			return &Resolved;
		}

		Nyx::Material Resolved;
		std::vector<std::string> Paths;
	};

	// A decal in the middle of the host's top face, `size` big, its tangent along x
	Nyx::Entity AddTopDecal(Nyx::Registry& world, EntityGuid host, const glm::vec2& size)
	{
		const Nyx::Entity decal = world.CreateEntity();
		SurfaceDecalComponent component;
		component.Anchor = SurfaceAnchor{ .Host = host, .LocalPosition = glm::vec3(0.0f, 0.5f, 0.0f) };
		component.Size = size;
		world.Add<SurfaceDecalComponent>(decal, component);
		return decal;
	}

	// What the cursor points at: the nearest decal's quad, hit from the front, where on it
	void TestRaysFindDecals()
	{
		Nyx::Registry world;
		const EntityGuid nearHost = EntityGuid::Generate();
		const EntityGuid farHost = EntityGuid::Generate();
		CreateHost(world, nearHost, TransformComponent{ .Position = glm::vec3(0.0f, 1.0f, 0.0f) });
		CreateHost(world, farHost, TransformComponent{ .Position = glm::vec3(0.0f, -2.0f, 0.0f) });
		const Nyx::Entity nearDecal = AddTopDecal(world, nearHost, glm::vec2(0.6f, 0.4f));
		const Nyx::Entity farDecal = AddTopDecal(world, farHost, glm::vec2(4.0f));
		const Nyx::EntityGuidLookup lookup = Nyx::MakeEntityGuidLookup(world);
		const glm::vec3 down(0.0f, -1.0f, 0.0f);

		// Straight down on the near quad's middle: it is in front of the far one
		const std::optional<Nyx::SurfaceDecalHit> middle =
			Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(0.0f, 5.0f, 0.0f), down }, 100.0f);
		Require(middle && middle->Decal == nearDecal, "The ray should find the nearer decal");
		Require(Near(middle->Point, glm::vec3(0.0f, 1.5f + Nyx::SurfaceDecalLift, 0.0f)) && std::abs(middle->Distance - (3.5f - Nyx::SurfaceDecalLift)) < Tolerance,
			"It should meet the quad where it lies, 3 mm above the face: " + Text(middle->Point));
		Require(glm::length(middle->UV - glm::vec2(0.5f)) < Tolerance, "The middle of the quad should be UV (0.5, 0.5)");

		// Towards a corner: UV along the tangent (x) and the bitangent
		const std::optional<Nyx::SurfaceDecalHit> corner =
			Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(0.25f, 5.0f, 0.15f), down }, 100.0f);
		Require(corner && corner->Decal == nearDecal, "Inside the quad, the ray should find it");
		Require(corner->UV.x > 0.9f && std::abs(corner->UV.y - 0.5f) > 0.3f, "Near a corner, the UV should be near the quad's edges");

		// Beside the near quad, but over the big far one
		const std::optional<Nyx::SurfaceDecalHit> beside =
			Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(1.0f, 5.0f, 0.0f), down }, 100.0f);
		Require(beside && beside->Decal == farDecal, "Outside a quad, the ray should go on to the next");

		// From below, from the back: nothing; too short: nothing
		Require(!Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(0.0f, -5.0f, 0.0f), -down }, 100.0f),
			"A decal shouldn't be hit from behind");
		Require(!Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(0.0f, 5.0f, 0.0f), down }, 3.0f),
			"A decal beyond the distance shouldn't be hit");

		// Hidden: the far one shows through
		world.Get<SurfaceDecalComponent>(nearDecal).bVisible = false;
		const std::optional<Nyx::SurfaceDecalHit> hidden =
			Nyx::RaycastSurfaceDecals(world, lookup, Nyx::Ray{ glm::vec3(0.0f, 5.0f, 0.0f), down }, 100.0f);
		Require(hidden && hidden->Decal == farDecal, "A hidden decal shouldn't be hit");
	}

	void TestSavedAndLoaded()
	{
		Nyx::Tests::TestDirectory directory("NyxSurfaceDecalTests");
		const std::filesystem::path scenePath = directory.Path / "Decal.nyxscene";

		Nyx::Registry world;
		CreateHost(world, EntityGuid{ 0x77 }, TransformComponent{});
		SurfaceDecalComponent decal;
		decal.Anchor = SurfaceAnchor{ .Host = EntityGuid{ 0x77 }, .LocalPosition = glm::vec3(0.1f, 0.5f, -0.2f), .LocalNormal = glm::vec3(0.0f, 1.0f, 0.0f), .LocalTangent = glm::vec3(0.0f, 0.0f, 1.0f) };
		decal.Size = glm::vec2(0.3f, 0.6f);
		decal.Material = Nyx::AssetReference{ .Type = "Material", .Path = "Materials/MaskedDecal.nyxmat" };
		decal.Color = glm::vec3(1.0f, 0.4f, 0.1f);
		decal.Intensity = 8.0f;
		decal.Reveal = 0.25f;
		decal.Pulse = 0.75f;
		decal.bVisible = false;
		world.Add<SurfaceDecalComponent>(world.CreateEntity(), decal);
		Require(Nyx::SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with a decal failed");

		Nyx::Registry loaded;
		TestAssetResolver resolver;
		Require(Nyx::SceneSerializer::LoadFromFile(scenePath, loaded, Nyx::ScenePostLoadContext{ .AssetResolver = &resolver }), "Loading a scene with a decal failed");

		std::optional<SurfaceDecalComponent> read;
		loaded.Each<SurfaceDecalComponent>([&](Nyx::Entity, const SurfaceDecalComponent& component)
			{
				read = component;
			});
		Require(read.has_value(), "The loaded scene has no decal");
		Require(read->Anchor.Host == decal.Anchor.Host && read->Anchor.LocalPosition == decal.Anchor.LocalPosition &&
				read->Anchor.LocalNormal == decal.Anchor.LocalNormal && read->Anchor.LocalTangent == decal.Anchor.LocalTangent,
			"The decal's anchor didn't load as saved");
		Require(read->Size == decal.Size && read->Color == decal.Color && read->Intensity == decal.Intensity && read->Reveal == decal.Reveal &&
				read->Pulse == decal.Pulse && read->bVisible == decal.bVisible,
			"The decal's values didn't load as saved");
		Require(read->Material.Type == "Material" && read->Material.Path == "Materials/MaskedDecal.nyxmat", "The decal's material didn't load as saved");
		Require(read->MaterialAsset == &resolver.Resolved && resolver.Paths == std::vector<std::string>{ "Materials/MaskedDecal.nyxmat" },
			"Loading didn't resolve the decal's material");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterComponentTypes();
		TestCornersFollowTheHost();
		TestCornersFollowTheHostBetweenSteps();
		TestExtraction();
		TestMaterialsByPath();
		TestRaysFindDecals();
		TestSavedAndLoaded();
		TestTimeOfAWorldDrawnBetweenSteps();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All surface decal tests passed\n";
	return 0;
}
