// Particles without a GPU (Particles.h): an emitter stepped as the renderer steps it. The same seed
// gives the same particles after many steps, another seed others; a paused world's step changes
// nothing; particles live their lifetime and no longer; the rate and the bursts, once and again;
// an emitter's budget and the world's; gravity, buoyancy, the drag towards the wind, and a game's
// velocity field moving them; particles in the entity's space following it; the colour and size
// keys; the order alpha particles are drawn in; and a world's emitters kept, started again and
// forgotten as the world changes, its time jumping back, the emitter's seed changed; the emitter and
// the world's wind and budget saved and loaded.
#include "ComponentRegistration.h"
#include "Entity.h"
#include "EnvironmentComponent.h"
#include "Log.h"
#include "ParticleEmitterComponent.h"
#include "ParticleVelocityField.h"
#include "Particles.h"
#include "SceneSerializer.h"
#include "TestSupport.h"
#include "TransformComponent.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx;
	using Nyx::Tests::Require;

	constexpr float Step = 1.0f / 60.0f;

	bool Near(float a, float b, float tolerance = 1e-4f)
	{
		return std::abs(a - b) <= tolerance;
	}

	bool Near(const glm::vec3& a, const glm::vec3& b, float tolerance = 1e-4f)
	{
		return glm::all(glm::lessThanEqual(glm::abs(a - b), glm::vec3(tolerance)));
	}

	// Sparks: many a second, up and out, falling, in the world
	ParticleEmitterComponent Sparks()
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = 120.0f;
		emitter.Lifetime = glm::vec2(0.5f, 1.0f);
		emitter.ConeAngle = 30.0f;
		emitter.Speed = glm::vec2(2.0f, 4.0f);
		emitter.Gravity = 9.81f;
		emitter.Drag = 0.5f;
		emitter.Spin = 90.0f;
		emitter.Seed = 7;
		emitter.MaxParticles = 100000;
		return emitter;
	}

	void Run(ParticleEmitterState& state, const ParticleEmitterComponent& emitter, int steps, const glm::mat4& transform = glm::mat4(1.0f),
		const ParticleAir& air = ParticleAir{}, uint32_t worldBudget = 1000000)
	{
		for (int step = 0; step < steps; ++step)
		{
			StepParticleEmitter(state, emitter, transform, Step, air, worldBudget);
		}
	}

	bool SameParticles(const ParticleEmitterState& a, const ParticleEmitterState& b)
	{
		return a.Particles.size() == b.Particles.size() && std::memcmp(a.Particles.data(), b.Particles.data(), a.Particles.size() * sizeof(Particle)) == 0;
	}

	void TestDeterministic()
	{
		const ParticleEmitterComponent emitter = Sparks();
		ParticleEmitterState first;
		ParticleEmitterState second;
		ResetParticleEmitter(first, emitter.Seed);
		ResetParticleEmitter(second, emitter.Seed);
		Run(first, emitter, 300);
		Run(second, emitter, 300);
		Require(!first.Particles.empty(), "No particles after 5 s");
		Require(SameParticles(first, second), "The same seed gives other particles after 300 steps");

		// Another seed, others
		ParticleEmitterComponent other = emitter;
		other.Seed = 8;
		ParticleEmitterState third;
		ResetParticleEmitter(third, other.Seed);
		Run(third, other, 300);
		Require(!SameParticles(first, third), "Another seed gives the same particles");

		// A paused world's step, 0 seconds, changes nothing
		const ParticleEmitterState before = first;
		uint32_t budget = 1000000;
		StepParticleEmitter(first, emitter, glm::mat4(1.0f), 0.0f, ParticleAir{}, budget);
		Require(SameParticles(first, before) && first.Age == before.Age && first.Random == before.Random, "A step of 0 seconds changes the particles");
	}

	void TestLifetimesAndRate()
	{
		ParticleEmitterComponent emitter = Sparks();
		emitter.Gravity = 0.0f;
		ParticleEmitterState state;
		ResetParticleEmitter(state, emitter.Seed);

		// After a second at 120 a second: each younger than its lifetime, its lifetime and size in range
		Run(state, emitter, 60);
		for (const Particle& particle : state.Particles)
		{
			Require(particle.Age < particle.Lifetime, "A particle older than its lifetime");
			Require(particle.Lifetime >= 0.5f && particle.Lifetime <= 1.0f, "A lifetime out of its range");
			Require(particle.Size >= emitter.Size.x && particle.Size <= emitter.Size.y, "A size out of its range");
		}
		// In steady flow: about the rate times the mean lifetime
		Run(state, emitter, 120);
		Require(state.Particles.size() > 70 && state.Particles.size() < 110, "Not about 90 alive at 120 a second for 0.75 s each: " + std::to_string(state.Particles.size()));

		// Emitting stopped: all gone after the longest lifetime
		emitter.bEmitting = false;
		Run(state, emitter, 59);
		Run(state, emitter, 2);
		Require(state.Particles.empty(), "Particles outlive their lifetime: " + std::to_string(state.Particles.size()));

		// The rate: 10 a second for 3 s, living longer, is 30, give or take the last one's rounding
		ParticleEmitterComponent slow = Sparks();
		slow.Rate = 10.0f;
		slow.Lifetime = glm::vec2(100.0f, 100.0f);
		ParticleEmitterState counted;
		ResetParticleEmitter(counted, 1);
		Run(counted, slow, 180);
		Require(counted.Particles.size() >= 29 && counted.Particles.size() <= 30, "10 a second for 3 s isn't 30: " + std::to_string(counted.Particles.size()));
	}

	void TestBursts()
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = 0.0f;
		emitter.Lifetime = glm::vec2(100.0f, 100.0f);
		emitter.Bursts = { ParticleBurst{ .Time = 0.0f, .Count = 50 }, ParticleBurst{ .Time = 0.55f, .Count = 10, .Interval = 0.25f } };
		ParticleEmitterState state;
		ResetParticleEmitter(state, 3);
		Run(state, emitter, 1);
		Require(state.Particles.size() == 50, "The burst at 0 s isn't 50: " + std::to_string(state.Particles.size()));
		Run(state, emitter, 29);
		Require(state.Particles.size() == 50, "Something born before 0.55 s");
		Run(state, emitter, 6);
		Require(state.Particles.size() == 60, "The burst at 0.55 s isn't 10 more");
		Run(state, emitter, 30);
		// At 0.55, 0.8 and 1.05 s
		Require(state.Particles.size() == 80, "The repeating burst isn't 10 every 0.25 s: " + std::to_string(state.Particles.size()));

		// A long step catches up, each burst born as late as it was due
		ParticleEmitterState late;
		ResetParticleEmitter(late, 3);
		uint32_t budget = 1000000;
		StepParticleEmitter(late, emitter, glm::mat4(1.0f), 0.1f, ParticleAir{}, budget);
		Require(late.Particles.size() == 50, "A first step of 0.1 s isn't the first burst");
	}

	void TestBudgets()
	{
		ParticleEmitterComponent emitter = Sparks();
		emitter.Lifetime = glm::vec2(100.0f, 100.0f);
		emitter.MaxParticles = 40;
		ParticleEmitterState state;
		ResetParticleEmitter(state, 1);
		Run(state, emitter, 120);
		Require(state.Particles.size() == 40, "The emitter's budget of 40 isn't kept: " + std::to_string(state.Particles.size()));
		Require(state.Refused >= 239 - 40 && state.Refused <= 240 - 40, "The particles refused aren't counted: " + std::to_string(state.Refused));

		// The world's: taken from as particles are born, nothing more once spent
		emitter.MaxParticles = 1000;
		ParticleEmitterState world;
		ResetParticleEmitter(world, 1);
		uint32_t budget = 25;
		for (int step = 0; step < 60; ++step)
		{
			StepParticleEmitter(world, emitter, glm::mat4(1.0f), Step, ParticleAir{}, budget);
		}
		Require(world.Particles.size() == 25 && budget == 0, "The world's budget of 25 isn't kept");

		// Every emitter of a world together (UpdateParticles())
		Registry registry;
		for (int index = 0; index < 3; ++index)
		{
			const Entity entity = registry.CreateEntity();
			registry.Add<TransformComponent>(entity, TransformComponent{ .Position = glm::vec3(index * 5.0f, 0.0f, 0.0f) });
			registry.Add<ParticleEmitterComponent>(entity, emitter);
		}
		RenderParticles particles;
		for (int frame = 0; frame <= 120; ++frame)
		{
			UpdateParticles(registry, frame * Step, std::nullopt, ParticleAir{}, 100, particles);
		}
		Require(particles.Alive <= 100 && particles.Alive >= 95, "The world's budget of 100 isn't kept across emitters: " + std::to_string(particles.Alive));
		Require(particles.Instances.size() == particles.Alive && particles.Emitters.size() == 3, "Not every particle drawn");
	}

	// A game's air: blowing along +x at 3 m/s, and counting what it is asked
	struct EastWind : IParticleVelocityField
	{
		mutable size_t Asked = 0;
		mutable float LastTime = -1.0f;

		void SampleVelocities(std::span<const glm::vec3> positions, float time, std::span<glm::vec3> outVelocities) const override
		{
			Asked += positions.size();
			LastTime = time;
			for (glm::vec3& velocity : outVelocities)
			{
				velocity = glm::vec3(3.0f, 0.0f, 0.0f);
			}
		}
	};

	void TestForcesAndAir()
	{
		// One particle at a time, born still at the origin, living long
		ParticleEmitterComponent emitter;
		emitter.Rate = 0.0f;
		emitter.Bursts = { ParticleBurst{ .Time = 0.0f, .Count = 1 } };
		emitter.Lifetime = glm::vec2(100.0f, 100.0f);
		emitter.Speed = glm::vec2(0.0f, 0.0f);
		emitter.SpawnRadius = 0.0f;
		emitter.Drag = 0.0f;
		const auto after = [&](const ParticleEmitterComponent& settings, int steps, const ParticleAir& air = ParticleAir{})
		{
			ParticleEmitterState state;
			ResetParticleEmitter(state, 1);
			Run(state, settings, steps, glm::mat4(1.0f), air);
			Require(state.Particles.size() == 1, "Not one particle");
			return state.Particles.front();
		};

		// Gravity pulls down, buoyancy lifts, about a t² / 2 after a second
		ParticleEmitterComponent falling = emitter;
		falling.Gravity = 9.81f;
		Require(Near(after(falling, 60).Position.y, -0.5f * 9.81f, 0.2f), "Gravity doesn't pull down as stones fall");
		ParticleEmitterComponent rising = emitter;
		rising.Buoyancy = 2.0f;
		Require(Near(after(rising, 60).Position.y, 1.0f, 0.05f), "Buoyancy doesn't lift");

		// Without drag the wind does nothing; with it, the particle takes on its speed
		ParticleAir wind;
		wind.Wind = glm::vec3(0.0f, 0.0f, -2.0f);
		Require(Near(after(emitter, 60, wind).Position, glm::vec3(0.0f)), "The wind moves a particle without drag");
		ParticleEmitterComponent dragged = emitter;
		dragged.Drag = 10.0f;
		const Particle blown = after(dragged, 120, wind);
		Require(Near(blown.Velocity, glm::vec3(0.0f, 0.0f, -2.0f), 0.01f), "With drag, the particle doesn't take the wind's speed");
		Require(blown.Position.z < -3.5f, "The wind doesn't carry it away");

		// A game's field takes the wind's place, asked for every particle, with the world's time
		EastWind east;
		ParticleAir field;
		field.Wind = glm::vec3(0.0f, 0.0f, -2.0f);
		field.Field = &east;
		field.Time = 12.5f;
		const Particle carried = after(dragged, 120, field);
		Require(Near(carried.Velocity, glm::vec3(3.0f, 0.0f, 0.0f), 0.01f) && carried.Position.x > 5.0f, "The velocity field doesn't carry the particle along it");
		Require(east.Asked >= 119 && east.LastTime == 12.5f, "The field isn't asked for each particle each step with the time");
	}

	void TestLocalSpace()
	{
		ParticleEmitterComponent emitter;
		emitter.Rate = 0.0f;
		emitter.Bursts = { ParticleBurst{ .Time = 0.0f, .Count = 1 } };
		emitter.Lifetime = glm::vec2(100.0f, 100.0f);
		emitter.Speed = glm::vec2(1.0f, 1.0f);
		emitter.ConeAngle = 0.0f;
		emitter.Direction = glm::vec3(1.0f, 0.0f, 0.0f);
		emitter.SpawnRadius = 0.0f;
		emitter.Drag = 0.0f;

		// In the world: born at the entity, along its turned direction, then left behind
		const glm::mat4 turned = glm::rotate(glm::translate(glm::mat4(1.0f), glm::vec3(10.0f, 0.0f, 0.0f)), glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f));
		ParticleEmitterState world;
		ResetParticleEmitter(world, 1);
		Run(world, emitter, 60, turned);
		Require(Near(world.Particles.front().Position, glm::vec3(10.0f, 0.0f, -1.0f), 0.02f), "Not born at the entity along its turned direction");
		Run(world, emitter, 1, glm::translate(glm::mat4(1.0f), glm::vec3(50.0f, 0.0f, 0.0f)));
		Require(world.Particles.front().Position.x < 11.0f, "A particle in the world follows its entity");

		// In the entity's space: moved with it, its scale left out
		emitter.bLocalSpace = true;
		ParticleEmitterState local;
		ResetParticleEmitter(local, 1);
		Run(local, emitter, 60, glm::scale(turned, glm::vec3(4.0f)));
		Require(Near(local.Particles.front().Position, glm::vec3(1.0f, 0.0f, 0.0f), 0.02f), "Kept in the entity's space");
		Require(Near(glm::vec3(GetParticleSpaceToWorld(emitter, glm::scale(turned, glm::vec3(4.0f))) * glm::vec4(local.Particles.front().Position, 1.0f)),
					glm::vec3(10.0f, 0.0f, -1.0f), 0.02f),
			"Where the entity is and turned to, without its scale");
	}

	void TestKeys()
	{
		Require(GetParticleColor({}, 0.3f) == glm::vec4(1.0f) && GetParticleSizeScale({}, 0.3f) == 1.0f, "Without keys, white and the birth size");
		const std::vector<ParticleColorKey> colors = { { .Time = 1.0f, .Color = glm::vec3(0.0f), .Alpha = 0.0f }, { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.5f, 0.0f), .Alpha = 1.0f },
			{ .Time = 0.5f, .Color = glm::vec3(1.0f, 0.0f, 0.0f), .Alpha = 0.5f } };
		Require(Near(glm::vec3(GetParticleColor(colors, 0.0f)), glm::vec3(1.0f, 0.5f, 0.0f)), "The first key at birth, keys in any order");
		Require(Near(glm::vec3(GetParticleColor(colors, 0.25f)), glm::vec3(1.0f, 0.25f, 0.0f)) && Near(GetParticleColor(colors, 0.25f).a, 0.75f), "Between the first two keys");
		Require(Near(GetParticleColor(colors, 0.75f).a, 0.25f), "Between the last two keys");
		Require(Near(GetParticleColor(colors, 2.0f).a, 0.0f), "Past the last key, the last");
		const std::vector<ParticleSizeKey> sizes = { { .Time = 0.2f, .Size = 1.0f }, { .Time = 1.0f, .Size = 3.0f } };
		Require(Near(GetParticleSizeScale(sizes, 0.0f), 1.0f) && Near(GetParticleSizeScale(sizes, 0.6f), 2.0f), "The size keys");
	}

	void TestBackToFront()
	{
		std::vector<ParticleInstanceData> instances(4);
		instances[0].Position = glm::vec3(0.0f, 0.0f, -2.0f);
		instances[1].Position = glm::vec3(0.0f, 0.0f, -8.0f);
		instances[2].Position = glm::vec3(0.0f, 0.0f, -5.0f);
		instances[3].Position = glm::vec3(0.0f, 0.0f, -8.0f);
		std::vector<uint32_t> order;
		SortParticlesBackToFront(instances, glm::vec3(0.0f), order);
		Require(order == std::vector<uint32_t>{ 1, 3, 2, 0 }, "Not the farthest first, ties in the order they were born");
	}

	void TestWorld()
	{
		Registry registry;
		const Entity entity = registry.CreateEntity();
		registry.Add<TransformComponent>(entity, TransformComponent{});
		ParticleEmitterComponent emitter = Sparks();
		emitter.Blend = EParticleBlend::Alpha;
		emitter.Shape = EParticleShape::Mesh;
		registry.Add<ParticleEmitterComponent>(entity, emitter);

		RenderParticles particles;
		UpdateParticles(registry, 10.0f, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.Instances.empty() && particles.States.size() == 1 && particles.Emitters.size() == 1 && particles.Emitters[0].InstanceCount == 0,
			"The first frame starts the emitter, moving nothing");
		for (int frame = 1; frame <= 60; ++frame)
		{
			UpdateParticles(registry, 10.0f + frame * Step, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		}
		Require(particles.Emitters.size() == 1 && particles.Emitters[0].InstanceCount == particles.Alive && particles.Alive > 50, "The emitter's particles aren't drawn");
		const RenderParticleEmitter& drawn = particles.Emitters[0];
		Require(drawn.Blend == EParticleBlend::Alpha && drawn.Shape == EParticleShape::Mesh && drawn.SourceEntity == entity, "The emitter's settings aren't drawn with");
		for (const ParticleInstanceData& instance : particles.Instances)
		{
			Require(glm::all(glm::greaterThanEqual(instance.Position, drawn.Bounds.Min)) && glm::all(glm::lessThanEqual(instance.Position, drawn.Bounds.Max)), "A particle outside its emitter's bounds");
			Require(Near(glm::length(instance.Rotation), 1.0f, 1e-3f), "A mesh's turn isn't a unit quaternion");
		}

		// Paused: the world's time stands still, the particles too
		const std::vector<ParticleInstanceData> paused = particles.Instances;
		UpdateParticles(registry, 10.0f + 60 * Step, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.Instances.size() == paused.size() && std::memcmp(particles.Instances.data(), paused.data(), paused.size() * sizeof(ParticleInstanceData)) == 0,
			"The particles move while the world's time stands still");

		// A long frame moves them on by MaxParticleStep at most
		UpdateParticles(registry, 100.0f, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.States.at(entity.Value).Age < 1.0f + MaxParticleStep + 0.01f, "A long frame moved the emitter on by more than a step");

		// The seed changed: started again
		registry.Get<ParticleEmitterComponent>(entity).Seed = 99;
		UpdateParticles(registry, 100.0f + Step, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.States.at(entity.Value).Seed == 99 && particles.Alive <= 3, "A new seed doesn't start the emitter again");

		// Time back: all started again
		UpdateParticles(registry, 100.0f + 30 * Step, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		UpdateParticles(registry, 1.0f, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.Alive == 0, "A jump back in time doesn't start the emitters again");

		// Gone: forgotten
		registry.DestroyEntity(entity);
		UpdateParticles(registry, 1.0f + Step, std::nullopt, ParticleAir{}, DefaultWorldMaxParticles, particles);
		Require(particles.States.empty() && particles.Emitters.empty(), "An emitter gone isn't forgotten");
	}

	void TestSavedAndLoaded()
	{
		Nyx::Tests::TestDirectory directory("NyxParticleTests");
		const std::filesystem::path scenePath = directory.Path / "Particles.nyxscene";
		Registry world;
		ParticleEmitterComponent emitter = Sparks();
		emitter.Bursts = { ParticleBurst{ .Time = 0.5f, .Count = 7, .Interval = 2.0f } };
		emitter.ColorOverLife = { { .Time = 0.0f, .Color = glm::vec3(1.0f, 0.5f, 0.25f), .Alpha = 0.5f }, { .Time = 1.0f, .Color = glm::vec3(0.0f), .Alpha = 0.0f } };
		emitter.SizeOverLife = { { .Time = 0.3f, .Size = 2.0f } };
		emitter.Shape = EParticleShape::Mesh;
		emitter.Mesh = AssetReference{ .Type = "Mesh", .Path = "Meshes/Sphere.nyxmesh" };
		emitter.Blend = EParticleBlend::Alpha;
		emitter.bLocalSpace = true;
		emitter.bLit = true;
		emitter.SoftDistance = 0.7f;
		emitter.Seed = 1234;
		world.Add<ParticleEmitterComponent>(world.CreateEntity(), emitter);
		EnvironmentComponent environment;
		environment.Wind = glm::vec3(1.0f, 0.0f, -2.0f);
		environment.MaxParticles = 777;
		world.Add<EnvironmentComponent>(world.CreateEntity(), environment);
		Require(SceneSerializer::SaveToFile(world, scenePath), "Saving a scene with an emitter failed");

		Registry loaded;
		Require(SceneSerializer::LoadFromFile(scenePath, loaded, ScenePostLoadContext{}), "Loading a scene with an emitter failed");
		std::optional<ParticleEmitterComponent> read;
		loaded.Each<ParticleEmitterComponent>([&](Entity, const ParticleEmitterComponent& component)
			{
				read = component;
			});
		Require(read.has_value(), "The loaded scene has no emitter");
		Require(read->Rate == emitter.Rate && read->Lifetime == emitter.Lifetime && read->Speed == emitter.Speed && read->Gravity == emitter.Gravity &&
				read->Drag == emitter.Drag && read->Spin == emitter.Spin && read->Seed == 1234 && read->SoftDistance == 0.7f,
			"The emitter's numbers didn't load as saved");
		Require(read->Bursts.size() == 1 && read->Bursts[0].Time == 0.5f && read->Bursts[0].Count == 7 && read->Bursts[0].Interval == 2.0f, "The bursts didn't load as saved");
		Require(read->ColorOverLife.size() == 2 && read->ColorOverLife[0].Color == glm::vec3(1.0f, 0.5f, 0.25f) && read->ColorOverLife[0].Alpha == 0.5f &&
				read->SizeOverLife.size() == 1 && read->SizeOverLife[0].Size == 2.0f,
			"The keys didn't load as saved");
		Require(read->Shape == EParticleShape::Mesh && read->Blend == EParticleBlend::Alpha && read->bLocalSpace && read->bLit && read->Mesh.Path == "Meshes/Sphere.nyxmesh",
			"The emitter's look didn't load as saved");
		loaded.Each<EnvironmentComponent>([&](Entity, const EnvironmentComponent& component)
			{
				Require(component.Wind == environment.Wind && component.MaxParticles == 777, "The wind and the world's budget didn't load as saved");
			});
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		RegisterComponentTypes();
		TestDeterministic();
		TestLifetimesAndRate();
		TestBursts();
		TestBudgets();
		TestForcesAndAir();
		TestLocalSpace();
		TestKeys();
		TestBackToFront();
		TestWorld();
		TestSavedAndLoaded();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All particle tests passed\n";
	return 0;
}
