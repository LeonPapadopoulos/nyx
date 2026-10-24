#pragma once

#include "Entity.h"
#include "ParticleEmitterComponent.h"
#include "SceneCulling.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace Nyx
{
	class IParticleVelocityField;
	class Mesh;
	class Registry;

	// Particles (ParticleEmitterComponent) as plain math (Docs/Design/lights-and-particles.md): an
	// emitter's particles stepped on the CPU, its bursts and its budgets, the air carrying them, and
	// what each looks like at a moment of its life. No GPU, so tests step them as the renderer does.

	// The most particles alive in a world by default, every emitter's together
	// (EnvironmentComponent's Max Particles)
	inline constexpr uint32_t DefaultWorldMaxParticles = 50000;

	// How big an emitter's icon in the editor's scene views is, in metres round it, which a click
	// picks it by
	inline constexpr float ParticleEmitterIconRadius = 0.2f;

	// The longest step an emitter is moved on by in one go, in seconds: a frame that took longer, or
	// a jump in the world's time, moves it on by this much, so particles don't leap
	inline constexpr float MaxParticleStep = 0.1f;

	struct Particle
	{
		// In the world, or in the entity's space without its scale for an emitter in local space
		glm::vec3 Position{ 0.0f };
		glm::vec3 Velocity{ 0.0f };
		// Seconds since it was born, and how long it lives
		float Age = 0.0f;
		float Lifetime = 1.0f;
		// Metres across at birth
		float Size = 0.1f;
		// Radians, and radians a second, about Axis (a mesh) or the view (a billboard)
		float Angle = 0.0f;
		float Spin = 0.0f;
		glm::vec3 Axis{ 0.0f, 0.0f, 1.0f };
	};

	// What the air does to an emitter's particles this step
	struct ParticleAir
	{
		// Its speed everywhere, in metres a second, without a field
		glm::vec3 Wind{ 0.0f };
		// A game's air, asked for every particle; null: the wind
		const IParticleVelocityField* Field = nullptr;
		// The world's time, for the field
		float Time = 0.0f;
	};

	// One emitter's particles and where its randomness and its life have got to
	struct ParticleEmitterState
	{
		// The seed it started from (the component's), and its random numbers since
		int32_t Seed = 0;
		uint32_t Random = 0;
		// Seconds since it started
		float Age = 0.0f;
		// Particles owed by its rate that haven't been born yet, a fraction of one
		float Owed = 0.0f;
		// How many times each burst has gone off
		std::vector<uint32_t> BurstsFired;
		std::vector<Particle> Particles;
		// Particles that weren't born for its budget or the world's, since it started
		uint64_t Refused = 0;
	};

	// The emitter as it starts: no particles, its randomness from `seed`
	void ResetParticleEmitter(ParticleEmitterState& state, int32_t seed);

	// The emitter moved on by `seconds` (0 or more; a paused world's 0 changes nothing), placed by
	// `emitterToWorld` (the entity's transform): its particles aged and those past their life gone,
	// then pulled by gravity and buoyancy and carried by `air` (towards its speed, by the drag), and
	// moved; then those due born, by its rate and its bursts, while it emits, at most as many as
	// make its Max Particles and as `worldBudget` has room for, which is taken from. Born particles
	// are spread over the step, as if born when they were due.
	void StepParticleEmitter(ParticleEmitterState& state, const ParticleEmitterComponent& emitter, const glm::mat4& emitterToWorld, float seconds,
		const ParticleAir& air, uint32_t& worldBudget);

	// What a particle looks like at `life`, 0 at birth to 1 at its end: its colour and alpha between
	// the keys (sorted by their time), white without any; its size against its size at birth, 1
	// without any
	glm::vec4 GetParticleColor(std::span<const ParticleColorKey> keys, float life);
	float GetParticleSizeScale(std::span<const ParticleSizeKey> keys, float life);

	// Where an emitter in the entity's space is in the world: its position and turn, not its scale
	glm::mat4 GetParticleSpaceToWorld(const ParticleEmitterComponent& emitter, const glm::mat4& emitterToWorld);

	// One particle as the GPU draws it (ParticleInstance in Particle.slang, an instance's vertex
	// stream): in the world, its colour times the emitter's emissive intensity and its alpha,
	// premultiplied by neither; for a billboard its angle in x, for a mesh its turn as a quaternion
	// (x, y, z, w)
	struct ParticleInstanceData
	{
		glm::vec3 Position{ 0.0f };
		float Size = 0.0f;
		glm::vec4 Color{ 0.0f };
		glm::vec4 Rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
	};
	static_assert(sizeof(ParticleInstanceData) == 48, "ParticleInstanceData must match ParticleInstance in Particle.slang");

	// An emitter as a frame draws it, its particles in RenderParticles::Instances
	struct RenderParticleEmitter
	{
		Nyx::Entity SourceEntity{};
		EParticleBlend Blend = EParticleBlend::Additive;
		EParticleShape Shape = EParticleShape::Billboard;
		// The mesh's path for the Mesh shape; empty: the cube. The renderer finds the mesh.
		std::string MeshPath;
		const Mesh* MeshAsset = nullptr;
		bool bLit = false;
		float SoftDistance = 0.0f;
		// Its instances, and the box round all of them as drawn, in the world (its entity's position
		// without any)
		uint32_t FirstInstance = 0;
		uint32_t InstanceCount = 0;
		BoundingBox Bounds;
		// Where its entity is, for picking and the editor's overlays
		glm::vec3 WorldPosition{ 0.0f };
		// Its entity's picking id in its world; 0 until given one
		uint32_t PickingId = 0;
	};

	// A world's particles: each emitter's state, kept from frame to frame, and this frame's to draw
	struct RenderParticles
	{
		std::unordered_map<uint64_t, ParticleEmitterState> States;
		// The world's time at the last step; none before the first
		std::optional<float> LastTime;
		std::vector<RenderParticleEmitter> Emitters;
		std::vector<ParticleInstanceData> Instances;
		// Alive this frame, every emitter's
		uint32_t Alive = 0;
	};

	// The world's emitters moved on to `time` (the world's), in the order the world holds them,
	// placed where their entities are drawn (with `interpolation`, between their last two steps):
	// an emitter new to it starts there, one whose seed changed starts again, one gone is forgotten;
	// a jump back in time starts them all again. Then each one's particles as the GPU draws them,
	// every emitter listed, also one with none to draw (for the editor's icons). `air` carries them;
	// `worldMax` is the most alive at once, every emitter's together.
	void UpdateParticles(const Registry& world, float time, std::optional<float> interpolation, const ParticleAir& air, uint32_t worldMax, RenderParticles& particles);

	// The order to draw an alpha emitter's particles in, for a camera at `camera`: the farthest
	// first, as indices from its first instance; ties in the order they were born
	void SortParticlesBackToFront(std::span<const ParticleInstanceData> instances, const glm::vec3& camera, std::vector<uint32_t>& outOrder);
}
