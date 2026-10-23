#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace Nyx
{
	class DebugDraw;

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
		// Detects the moving bodies that enter and leave it (contact events) but pushes nothing, e.g. a
		// trigger volume
		bool bSensor = false;
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

	// Which bodies a query sees
	struct QueryFilter
	{
		bool bStatic = true;
		// Kinematic and dynamic bodies
		bool bMoving = true;
		bool bSensors = false;
		// E.g. the body that casts, so it doesn't find itself
		BodyHandle Ignore;
	};

	// What a ray or a shape cast hit first
	struct QueryHit
	{
		BodyHandle Body;
		// Ray: where it enters the body; shape cast: where the shape first touches it
		glm::vec3 Point{ 0.0f };
		// The hit body's surface normal there, pointing back at the caster
		glm::vec3 Normal{ 0.0f };
		// How far the ray or the shape went before it hit
		float Distance = 0.0f;
		uint64_t UserData = 0;
	};

	enum class EContactEvent : uint8_t
	{
		Began,
		Ended
	};

	// Two bodies that started or stopped touching during a step; a sensor and a body that entered or
	// left it alike
	struct ContactEvent
	{
		EContactEvent Kind = EContactEvent::Began;
		// BodyA has the lower handle. An ended contact can name a body destroyed since.
		BodyHandle BodyA;
		BodyHandle BodyB;
		// Began only: where they touch, in world space, the normal pointing from A to B, and how fast
		// they were coming together along it (m/s)
		glm::vec3 Point{ 0.0f };
		glm::vec3 Normal{ 0.0f };
		float ApproachSpeed = 0.0f;
	};

	enum class EConstraintKind : uint8_t
	{
		// Holds the bodies where they are to each other
		Fixed,
		// Joins them at a point, around which they turn freely: a ball joint
		Point,
		// Joins them at a point, turning about one axis only: a door
		Hinge,
		// Lets them move along one axis only, without turning: a drawer
		Slider,
		// Keeps a point on each between a least and a greatest distance apart: a rope or a rod
		Distance
	};

	// A joint between two bodies, or a body and the world. Everything in world space, as the bodies
	// are now; that is the joint's rest.
	struct ConstraintDesc
	{
		EConstraintKind Kind = EConstraintKind::Fixed;
		BodyHandle Body;
		// Invalid: the world
		BodyHandle Other;
		// Point, hinge: the pivot; distance: the end on Body
		glm::vec3 Point{ 0.0f };
		// Distance: the end on Other, or in the world
		glm::vec3 OtherPoint{ 0.0f };
		// Hinge: what it turns about; slider: what it moves along
		glm::vec3 Axis{ 0.0f, 1.0f, 0.0f };
		// Hinge and slider: limits away from the rest, in radians (Min in [-pi, 0], Max in [0, pi])
		// or metres (Min <= 0 <= Max)
		bool bLimits = false;
		float LimitMin = 0.0f;
		float LimitMax = 0.0f;
		// Distance
		float MinDistance = 0.0f;
		float MaxDistance = 1.0f;
	};

	// A constraint of a PhysicsWorld; like a BodyHandle, valid only there and until destroyed
	struct ConstraintHandle
	{
		static constexpr uint32_t InvalidValue = 0xffffffffu;
		uint32_t Value = InvalidValue;

		bool IsValid() const { return Value != InvalidValue; }
		bool operator==(const ConstraintHandle&) const = default;
	};

	// What a character is made with: an upright capsule standing on its position, which walks
	// through the world by MoveCharacter() instead of being moved by the step
	struct CharacterDesc
	{
		float Radius = 0.3f;
		// From its feet to the top of its head, the round ends included; at least twice the radius
		float Height = 1.8f;
		// Where its feet are
		glm::vec3 Position{ 0.0f };
		// Ground steeper than this, in radians, it can't walk up, and slides down
		float MaxSlope = 0.7853982f;
		// In kg: how much it weighs on what it stands on
		float Mass = 70.0f;
		// In newtons: how hard it can push the dynamic bodies it walks into
		float MaxPushForce = 100.0f;
		// The owner's own number for the character, e.g. its entity; its body has it too
		uint64_t UserData = 0;
	};

	// A character of a PhysicsWorld; like a BodyHandle, valid only there and until destroyed
	struct CharacterHandle
	{
		static constexpr uint32_t InvalidValue = 0xffffffffu;
		uint32_t Value = InvalidValue;

		bool IsValid() const { return Value != InvalidValue; }
		bool operator==(const CharacterHandle&) const = default;
	};

	enum class ECharacterGround : uint8_t
	{
		// Stands on ground it can walk on
		OnGround,
		// Stands on ground too steep for it, and slides down
		OnSteepGround,
		// Nothing holds it up: it falls
		InAir
	};

	// What a character tries to do in one move (PhysicsWorld::MoveCharacter())
	struct CharacterMove
	{
		// Where it wants to walk, in m/s; only the part along the ground counts
		glm::vec3 WalkVelocity{ 0.0f };
		// How fast it gets to that velocity, in m/s per second: on walkable ground, and otherwise
		float GroundAcceleration = 30.0f;
		float AirAcceleration = 10.0f;
		// Above 0: jumps at this speed (m/s), if it stands on walkable ground
		float JumpSpeed = 0.0f;
		// 1: falls normally, 0: floats
		float GravityFactor = 1.0f;
		// Climbs steps up to this high, and follows the ground down by up to GroundSnapDistance
		// instead of leaving it, e.g. going down stairs
		float StepHeight = 0.3f;
		float GroundSnapDistance = 0.5f;
	};

	// A character as its last move left it
	struct CharacterState
	{
		// Where its feet are
		glm::vec3 Position{ 0.0f };
		glm::vec3 Velocity{ 0.0f };
		ECharacterGround Ground = ECharacterGround::InAir;
		// What it stands on faces this way; up in the air
		glm::vec3 GroundNormal{ 0.0f, 1.0f, 0.0f };
	};

	// What PhysicsWorld::DrawDebug() draws, and in which colours (linear RGB)
	struct PhysicsDebugOptions
	{
		// Every body's shape: a box's edges, circles around a sphere and along a capsule, in the
		// body's axes and coloured by how it moves; every character's capsule, in its own colour
		bool bShapes = true;
		// The last step's contact points, each a cross with an arrow along its normal; only kept
		// while asked for (PhysicsWorld::SetKeepContactPoints())
		bool bContacts = true;
		// Each constraint: a line between its anchor points on its two bodies, a cross at each
		bool bConstraints = true;

		glm::vec3 StaticColor{ 0.6f, 0.6f, 0.6f };
		glm::vec3 KinematicColor{ 0.3f, 0.6f, 1.0f };
		glm::vec3 AwakeColor{ 0.2f, 1.0f, 0.3f };
		glm::vec3 AsleepColor{ 0.15f, 0.45f, 0.2f };
		glm::vec3 SensorColor{ 1.0f, 0.85f, 0.1f };
		glm::vec3 ContactColor{ 1.0f, 0.25f, 0.2f };
		glm::vec3 ConstraintColor{ 1.0f, 0.4f, 1.0f };
		glm::vec3 CharacterColor{ 1.0f, 0.55f, 0.0f };
	};

	struct PhysicsSettings
	{
		glm::vec3 Gravity{ 0.0f, -9.81f, 0.0f };
		uint32_t MaxBodies = 16384;
		uint32_t MaxBodyPairs = 16384;
		uint32_t MaxContactConstraints = 8192;
		// Threads that run the step besides the caller's; 0: one per core but one, at most 8
		uint32_t WorkerThreads = 0;
		// Contact events kept until taken; later ones are dropped, so a world nobody listens to doesn't grow
		uint32_t MaxContactEvents = 8192;
	};

	// Rigid bodies and their collisions, on Jolt: made, moved, pushed, joined, queried and stepped.
	// Bodies collide by their motion: static with moving, moving with everything; sensors see moving
	// bodies only. Characters walk among them (Jolt's CharacterVirtual), each with a kinematic body
	// inside, so the others see it. The caller owns the world and calls everything from one thread;
	// Jolt's own threads run inside Step() only. Same bodies, made in the same order and stepped the
	// same way, give the same results and the same contact events.
	class PhysicsWorld
	{
	public:
		explicit PhysicsWorld(const PhysicsSettings& settings = {});
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		// Invalid when the world is full (MaxBodies) or the shape can't be made, which is logged
		BodyHandle CreateBody(const BodyDesc& desc);
		// Its constraints go with it
		void DestroyBody(BodyHandle body);
		size_t GetBodyCount() const;

		// Moves every body `seconds` on: forces added since the last step act during it, then go
		void Step(float seconds);

		// The contacts that began and ended in the steps since the last call, step by step, each
		// step's sorted by their bodies
		std::vector<ContactEvent> TakeContactEvents();

		// The first body along the ray from `origin` (direction needn't be unit length); a ray that
		// starts inside a body hits it at once
		std::optional<QueryHit> RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
			const QueryFilter& filter = {}) const;
		// The first body a shape meets, moved from `pose` along `direction` at most `maxDistance`
		std::optional<QueryHit> ShapeCast(const BodyShape& shape, const BodyPose& pose, const glm::vec3& direction,
			float maxDistance, const QueryFilter& filter = {}) const;
		// The bodies a shape at `pose` overlaps, in handle order
		std::vector<BodyHandle> Overlap(const BodyShape& shape, const BodyPose& pose, const QueryFilter& filter = {}) const;

		// Invalid when a body is invalid, or Body is Other, which is logged
		ConstraintHandle CreateConstraint(const ConstraintDesc& desc);
		void DestroyConstraint(ConstraintHandle constraint);
		size_t GetConstraintCount() const;

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

		// Invalid when its shape can't be made or the world is full, which is logged. Its body
		// (GetCharacterBody()), a little smaller than the capsule, is what rays, shape casts and
		// dynamic bodies meet.
		CharacterHandle CreateCharacter(const CharacterDesc& desc);
		void DestroyCharacter(CharacterHandle character);
		size_t GetCharacterCount() const;

		// Walks the character for `seconds`, at once, not in Step(): its velocity turns towards the
		// walk, gravity pulls it, it jumps if asked and able, then it moves, sliding along what it
		// meets, climbing steps, sliding down ground too steep, following the ground down and pushing
		// dynamic bodies. Called before Step(), so the bodies meet it where it is now, as Jolt advises.
		void MoveCharacter(CharacterHandle character, const CharacterMove& move, float seconds);
		CharacterState GetCharacterState(CharacterHandle character) const;
		// Puts it there at once (its feet), without moving it through what lies between, and stops it
		void TeleportCharacter(CharacterHandle character, const glm::vec3& position);
		// The kinematic body inside it, e.g. for QueryFilter::Ignore; it has the character's user data
		BodyHandle GetCharacterBody(CharacterHandle character) const;

		void SetGravity(const glm::vec3& gravity);
		glm::vec3 GetGravity() const;

		// Draws the world as lines, as the options say, to see its shapes, contacts and joints:
		// where the last step left them. Nothing in builds without debug drawing (DebugDraw::Enabled).
		void DrawDebug(DebugDraw& drawing, const PhysicsDebugOptions& options) const;
		// Whether the steps keep their contact points for DrawDebug(), from the next step on. Off by
		// default: keeping them costs a little for every pair of touching bodies.
		void SetKeepContactPoints(bool bKeep);

	private:
		struct Implementation;
		std::unique_ptr<Implementation> Impl;
	};
}
