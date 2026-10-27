#include "NyxPCH.h"
#include "Particles.h"

#include "WindField.h"
#include "PreviousTransformComponent.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <unordered_set>

namespace Nyx
{
	namespace
	{
		// The next of an emitter's random numbers (PCG's output on a linear congruential state),
		// from 0 to 1, 1 left out
		float NextRandom(uint32_t& state)
		{
			state = state * 747796405u + 2891336453u;
			uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
			word = (word >> 22u) ^ word;
			return static_cast<float>(word >> 8) * (1.0f / 16777216.0f);
		}

		float RandomBetween(uint32_t& state, const glm::vec2& range)
		{
			return range.x + (range.y - range.x) * NextRandom(state);
		}

		// A direction at random within `coneRadians` of `axis` (normalised), each as likely
		glm::vec3 RandomInCone(uint32_t& state, const glm::vec3& axis, float coneRadians)
		{
			const float cosine = 1.0f - NextRandom(state) * (1.0f - std::cos(coneRadians));
			const float sine = std::sqrt(std::max(1.0f - cosine * cosine, 0.0f));
			const float turn = NextRandom(state) * glm::two_pi<float>();
			const glm::vec3 helper = std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 tangent = glm::normalize(glm::cross(helper, axis));
			const glm::vec3 bitangent = glm::cross(axis, tangent);
			return axis * cosine + (tangent * std::cos(turn) + bitangent * std::sin(turn)) * sine;
		}

		// A point at random in a ball of `radius` round the origin, each place as likely
		glm::vec3 RandomInBall(uint32_t& state, float radius)
		{
			const glm::vec3 direction = RandomInCone(state, glm::vec3(0.0f, 1.0f, 0.0f), glm::pi<float>());
			return direction * (radius * std::cbrt(NextRandom(state)));
		}

		// The keys round `life`: the last at or before it and the first at or after it, in any order
		template<typename Key>
		std::pair<const Key*, const Key*> KeysAround(std::span<const Key> keys, float life)
		{
			const Key* before = nullptr;
			const Key* after = nullptr;
			for (const Key& key : keys)
			{
				if (key.Time <= life && (!before || key.Time >= before->Time))
				{
					before = &key;
				}
				if (key.Time >= life && (!after || key.Time < after->Time))
				{
					after = &key;
				}
			}
			return { before ? before : after, after ? after : before };
		}

		template<typename Key>
		float Between(const Key& before, const Key& after, float life)
		{
			return after.Time > before.Time ? std::clamp((life - before.Time) / (after.Time - before.Time), 0.0f, 1.0f) : 0.0f;
		}
	}

	void ResetParticleEmitter(ParticleEmitterState& state, int32_t seed)
	{
		state = ParticleEmitterState{};
		state.Seed = seed;
		state.Random = static_cast<uint32_t>(seed) * 0x9E3779B9u + 0x7F4A7C15u;
	}

	glm::mat4 GetParticleSpaceToWorld(const ParticleEmitterComponent&, const glm::mat4& emitterToWorld)
	{
		glm::mat4 space(1.0f);
		for (int column = 0; column < 3; ++column)
		{
			const glm::vec3 axis(emitterToWorld[column]);
			const float length = glm::length(axis);
			space[column] = glm::vec4(length > 0.0f ? axis / length : glm::vec3(0.0f), 0.0f);
		}
		space[3] = emitterToWorld[3];
		return space;
	}

	void AddParticleWind(const WindState& wind, std::span<const glm::vec3> positions, float time, std::span<glm::vec3> airSpeeds)
	{
		if (!(wind.Speed > 0.0f) || positions.empty())
		{
			return;
		}
		glm::vec3 low = positions.front();
		glm::vec3 high = positions.front();
		for (const glm::vec3& position : positions)
		{
			low = glm::min(low, position);
			high = glm::max(high, position);
		}
		// A quarter of the smallest eddy the wind has
		const float eddies = std::max(wind.To.EddiesPerMetre, wind.PatternBlend < 1.0f ? wind.From.EddiesPerMetre : 0.0f);
		const float cell = std::clamp(eddies > 0.0f ? 0.25f / eddies : ParticleWindCell, 0.05f, ParticleWindCell);
		const glm::ivec3 cells = glm::ivec3(glm::floor((high - low) / cell)) + 2;
		const size_t corners = static_cast<size_t>(cells.x) * static_cast<size_t>(cells.y) * static_cast<size_t>(cells.z);
		if (corners * 2 > positions.size())
		{
			AddWind(wind, positions, time, airSpeeds);
			return;
		}

		std::vector<glm::vec3> points;
		points.reserve(corners);
		for (int32_t z = 0; z < cells.z; ++z)
		{
			for (int32_t y = 0; y < cells.y; ++y)
			{
				for (int32_t x = 0; x < cells.x; ++x)
				{
					points.push_back(low + glm::vec3(static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)) * cell);
				}
			}
		}
		std::vector<glm::vec3> lattice(points.size(), glm::vec3(0.0f));
		AddWind(wind, points, time, lattice);

		const auto at = [&](int32_t x, int32_t y, int32_t z) -> const glm::vec3& { return lattice[static_cast<size_t>(x + cells.x * (y + cells.y * z))]; };
		for (size_t index = 0; index < positions.size(); ++index)
		{
			const glm::vec3 q = (positions[index] - low) / cell;
			const glm::ivec3 c = glm::clamp(glm::ivec3(q), glm::ivec3(0), cells - 2);
			const glm::vec3 f = glm::clamp(q - glm::vec3(c), 0.0f, 1.0f);
			const glm::vec3 x00 = glm::mix(at(c.x, c.y, c.z), at(c.x + 1, c.y, c.z), f.x);
			const glm::vec3 x10 = glm::mix(at(c.x, c.y + 1, c.z), at(c.x + 1, c.y + 1, c.z), f.x);
			const glm::vec3 x01 = glm::mix(at(c.x, c.y, c.z + 1), at(c.x + 1, c.y, c.z + 1), f.x);
			const glm::vec3 x11 = glm::mix(at(c.x, c.y + 1, c.z + 1), at(c.x + 1, c.y + 1, c.z + 1), f.x);
			airSpeeds[index] += glm::mix(glm::mix(x00, x10, f.y), glm::mix(x01, x11, f.y), f.z);
		}
	}

	void StepParticleEmitter(ParticleEmitterState& state, const ParticleEmitterComponent& emitter, const glm::mat4& emitterToWorld, float seconds,
		const ParticleAir& air, uint32_t& worldBudget)
	{
		if (!(seconds > 0.0f))
		{
			return;
		}

		// Particles are kept in the world, or in the entity's space without its scale
		const glm::mat4 entitySpace = GetParticleSpaceToWorld(emitter, emitterToWorld);
		const glm::mat4 space = emitter.bLocalSpace ? entitySpace : glm::mat4(1.0f);
		const glm::mat3 worldToSpace = glm::transpose(glm::mat3(space));

		// The air's speed at each particle, in the particles' space: the world's wind there and what a
		// game's air adds to it
		const float drag = std::max(emitter.Drag, 0.0f);
		const float pull = 1.0f - std::exp(-drag * seconds);
		std::vector<glm::vec3> airSpeeds;
		const bool bWind = air.Wind.Speed > 0.0f;
		if (pull > 0.0f && (bWind || air.Field) && !state.Particles.empty())
		{
			std::vector<glm::vec3> positions(state.Particles.size());
			for (size_t index = 0; index < positions.size(); ++index)
			{
				positions[index] = emitter.bLocalSpace ? glm::vec3(space * glm::vec4(state.Particles[index].Position, 1.0f)) : state.Particles[index].Position;
			}
			airSpeeds.assign(positions.size(), glm::vec3(0.0f));
			if (air.Field)
			{
				air.Field->SampleWind(positions, air.Time, airSpeeds);
			}
			AddParticleWind(air.Wind, positions, air.Time, airSpeeds);
			if (emitter.bLocalSpace)
			{
				for (glm::vec3& speed : airSpeeds)
				{
					speed = worldToSpace * speed;
				}
			}
		}
		const glm::vec3 still(0.0f);

		// Aged, those past their life gone; then pulled, carried towards the air's speed and moved.
		// One pass in plain floats, which a Debug build runs many times faster than glm's operators.
		const glm::vec3 kick = worldToSpace * glm::vec3(0.0f, emitter.Buoyancy - emitter.Gravity, 0.0f) * seconds;
		Particle* const particles = state.Particles.data();
		const size_t count = state.Particles.size();
		size_t kept = 0;
		for (size_t index = 0; index < count; ++index)
		{
			Particle particle = particles[index];
			particle.Age += seconds;
			if (particle.Age >= particle.Lifetime)
			{
				continue;
			}
			const glm::vec3& airSpeed = airSpeeds.empty() ? still : airSpeeds[index];
			glm::vec3& velocity = particle.Velocity;
			velocity.x += kick.x;
			velocity.y += kick.y;
			velocity.z += kick.z;
			velocity.x += (airSpeed.x - velocity.x) * pull;
			velocity.y += (airSpeed.y - velocity.y) * pull;
			velocity.z += (airSpeed.z - velocity.z) * pull;
			particle.Position.x += velocity.x * seconds;
			particle.Position.y += velocity.y * seconds;
			particle.Position.z += velocity.z * seconds;
			particle.Angle += particle.Spin * seconds;
			particles[kept++] = particle;
		}
		state.Particles.resize(kept);

		// Born, each as if `late` seconds ago within the step
		const glm::vec3 direction = glm::length(emitter.Direction) > 0.0f ? glm::normalize(emitter.Direction) : glm::vec3(0.0f, 1.0f, 0.0f);
		const float cone = glm::radians(std::clamp(emitter.ConeAngle, 0.0f, 180.0f));
		const glm::mat4 bornTo = emitter.bLocalSpace ? glm::mat4(1.0f) : entitySpace;
		const size_t maxParticles = static_cast<size_t>(std::max(emitter.MaxParticles, 0));
		const auto bear = [&](float late)
		{
			// The same random numbers in the same order whether or not it is born
			Particle particle;
			particle.Lifetime = std::max(RandomBetween(state.Random, glm::max(emitter.Lifetime, glm::vec2(0.0f))), 0.0f);
			particle.Size = std::max(RandomBetween(state.Random, emitter.Size), 0.0f);
			const glm::vec3 offset = RandomInBall(state.Random, std::max(emitter.SpawnRadius, 0.0f));
			const glm::vec3 way = RandomInCone(state.Random, direction, cone);
			const float speed = RandomBetween(state.Random, emitter.Speed);
			particle.Angle = NextRandom(state.Random) * glm::two_pi<float>();
			particle.Spin = glm::radians(emitter.Spin) * (NextRandom(state.Random) * 2.0f - 1.0f);
			particle.Axis = RandomInCone(state.Random, glm::vec3(0.0f, 1.0f, 0.0f), glm::pi<float>());
			if (state.Particles.size() >= maxParticles || worldBudget == 0)
			{
				++state.Refused;
				return;
			}
			if (late >= particle.Lifetime)
			{
				return;
			}
			particle.Position = glm::vec3(bornTo * glm::vec4(offset, 1.0f));
			particle.Velocity = glm::mat3(bornTo) * (way * speed);
			particle.Age = late;
			particle.Position += particle.Velocity * late;
			particle.Angle += particle.Spin * late;
			state.Particles.push_back(particle);
			--worldBudget;
		};

		if (emitter.bEmitting)
		{
			// By its rate, spread over the step
			state.Owed += std::max(emitter.Rate, 0.0f) * seconds;
			const uint32_t due = static_cast<uint32_t>(std::floor(state.Owed));
			state.Owed -= static_cast<float>(due);
			for (uint32_t index = 0; index < due; ++index)
			{
				bear(seconds * (static_cast<float>(index) + 0.5f) / static_cast<float>(due));
			}

			// Its bursts that came due in the step, each when it was due
			state.BurstsFired.resize(emitter.Bursts.size(), 0);
			const float end = state.Age + seconds;
			for (size_t burst = 0; burst < emitter.Bursts.size(); ++burst)
			{
				const ParticleBurst& settings = emitter.Bursts[burst];
				// A few at most a step, however short the interval
				for (int repeat = 0; repeat < 16; ++repeat)
				{
					uint32_t& fired = state.BurstsFired[burst];
					if (fired > 0 && settings.Interval <= 0.0f)
					{
						break;
					}
					const float at = settings.Time + static_cast<float>(fired) * std::max(settings.Interval, 0.0f);
					if (at >= end)
					{
						break;
					}
					++fired;
					const float late = std::clamp(end - at, 0.0f, seconds);
					for (int32_t index = 0; index < settings.Count; ++index)
					{
						bear(late);
					}
				}
			}
		}
		state.Age += seconds;
	}

	glm::vec4 GetParticleColor(std::span<const ParticleColorKey> keys, float life)
	{
		const auto [before, after] = KeysAround(keys, life);
		if (!before)
		{
			return glm::vec4(1.0f);
		}
		const float t = Between(*before, *after, life);
		return glm::mix(glm::vec4(before->Color, before->Alpha), glm::vec4(after->Color, after->Alpha), t);
	}

	float GetParticleSizeScale(std::span<const ParticleSizeKey> keys, float life)
	{
		const auto [before, after] = KeysAround(keys, life);
		if (!before)
		{
			return 1.0f;
		}
		return glm::mix(before->Size, after->Size, Between(*before, *after, life));
	}

	void UpdateParticles(const Registry& world, float time, std::optional<float> interpolation, const ParticleAir& air, uint32_t worldMax, RenderParticles& particles)
	{
		particles.Emitters.clear();
		particles.Instances.clear();
		particles.Instances.reserve(particles.Alive);

		// How far to move on: nothing at the first frame, or a paused world's; a jump back starts all
		float seconds = 0.0f;
		if (particles.LastTime && time < *particles.LastTime)
		{
			particles.States.clear();
		}
		else if (particles.LastTime)
		{
			seconds = std::min(time - *particles.LastTime, MaxParticleStep);
		}
		particles.LastTime = time;

		// Room for this many more in the world, after those alive at the last frame
		uint32_t budget = worldMax > particles.Alive ? worldMax - particles.Alive : 0;
		ParticleAir stepAir = air;
		stepAir.Time = time;

		std::unordered_set<uint64_t> seen;
		world.Each<ParticleEmitterComponent>([&](Entity entity, const ParticleEmitterComponent& emitter)
			{
				seen.insert(entity.Value);
				const glm::mat4 transform = GetDrawnTransform(world, entity, interpolation).ToMatrix();
				const auto [found, bNew] = particles.States.try_emplace(entity.Value);
				ParticleEmitterState& state = found->second;
				if (bNew || state.Seed != emitter.Seed)
				{
					ResetParticleEmitter(state, emitter.Seed);
				}
				StepParticleEmitter(state, emitter, transform, seconds, stepAir, budget);

				// As the GPU draws them
				RenderParticleEmitter render;
				render.SourceEntity = entity;
				render.Blend = emitter.Blend;
				render.Shape = emitter.Shape;
				render.MeshPath = emitter.Mesh.Path;
				render.bLit = emitter.bLit;
				render.SoftDistance = std::max(emitter.SoftDistance, 0.0f);
				render.FirstInstance = static_cast<uint32_t>(particles.Instances.size());
				render.WorldPosition = world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Position : glm::vec3(0.0f);
				const glm::mat4 space = emitter.bLocalSpace ? GetParticleSpaceToWorld(emitter, transform) : glm::mat4(1.0f);
				const glm::quat spaceTurn = glm::quat_cast(glm::mat3(space));
				const float intensity = std::max(emitter.EmissiveIntensity, 0.0f);
				glm::vec3 boundsMin(std::numeric_limits<float>::max());
				glm::vec3 boundsMax(std::numeric_limits<float>::lowest());
				for (const Particle& particle : state.Particles)
				{
					const float life = particle.Lifetime > 0.0f ? particle.Age / particle.Lifetime : 1.0f;
					ParticleInstanceData instance;
					instance.Size = particle.Size * GetParticleSizeScale(emitter.SizeOverLife, life);
					const glm::vec4 color = GetParticleColor(emitter.ColorOverLife, life);
					instance.Color = glm::vec4(std::max(color.r, 0.0f) * intensity, std::max(color.g, 0.0f) * intensity, std::max(color.b, 0.0f) * intensity, std::min(color.a, 1.0f));
					if (instance.Size <= 0.0f || instance.Color.a <= 0.0f)
					{
						continue;
					}
					instance.Position = emitter.bLocalSpace ? glm::vec3(space * glm::vec4(particle.Position, 1.0f)) : particle.Position;
					if (emitter.Shape == EParticleShape::Mesh)
					{
						const glm::quat turn = spaceTurn * glm::angleAxis(particle.Angle, particle.Axis);
						instance.Rotation = glm::vec4(turn.x, turn.y, turn.z, turn.w);
					}
					else
					{
						instance.Rotation = glm::vec4(particle.Angle, 0.0f, 0.0f, 0.0f);
					}
					// Round it, turned any way: a cube's half diagonal
					const float reach = instance.Size * 0.87f;
					for (int axis = 0; axis < 3; ++axis)
					{
						boundsMin[axis] = std::min(boundsMin[axis], instance.Position[axis] - reach);
						boundsMax[axis] = std::max(boundsMax[axis], instance.Position[axis] + reach);
					}
					particles.Instances.push_back(instance);
				}
				render.InstanceCount = static_cast<uint32_t>(particles.Instances.size()) - render.FirstInstance;
				render.Bounds = render.InstanceCount > 0 ? BoundingBox{ boundsMin, boundsMax } : BoundingBox{ render.WorldPosition, render.WorldPosition };
				particles.Emitters.push_back(render);
			});

		std::erase_if(particles.States, [&](const auto& entry)
			{
				return !seen.contains(entry.first);
			});
		particles.Alive = 0;
		for (const auto& [entity, state] : particles.States)
		{
			particles.Alive += static_cast<uint32_t>(state.Particles.size());
		}
	}

	void SortParticlesBackToFront(std::span<const ParticleInstanceData> instances, const glm::vec3& camera, std::vector<uint32_t>& outOrder)
	{
		// One number a particle: its distance squared above (a positive float's bits sort as the float
		// does), and below the index counted down, so that ties keep their order; largest first
		std::vector<uint64_t> keys(instances.size());
		for (size_t index = 0; index < instances.size(); ++index)
		{
			const glm::vec3& position = instances[index].Position;
			const float x = position.x - camera.x;
			const float y = position.y - camera.y;
			const float z = position.z - camera.z;
			const float distance = x * x + y * y + z * z;
			uint32_t bits = 0;
			std::memcpy(&bits, &distance, sizeof(bits));
			keys[index] = (static_cast<uint64_t>(bits) << 32) | (0xFFFFFFFFu - static_cast<uint32_t>(index));
		}
		std::sort(keys.data(), keys.data() + keys.size(), std::greater<uint64_t>());
		outOrder.resize(instances.size());
		for (size_t index = 0; index < keys.size(); ++index)
		{
			outOrder[index] = 0xFFFFFFFFu - static_cast<uint32_t>(keys[index]);
		}
	}
}
