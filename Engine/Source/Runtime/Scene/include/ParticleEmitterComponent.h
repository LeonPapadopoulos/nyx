#pragma once

#include "AssetReference.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace Nyx
{
	// How an emitter's particles are blended over the scene
	NYX_ENUM()
	enum class EParticleBlend : uint8_t
	{
		// Their light added: glows, sparks, fire; in any order
		Additive,
		// Covering what is behind by their alpha: smoke, dust; drawn back to front
		Alpha
	};

	// What each particle is drawn as
	NYX_ENUM()
	enum class EParticleShape : uint8_t
	{
		// A soft round quad facing the camera
		Billboard,
		// A small mesh, turned and spinning, e.g. embers; a convex one, such as the cube
		Mesh
	};

	// Many particles at once, at a moment of the emitter's life
	NYX_REFLECT()
	struct ParticleBurst
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.05, Tooltip = "When, in seconds after the emitter starts"))
		float Time = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Tooltip = "How many particles"))
		int32_t Count = 10;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.05, Tooltip = "Again this many seconds after, while the emitter emits; 0: once"))
		float Interval = 0.0f;
	};

	// A particle's colour and alpha at a part of its life
	NYX_REFLECT()
	struct ParticleColorKey
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "The part of its life, 0 at its birth to 1 at its end"))
		float Time = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (UI = Color))
		glm::vec3 Color{ 1.0f, 1.0f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "0 to 1: how much of it is there"))
		float Alpha = 1.0f;
	};

	// A particle's size at a part of its life, against its size at birth
	NYX_REFLECT()
	struct ParticleSizeKey
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "The part of its life, 0 at its birth to 1 at its end"))
		float Time = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (DragSpeed = 0.01, Tooltip = "Its size then, against its size at birth"))
		float Size = 1.0f;
	};

	// Particles given off from the entity's position, such as sparks, embers or smoke: each born
	// with a speed in a cone round a direction, pulled by gravity, carried by the air (the world's
	// wind, or a game's air: IRenderer::SetWorldParticleVelocityField()) and lifted like smoke, its
	// size and colour changing over its life. Only for the eye: the renderer moves them, as the
	// world's time goes on (standing still while a game is paused), and nothing in the world feels
	// them. The same seed gives the same particles.
	NYX_REFLECT(Component, meta = (DisplayName = "Particle Emitter"))
	struct ParticleEmitterComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Emission", DisplayName = "Emitting", Tooltip = "Whether new particles are born; those alive live on"))
		bool bEmitting = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Emission", DragSpeed = 0.5, Tooltip = "Particles born a second"))
		float Rate = 20.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Emission", Tooltip = "Many particles at once, at moments of the emitter's life"))
		std::vector<Nyx::ParticleBurst> Bursts;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Emission", DragSpeed = 0.05, Tooltip = "How long a particle lives, in seconds: from the first to the second, at random"))
		glm::vec2 Lifetime{ 1.5f, 2.5f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Emission", DisplayName = "Spawn Radius", DragSpeed = 0.01, Tooltip = "How far from the entity a particle may be born, in metres, anywhere in a ball"))
		float SpawnRadius = 0.1f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 0.01, Tooltip = "The way particles start out, in the entity's space"))
		glm::vec3 Direction{ 0.0f, 1.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DisplayName = "Cone Angle", DragSpeed = 0.5, Tooltip = "How far from the direction a particle may start out, in degrees, 0 to 180"))
		float ConeAngle = 20.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 0.05, Tooltip = "How fast a particle starts out, in metres a second: from the first to the second, at random"))
		glm::vec2 Speed{ 0.5f, 1.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 0.05, Tooltip = "How hard particles are pulled down, in metres a second squared: 9.81 as stones fall"))
		float Gravity = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 0.05, Tooltip = "How hard particles are lifted, in metres a second squared, as warm smoke rises"))
		float Buoyancy = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 0.05, Tooltip = "How fast particles take on the air's speed, a second: 0 the air doesn't touch them, 1 they lose about two thirds of the difference a second, 10 they move with it"))
		float Drag = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DragSpeed = 1.0, Tooltip = "How fast particles turn, at most, in degrees a second; each turns its own way"))
		float Spin = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motion", DisplayName = "Local Space", Tooltip = "Whether particles move with the entity, e.g. a torch's flame; else they are left where they were born, e.g. a trail of smoke"))
		bool bLocalSpace = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", Tooltip = "Soft round quads facing the camera, or small meshes"))
		EParticleShape Shape = EParticleShape::Billboard;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", AssetType = Mesh, Tooltip = "The mesh each particle is, for the Mesh shape: a small convex one; none, a cube"))
		Nyx::AssetReference Mesh;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DragSpeed = 0.01, Tooltip = "How big a particle is at birth, in metres across: from the first to the second, at random"))
		glm::vec2 Size{ 0.2f, 0.3f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Size Over Life", Tooltip = "Its size against its size at birth, over its life, between the keys; none: the same all its life"))
		std::vector<Nyx::ParticleSizeKey> SizeOverLife;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Color Over Life", Tooltip = "Its colour and alpha over its life, between the keys; none: white"))
		std::vector<Nyx::ParticleColorKey> ColorOverLife;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", Tooltip = "Additive adds their light (glows, sparks); Alpha covers what is behind (smoke)"))
		EParticleBlend Blend = EParticleBlend::Additive;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Emissive Intensity", DragSpeed = 0.05, Tooltip = "What the colour is multiplied by: above 1 it glows, and blooms"))
		float EmissiveIntensity = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Lit", Tooltip = "Whether the sun, the sky and the point lights light them, as smoke is; else they show their colour, as glows do"))
		bool bLit = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Look", DisplayName = "Soft Distance", DragSpeed = 0.01, Tooltip = "Over how many metres a particle fades where it meets what is behind it, so it shows no hard edge there; 0: a hard edge"))
		float SoftDistance = 0.3f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Budget", DisplayName = "Max Particles", Tooltip = "The most alive at once; no more are born until some die"))
		int32_t MaxParticles = 1000;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Budget", Tooltip = "Where its randomness starts: the same seed gives the same particles. Changed, the emitter starts again"))
		int32_t Seed = 1;
	};
}

#include "Generated/Runtime/ParticleEmitterComponent.reflect.h"
