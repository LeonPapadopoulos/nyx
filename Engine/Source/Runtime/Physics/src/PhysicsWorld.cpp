#include "NyxPCH.h"
#include "PhysicsWorld.h"

#include "Log.h"
#include "Profiler.h"

// Jolt.h first, as Jolt asks
#include <Jolt/Jolt.h>

#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <thread>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
#endif

namespace Nyx
{
	namespace
	{
		// Jolt's object layers: which bodies collide is decided by their motion
		namespace ObjectLayers
		{
			constexpr JPH::ObjectLayer Static = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Count = 2;
		}

		namespace BroadPhaseLayers
		{
			constexpr JPH::BroadPhaseLayer Static(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			constexpr uint32_t Count = 2;
		}

		// Static bodies collide with moving ones only; moving ones with everything
		bool LayersCollide(JPH::ObjectLayer a, JPH::ObjectLayer b)
		{
			return a == ObjectLayers::Moving || b == ObjectLayers::Moving;
		}

		class BroadPhaseLayerMap final : public JPH::BroadPhaseLayerInterface
		{
		public:
			JPH::uint GetNumBroadPhaseLayers() const override
			{
				return BroadPhaseLayers::Count;
			}

			JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
			{
				return layer == ObjectLayers::Static ? BroadPhaseLayers::Static : BroadPhaseLayers::Moving;
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				return layer == BroadPhaseLayers::Static ? "Static" : "Moving";
			}
#endif
		};

		class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				return layer == ObjectLayers::Moving || broadPhaseLayer == BroadPhaseLayers::Moving;
			}
		};

		class ObjectPairFilter final : public JPH::ObjectLayerPairFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override
			{
				return LayersCollide(a, b);
			}
		};

		void TraceToLog(const char* format, ...)
		{
			char text[1024];
			va_list arguments;
			va_start(arguments, format);
			std::vsnprintf(text, sizeof(text), format, arguments);
			va_end(arguments);
			LOG_INFO("Jolt: {0}", text);
		}

#ifdef JPH_ENABLE_ASSERTS
		// A failed Jolt assert is logged; under a debugger it also breaks, as Nyx's ASSERT does
		bool AssertToLog(const char* expression, const char* message, const char* file, JPH::uint line)
		{
			LOG_ERROR("Jolt assert failed: {0} {1} ({2}:{3})", expression, message ? message : "", file, line);
	#if defined(_WIN32)
			return ::IsDebuggerPresent() != FALSE;
	#else
			return false;
	#endif
		}
#endif

		// Jolt's process-wide setup, once: its allocator, its logging, its shape types
		void InitializeJoltOnce()
		{
			static std::once_flag once;
			std::call_once(once, []
				{
					JPH::RegisterDefaultAllocator();
					JPH::Trace = TraceToLog;
					JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = AssertToLog;)
					JPH::Factory::sInstance = new JPH::Factory();
					JPH::RegisterTypes();
				});
		}

		JPH::Vec3 ToJolt(const glm::vec3& v)
		{
			return JPH::Vec3(v.x, v.y, v.z);
		}

		JPH::Quat ToJolt(const glm::quat& q)
		{
			return JPH::Quat(q.x, q.y, q.z, q.w).Normalized();
		}

		glm::vec3 ToGlm(JPH::Vec3Arg v)
		{
			return glm::vec3(v.GetX(), v.GetY(), v.GetZ());
		}

		glm::quat ToGlm(JPH::QuatArg q)
		{
			return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ());
		}

		JPH::BodyID ToJolt(BodyHandle body)
		{
			return JPH::BodyID(body.Value);
		}

		JPH::EMotionType ToJolt(EBodyMotion motion)
		{
			switch (motion)
			{
			case EBodyMotion::Static:    return JPH::EMotionType::Static;
			case EBodyMotion::Kinematic: return JPH::EMotionType::Kinematic;
			default:                     return JPH::EMotionType::Dynamic;
			}
		}

		JPH::ShapeRefC MakeShape(const BodyShape& shape)
		{
			JPH::ShapeSettings::ShapeResult result;
			switch (shape.Kind)
			{
			case EShapeKind::Sphere:
			{
				result = JPH::SphereShapeSettings(std::max(shape.Radius, 0.001f)).Create();
				break;
			}
			case EShapeKind::Capsule:
			{
				result = JPH::CapsuleShapeSettings(std::max(shape.HalfHeight, 0.001f), std::max(shape.Radius, 0.001f)).Create();
				break;
			}
			default:
			{
				// Jolt rounds a box's edges by a convex radius, which no half extent may be below
				const glm::vec3 halfExtents = glm::max(shape.HalfExtents, glm::vec3(0.001f));
				const float convexRadius = std::min(JPH::cDefaultConvexRadius, std::min({ halfExtents.x, halfExtents.y, halfExtents.z }) * 0.5f);
				result = JPH::BoxShapeSettings(ToJolt(halfExtents), convexRadius).Create();
				break;
			}
			}

			if (result.HasError())
			{
				LOG_ERROR("Physics: can't make a body's shape: {0}", result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}

		uint32_t DefaultWorkerThreads()
		{
			const uint32_t cores = std::max(std::thread::hardware_concurrency(), 2u);
			return std::min(cores - 1, 8u);
		}
	}

	struct PhysicsWorld::Implementation
	{
		explicit Implementation(const PhysicsSettings& settings)
			: Jobs(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers,
				  static_cast<int>(settings.WorkerThreads != 0 ? settings.WorkerThreads : DefaultWorkerThreads()))
		{
			System.Init(settings.MaxBodies, 0, settings.MaxBodyPairs, settings.MaxContactConstraints, BroadPhaseMap,
				BroadPhaseFilter, PairFilter);
			System.SetGravity(ToJolt(settings.Gravity));
		}

		JPH::BodyInterface& Bodies()
		{
			return System.GetBodyInterface();
		}

		const JPH::BodyInterface& Bodies() const
		{
			return System.GetBodyInterface();
		}

		// Scratch memory for a step
		JPH::TempAllocatorImpl Temp{ 16 * 1024 * 1024 };
		JPH::JobSystemThreadPool Jobs;

		// Jolt keeps references to these; they are made before the system and go after it
		BroadPhaseLayerMap BroadPhaseMap;
		ObjectVsBroadPhaseFilter BroadPhaseFilter;
		ObjectPairFilter PairFilter;

		JPH::PhysicsSystem System;
		// Bodies added since the broad phase was last optimized, a cost worth paying once after a
		// batch of new bodies, before the next step
		bool bBroadPhaseStale = false;
	};

	PhysicsWorld::PhysicsWorld(const PhysicsSettings& settings)
	{
		InitializeJoltOnce();
		Impl = std::make_unique<Implementation>(settings);
	}

	PhysicsWorld::~PhysicsWorld() = default;

	BodyHandle PhysicsWorld::CreateBody(const BodyDesc& desc)
	{
		const JPH::ShapeRefC shape = MakeShape(desc.Shape);
		if (!shape)
		{
			return {};
		}

		const bool bStatic = desc.Motion == EBodyMotion::Static;
		JPH::BodyCreationSettings settings(shape, JPH::RVec3(ToJolt(desc.Position)), ToJolt(desc.Rotation), ToJolt(desc.Motion),
			bStatic ? ObjectLayers::Static : ObjectLayers::Moving);
		settings.mFriction = desc.Friction;
		settings.mRestitution = desc.Restitution;
		settings.mLinearDamping = desc.LinearDamping;
		settings.mAngularDamping = desc.AngularDamping;
		settings.mGravityFactor = desc.GravityFactor;
		settings.mUserData = desc.UserData;
		if (desc.Mass > 0.0f && desc.Motion == EBodyMotion::Dynamic)
		{
			settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
			settings.mMassPropertiesOverride.mMass = desc.Mass;
		}

		const JPH::BodyID id =
			Impl->Bodies().CreateAndAddBody(settings, bStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		if (id.IsInvalid())
		{
			LOG_ERROR("Physics: no room for another body ({0} at most)", Impl->System.GetMaxBodies());
			return {};
		}

		Impl->bBroadPhaseStale = true;
		return BodyHandle{ id.GetIndexAndSequenceNumber() };
	}

	void PhysicsWorld::DestroyBody(BodyHandle body)
	{
		if (!body.IsValid())
		{
			return;
		}
		Impl->Bodies().RemoveBody(ToJolt(body));
		Impl->Bodies().DestroyBody(ToJolt(body));
	}

	size_t PhysicsWorld::GetBodyCount() const
	{
		return Impl->System.GetNumBodies();
	}

	void PhysicsWorld::Step(float seconds)
	{
		NYX_PROFILE_SCOPE("Physics step");
		if (seconds <= 0.0f)
		{
			return;
		}

		if (Impl->bBroadPhaseStale)
		{
			Impl->System.OptimizeBroadPhase();
			Impl->bBroadPhaseStale = false;
		}

		const JPH::EPhysicsUpdateError error = Impl->System.Update(seconds, 1, &Impl->Temp, &Impl->Jobs);
		if (error != JPH::EPhysicsUpdateError::None)
		{
			LOG_WARNING("Physics: the step ran out of room ({0}); raise PhysicsSettings' limits", static_cast<uint32_t>(error));
		}
	}

	BodyPose PhysicsWorld::GetPose(BodyHandle body) const
	{
		JPH::RVec3 position;
		JPH::Quat rotation;
		Impl->Bodies().GetPositionAndRotation(ToJolt(body), position, rotation);
		return BodyPose{ ToGlm(JPH::Vec3(position)), ToGlm(rotation) };
	}

	void PhysicsWorld::Teleport(BodyHandle body, const BodyPose& pose)
	{
		JPH::BodyInterface& bodies = Impl->Bodies();
		const JPH::BodyID id = ToJolt(body);
		const bool bStatic = bodies.GetMotionType(id) == JPH::EMotionType::Static;
		bodies.SetPositionAndRotation(id, JPH::RVec3(ToJolt(pose.Position)), ToJolt(pose.Rotation),
			bStatic ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
		if (!bStatic)
		{
			bodies.SetLinearAndAngularVelocity(id, JPH::Vec3::sZero(), JPH::Vec3::sZero());
		}
	}

	void PhysicsWorld::MoveKinematic(BodyHandle body, const BodyPose& target, float seconds)
	{
		Impl->Bodies().MoveKinematic(ToJolt(body), JPH::RVec3(ToJolt(target.Position)), ToJolt(target.Rotation), seconds);
	}

	glm::vec3 PhysicsWorld::GetLinearVelocity(BodyHandle body) const
	{
		return ToGlm(Impl->Bodies().GetLinearVelocity(ToJolt(body)));
	}

	glm::vec3 PhysicsWorld::GetAngularVelocity(BodyHandle body) const
	{
		return ToGlm(Impl->Bodies().GetAngularVelocity(ToJolt(body)));
	}

	void PhysicsWorld::SetLinearVelocity(BodyHandle body, const glm::vec3& velocity)
	{
		Impl->Bodies().SetLinearVelocity(ToJolt(body), ToJolt(velocity));
	}

	void PhysicsWorld::AddForce(BodyHandle body, const glm::vec3& force)
	{
		Impl->Bodies().AddForce(ToJolt(body), ToJolt(force));
	}

	void PhysicsWorld::AddForceAtPoint(BodyHandle body, const glm::vec3& force, const glm::vec3& worldPoint)
	{
		Impl->Bodies().AddForce(ToJolt(body), ToJolt(force), JPH::RVec3(ToJolt(worldPoint)));
	}

	void PhysicsWorld::AddTorque(BodyHandle body, const glm::vec3& torque)
	{
		Impl->Bodies().AddTorque(ToJolt(body), ToJolt(torque));
	}

	void PhysicsWorld::AddImpulse(BodyHandle body, const glm::vec3& impulse)
	{
		Impl->Bodies().AddImpulse(ToJolt(body), ToJolt(impulse));
	}

	float PhysicsWorld::GetMass(BodyHandle body) const
	{
		const JPH::BodyLockRead lock(Impl->System.GetBodyLockInterface(), ToJolt(body));
		if (!lock.Succeeded() || lock.GetBody().IsStatic())
		{
			return 0.0f;
		}
		const float inverseMass = lock.GetBody().GetMotionProperties()->GetInverseMass();
		return inverseMass > 0.0f ? 1.0f / inverseMass : 0.0f;
	}

	glm::vec3 PhysicsWorld::GetCenterOfMass(BodyHandle body) const
	{
		return ToGlm(JPH::Vec3(Impl->Bodies().GetCenterOfMassPosition(ToJolt(body))));
	}

	bool PhysicsWorld::IsAwake(BodyHandle body) const
	{
		return Impl->Bodies().IsActive(ToJolt(body));
	}

	uint64_t PhysicsWorld::GetUserData(BodyHandle body) const
	{
		return Impl->Bodies().GetUserData(ToJolt(body));
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		Impl->System.SetGravity(ToJolt(gravity));
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return ToGlm(Impl->System.GetGravity());
	}
}
