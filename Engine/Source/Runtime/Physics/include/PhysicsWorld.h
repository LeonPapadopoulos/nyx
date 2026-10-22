#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>

namespace Nyx
{
	// How a body moves: not at all, only where it is put (MoveKinematic(); pushes dynamic bodies, is
	// pushed by nothing), or by forces, gravity and collisions
	enum class EBodyMotion : uint8_t
	{
		Static,
		Kinematic,
		Dynamic
	};

	enum class EShapeKind : uint8_t
	{
		Box,
		Sphere,
		// Along the body's Y axis
		Capsule
	};

	// A body's collision shape, around the body's position
	struct BodyShape
	{
		EShapeKind Kind = EShapeKind::Box;
		// Box: half its size along each axis
		glm::vec3 HalfExtents{ 0.5f };
		// Sphere and capsule
		float Radius = 0.5f;
		// Capsule: half the length of its middle, without the round ends
		float HalfHeight = 0.5f;
	};

	// What a body is made with
	struct BodyDesc
	{
		BodyShape Shape;
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		EBodyMotion Motion = EBodyMotion::Dynamic;
		// In kg; 0: from the shape's volume at the density of water
		float Mass = 0.0f;
		float Friction = 0.5f;
		// 0: no bounce, 1: bounces back as fast as it came
		float Restitution = 0.0f;
		// Velocity lost per second, roughly
		float LinearDamping = 0.05f;
		float AngularDamping = 0.05f;
		// 1: falls normally, 0: floats
		float GravityFactor = 1.0f;
		// The owner's own number for the body, e.g. its entity's guid
		uint64_t UserData = 0;
	};

	// A body of a PhysicsWorld. Valid only in the world that made it, and only until destroyed; never
	// saved or sent anywhere (an entity's guid names its body there).
	struct BodyHandle
	{
		static constexpr uint32_t InvalidValue = 0xffffffffu;
		uint32_t Value = InvalidValue;

		bool IsValid() const { return Value != InvalidValue; }
		bool operator==(const BodyHandle&) const = default;
	};

	// Where a body is: the position and rotation of its shape's origin
	struct BodyPose
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};

	struct PhysicsSettings
	{
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		uint32_t MaxBodies = 16384;
		uint32_t MaxBodyPairs = 16384;
		uint32_t MaxContactConstraints = 8192;
		// Threads that run the step besides the caller's; 0: one per core but one, at most 8
		uint32_t WorkerThreads = 0;
	};

	// Rigid bodies and their collisions, on Jolt: made, moved, pushed and stepped. Bodies collide by
	// their motion: static with moving, moving with everything. The caller owns the world and calls
	// everything from one thread; Jolt's own threads run inside Step() only. Same bodies, made in
	// the same order and stepped the same way, give the same results.
	class PhysicsWorld
	{
	public:
		explicit PhysicsWorld(const PhysicsSettings& settings = {});
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Invalid when the world is full (MaxBodies) or the shape can't be made, which is logged
		BodyHandle CreateBody(const BodyDesc& desc);
		void DestroyBody(BodyHandle body);
		size_t GetBodyCount() const;

		// Moves every body `seconds` on: forces added since the last step act during it, then go
		void Step(float seconds);

		BodyPose GetPose(BodyHandle body) const;
		// Puts the body there at once, without moving it through what lies between, and stops it
		void Teleport(BodyHandle body, const BodyPose& pose);
		// A kinematic body: moves it to `target` over the next step of `seconds`, pushing what it meets
		void MoveKinematic(BodyHandle body, const BodyPose& target, float seconds);

		glm::vec3 GetLinearVelocity(BodyHandle body) const;
		glm::vec3 GetAngularVelocity(BodyHandle body) const;
		void SetLinearVelocity(BodyHandle body, const glm::vec3& velocity);

		// For the next step, in newtons, at the centre of mass or at a point in world space: a
		// point off the centre of mass turns the body too
		void AddForce(BodyHandle body, const glm::vec3& force);
		void AddForceAtPoint(BodyHandle body, const glm::vec3& force, const glm::vec3& worldPoint);
		void AddTorque(BodyHandle body, const glm::vec3& torque);
		// A push at once, in newton seconds
		void AddImpulse(BodyHandle body, const glm::vec3& impulse);

		float GetMass(BodyHandle body) const;
		glm::vec3 GetCenterOfMass(BodyHandle body) const;
		// Resting bodies sleep and cost nothing until something touches or moves them
		bool IsAwake(BodyHandle body) const;
		uint64_t GetUserData(BodyHandle body) const;

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

	private:
		struct Implementation;
		std::unique_ptr<Implementation> Impl;
	};
}
