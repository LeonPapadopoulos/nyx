#pragma once

#include <glm/glm.hpp>

#include <span>

namespace Nyx
{
	// The air particles are carried by (ParticleEmitterComponent's Drag pulls them towards its
	// speed), as a game knows it: its wind, the draught of a fire, the air a moving body pushes. A
	// game gives one to a world (IRenderer::SetWorldParticleVelocityField(), which Nyx's game does with
	// the one its game world was given, GameWorld::SetParticleVelocityField()); without one, the
	// world's wind (EnvironmentComponent's Wind) is the same everywhere. The renderer asks it once per emitter a
	// frame, for every particle of the emitter at once; it must outlive its world's use of it, and
	// may be asked during IRenderer's DrawFrame(), on the thread that calls it.
	class IParticleVelocityField
	{
	public:
		virtual ~IParticleVelocityField() = default;

		// The air's speed at each of `positions` (in the world, in metres), in metres a second, into
		// `outVelocities` (as many), at the world's `time` in seconds
		virtual void SampleVelocities(std::span<const glm::vec3> positions, float time, std::span<glm::vec3> outVelocities) const = 0;
	};
}
