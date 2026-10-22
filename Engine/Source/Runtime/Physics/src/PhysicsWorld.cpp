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
#include <Jolt/Physics/Body/BodyLockMulti.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Constraints/DistanceConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>
#include <Jolt/Physics/Constraints/PointConstraint.h>
#include <Jolt/Physics/Constraints/SliderConstraint.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/RegisterTypes.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <thread>
#include <tuple>
#include <utility>

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
		// Jolt's object layers: which bodies collide is decided by their motion, and sensors have
		// their own. Each object layer has a broad phase layer of the same number.
		namespace ObjectLayers
		{
			constexpr JPH::ObjectLayer Static = 0;
			constexpr JPH::ObjectLayer Moving = 1;
			constexpr JPH::ObjectLayer Sensor = 2;
			constexpr JPH::ObjectLayer Count = 3;
		}

		namespace BroadPhaseLayers
		{
			constexpr JPH::BroadPhaseLayer Static(0);
			constexpr JPH::BroadPhaseLayer Moving(1);
			// Apart, so the step's search for pairs of moving and static bodies never walks them
			constexpr JPH::BroadPhaseLayer Sensor(2);
			constexpr uint32_t Count = 3;
		}

		// Moving bodies collide with everything, static bodies and sensors with moving ones only
		bool LayersCollide(JPH::ObjectLayer a, JPH::ObjectLayer b)
		{
			return a == ObjectLayers::Moving || b == ObjectLayers::Moving;
		}

		JPH::ObjectLayer LayerOf(const BodyDesc& desc)
		{
			if (desc.bSensor)
			{
				return ObjectLayers::Sensor;
			}
			return desc.Motion == EBodyMotion::Static ? ObjectLayers::Static : ObjectLayers::Moving;
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
				return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
			}

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
			const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
			{
				static constexpr const char* Names[] = { "Static", "Moving", "Sensor" };
				return Names[layer.GetValue()];
			}
#endif
		};

		class ObjectVsBroadPhaseFilter final : public JPH::ObjectVsBroadPhaseLayerFilter
		{
		public:
			bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer broadPhaseLayer) const override
			{
				return LayersCollide(layer, static_cast<JPH::ObjectLayer>(broadPhaseLayer.GetValue()));
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

		BodyHandle ToHandle(const JPH::BodyID& id)
		{
			return BodyHandle{ id.GetIndexAndSequenceNumber() };
		}

		// The two bodies of a contact, the lower handle first
		using BodyPair = std::pair<uint32_t, uint32_t>;

		BodyPair PairOf(const ContactEvent& event)
		{
			return { event.BodyA.Value, event.BodyB.Value };
		}

		// Jolt calls this on its job threads during the step, for many pairs at once: it only
		// gathers the events, which Step() sorts once Jolt is done. Jolt gives body 1 the lower id.
		class ContactCollector final : public JPH::ContactListener
		{
		public:
			void OnContactAdded(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
			{
				// The middle of the contact points, halfway between both surfaces
				const JPH::uint count = manifold.mRelativeContactPointsOn1.size();
				JPH::Vec3 point = JPH::Vec3::sZero();
				for (JPH::uint i = 0; i < count; ++i)
				{
					point += 0.5f * (manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i]);
				}
				const JPH::RVec3 worldPoint = manifold.mBaseOffset + point / float(std::max<JPH::uint>(count, 1));

				// Jolt's normal points from body 1 to body 2: they close in when 2 moves against it
				const JPH::Vec3 relativeVelocity = body2.GetPointVelocity(worldPoint) - body1.GetPointVelocity(worldPoint);

				ContactEvent event;
				event.Kind = EContactEvent::Began;
				event.BodyA = ToHandle(body1.GetID());
				event.BodyB = ToHandle(body2.GetID());
				event.Point = ToGlm(JPH::Vec3(worldPoint));
				event.Normal = ToGlm(manifold.mWorldSpaceNormal);
				event.ApproachSpeed = std::max(0.0f, -relativeVelocity.Dot(manifold.mWorldSpaceNormal));
				Add(event);
			}

			// The bodies can't be read here: Jolt has them locked, and either may be gone already
			void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
			{
				ContactEvent event;
				event.Kind = EContactEvent::Ended;
				event.BodyA = ToHandle(pair.GetBody1ID());
				event.BodyB = ToHandle(pair.GetBody2ID());
				Add(event);
			}

			void Add(const ContactEvent& event)
			{
				const std::scoped_lock lock(Mutex);
				Pending.push_back(event);
			}

			std::vector<ContactEvent> Take()
			{
				std::vector<ContactEvent> events;
				const std::scoped_lock lock(Mutex);
				events.swap(Pending);
				return events;
			}

		private:
			std::mutex Mutex;
			std::vector<ContactEvent> Pending;
		};

		// An order that doesn't depend on which thread found which pair
		void SortContactEvents(std::vector<ContactEvent>& events)
		{
			std::sort(events.begin(), events.end(), [](const ContactEvent& a, const ContactEvent& b)
				{
					return std::tie(a.BodyA.Value, a.BodyB.Value, a.Kind) < std::tie(b.BodyA.Value, b.BodyB.Value, b.Kind);
				});
		}

		// Leaves out what a QueryFilter doesn't want: whole layers before the narrow phase, then
		// single bodies
		class QueryLayerFilter final : public JPH::ObjectLayerFilter
		{
		public:
			explicit QueryLayerFilter(const QueryFilter& filter)
				: Filter(filter)
			{
			}

			bool ShouldCollide(JPH::ObjectLayer layer) const override
			{
				switch (layer)
				{
				case ObjectLayers::Static: return Filter.bStatic;
				case ObjectLayers::Moving: return Filter.bMoving;
				default:                   return Filter.bSensors;
				}
			}

		private:
			const QueryFilter& Filter;
		};

		class QueryBodyFilter final : public JPH::BodyFilter
		{
		public:
			explicit QueryBodyFilter(const QueryFilter& filter)
				: Filter(filter)
			{
			}

			bool ShouldCollide(const JPH::BodyID& id) const override
			{
				return id.GetIndexAndSequenceNumber() != Filter.Ignore.Value;
			}

		private:
			const QueryFilter& Filter;
		};

		// Our shapes are symmetric about their origin, where their centre of mass is too
		JPH::RMat44 ToJolt(const BodyPose& pose)
		{
			return JPH::RMat44::sRotationTranslation(ToJolt(pose.Rotation), JPH::RVec3(ToJolt(pose.Position)));
		}

		glm::vec3 NormalizedOr(const glm::vec3& v, const glm::vec3& fallback)
		{
			const float length = glm::length(v);
			return length > 1e-6f ? v / length : fallback;
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
			System.SetContactListener(&Contacts);
			MaxContactEvents = settings.MaxContactEvents;
		}

		JPH::BodyInterface& Bodies()
		{
			return System.GetBodyInterface();
		}

		const JPH::BodyInterface& Bodies() const
		{
			return System.GetBodyInterface();
		}

		// Still there, and static or asleep
		bool IsAtRest(BodyHandle body) const
		{
			return Bodies().IsAdded(ToJolt(body)) && !Bodies().IsActive(ToJolt(body));
		}

		void ActivateIfMoving(JPH::Body* body)
		{
			if (body != nullptr && !body->IsStatic())
			{
				Bodies().ActivateBody(body->GetID());
			}
		}

		using ConstraintMap = std::map<uint32_t, JPH::Ref<JPH::TwoBodyConstraint>>;

		ConstraintMap::iterator RemoveConstraint(ConstraintMap::iterator it)
		{
			const JPH::Ref<JPH::TwoBodyConstraint> constraint = it->second;
			System.RemoveConstraint(constraint);
			// What hung from it falls now, even if it was asleep
			ActivateIfMoving(constraint->GetBody1());
			ActivateIfMoving(constraint->GetBody2());
			return Constraints.erase(it);
		}

		// Jolt ends the contacts of bodies falling asleep, as it stops looking at them, and begins
		// them again when they wake. A box resting in a sensor would leave and enter it, so such a
		// contact is kept open here instead, until it is found again or its bodies wake apart.
		void FilterSleepingContacts(std::vector<ContactEvent>& events)
		{
			std::vector<ContactEvent> kept;
			kept.reserve(events.size());
			for (const ContactEvent& event : events)
			{
				const BodyPair pair = PairOf(event);
				if (event.Kind == EContactEvent::Began)
				{
					if (SleepingContacts.erase(pair) == 0)
					{
						kept.push_back(event);
					}
				}
				else if (IsAtRest(event.BodyA) && IsAtRest(event.BodyB))
				{
					SleepingContacts[pair] = false;
				}
				else
				{
					kept.push_back(event);
				}
			}

			// A body woken during a step is looked at only from the next one on: a sleeping contact
			// not found again by then has ended
			for (auto it = SleepingContacts.begin(); it != SleepingContacts.end();)
			{
				const BodyHandle bodyA{ it->first.first };
				const BodyHandle bodyB{ it->first.second };
				const bool bWoken = Bodies().IsActive(ToJolt(bodyA)) || Bodies().IsActive(ToJolt(bodyB));
				if (bWoken && it->second)
				{
					kept.push_back(ContactEvent{ .Kind = EContactEvent::Ended, .BodyA = bodyA, .BodyB = bodyB });
					it = SleepingContacts.erase(it);
					continue;
				}
				it->second = bWoken;
				++it;
			}

			events.swap(kept);
		}

		// Scratch memory for a step
		JPH::TempAllocatorImpl Temp{ 16 * 1024 * 1024 };
		JPH::JobSystemThreadPool Jobs;

		// Jolt keeps references to these; they are made before the system and go after it
		BroadPhaseLayerMap BroadPhaseMap;
		ObjectVsBroadPhaseFilter BroadPhaseFilter;
		ObjectPairFilter PairFilter;
		ContactCollector Contacts;

		JPH::PhysicsSystem System;
		// Bodies added since the broad phase was last optimized, a cost worth paying once after a
		// batch of new bodies, before the next step
		bool bBroadPhaseStale = false;

		// Sorted, step by step, until taken
		std::vector<ContactEvent> ContactEvents;
		uint32_t MaxContactEvents = 0;
		bool bContactEventsDropped = false;
		// Contacts whose bodies sleep, and whether one of them was awake after the last step
		std::map<BodyPair, bool> SleepingContacts;

		// By handle; ordered, so that removing a body's constraints removes them in the same order
		// every time, as Jolt solves them in the order they are in
		ConstraintMap Constraints;
		uint32_t NextConstraint = 0;
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
			LayerOf(desc));
		settings.mIsSensor = desc.bSensor;
		// Jolt leaves kinematic bodies out of a static or kinematic sensor otherwise
		settings.mCollideKinematicVsNonDynamic = desc.bSensor;
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

		// Jolt's constraints would point at the body once it is gone
		for (auto it = Impl->Constraints.begin(); it != Impl->Constraints.end();)
		{
			const JPH::TwoBodyConstraint& constraint = *it->second;
			const bool bJoined = constraint.GetBody1()->GetID() == ToJolt(body) || constraint.GetBody2()->GetID() == ToJolt(body);
			it = bJoined ? Impl->RemoveConstraint(it) : std::next(it);
		}

		// Jolt ended these when the bodies fell asleep, so it won't again
		for (auto it = Impl->SleepingContacts.begin(); it != Impl->SleepingContacts.end();)
		{
			if (it->first.first != body.Value && it->first.second != body.Value)
			{
				++it;
				continue;
			}
			Impl->Contacts.Add(ContactEvent{ .Kind = EContactEvent::Ended, .BodyA = BodyHandle{ it->first.first }, .BodyB = BodyHandle{ it->first.second } });
			it = Impl->SleepingContacts.erase(it);
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

		// Jolt's threads are done: the step's contact events can be put in order
		std::vector<ContactEvent> events = Impl->Contacts.Take();
		Impl->FilterSleepingContacts(events);
		SortContactEvents(events);

		const size_t room = Impl->MaxContactEvents - std::min<size_t>(Impl->ContactEvents.size(), Impl->MaxContactEvents);
		if (events.size() > room && !Impl->bContactEventsDropped)
		{
			LOG_WARNING("Physics: contact events dropped, {0} are waiting to be taken", Impl->ContactEvents.size());
			Impl->bContactEventsDropped = true;
		}
		Impl->ContactEvents.insert(Impl->ContactEvents.end(), events.begin(), events.begin() + std::min(events.size(), room));
	}

	std::vector<ContactEvent> PhysicsWorld::TakeContactEvents()
	{
		Impl->bContactEventsDropped = false;
		return std::exchange(Impl->ContactEvents, {});
	}

	std::optional<QueryHit> PhysicsWorld::RayCast(const glm::vec3& origin, const glm::vec3& direction, float maxDistance,
		const QueryFilter& filter) const
	{
		const glm::vec3 unitDirection = NormalizedOr(direction, glm::vec3(0.0f));
		if (unitDirection == glm::vec3(0.0f) || maxDistance <= 0.0f)
		{
			return std::nullopt;
		}

		const JPH::RRayCast ray(JPH::RVec3(ToJolt(origin)), ToJolt(unitDirection * maxDistance));
		JPH::RayCastResult result;
		const QueryLayerFilter layerFilter(filter);
		const QueryBodyFilter bodyFilter(filter);
		if (!Impl->System.GetNarrowPhaseQuery().CastRay(ray, result, {}, layerFilter, bodyFilter))
		{
			return std::nullopt;
		}

		const JPH::BodyLockRead lock(Impl->System.GetBodyLockInterface(), result.mBodyID);
		if (!lock.Succeeded())
		{
			return std::nullopt;
		}
		const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
		return QueryHit{
			.Body = ToHandle(result.mBodyID),
			.Point = ToGlm(JPH::Vec3(point)),
			.Normal = ToGlm(lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point)),
			.Distance = result.mFraction * maxDistance,
			.UserData = lock.GetBody().GetUserData(),
		};
	}

	std::optional<QueryHit> PhysicsWorld::ShapeCast(const BodyShape& shape, const BodyPose& pose, const glm::vec3& direction,
		float maxDistance, const QueryFilter& filter) const
	{
		const glm::vec3 unitDirection = NormalizedOr(direction, glm::vec3(0.0f));
		const JPH::ShapeRefC joltShape = MakeShape(shape);
		if (unitDirection == glm::vec3(0.0f) || maxDistance <= 0.0f || !joltShape)
		{
			return std::nullopt;
		}

		const JPH::RShapeCast cast =
			JPH::RShapeCast::sFromWorldTransform(joltShape, JPH::Vec3::sOne(), ToJolt(pose), ToJolt(unitDirection * maxDistance));
		JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
		const QueryLayerFilter layerFilter(filter);
		const QueryBodyFilter bodyFilter(filter);
		Impl->System.GetNarrowPhaseQuery().CastShape(cast, JPH::ShapeCastSettings(), JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);
		if (!collector.HadHit())
		{
			return std::nullopt;
		}

		// Jolt's penetration axis points into the hit body
		const JPH::ShapeCastResult& hit = collector.mHit;
		return QueryHit{
			.Body = ToHandle(hit.mBodyID2),
			.Point = ToGlm(hit.mContactPointOn2),
			.Normal = ToGlm(-hit.mPenetrationAxis.NormalizedOr(JPH::Vec3::sZero())),
			.Distance = hit.mFraction * maxDistance,
			.UserData = Impl->Bodies().GetUserData(hit.mBodyID2),
		};
	}

	std::vector<BodyHandle> PhysicsWorld::Overlap(const BodyShape& shape, const BodyPose& pose, const QueryFilter& filter) const
	{
		const JPH::ShapeRefC joltShape = MakeShape(shape);
		if (!joltShape)
		{
			return {};
		}

		JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
		const QueryLayerFilter layerFilter(filter);
		const QueryBodyFilter bodyFilter(filter);
		Impl->System.GetNarrowPhaseQuery().CollideShape(joltShape, JPH::Vec3::sOne(), ToJolt(pose), JPH::CollideShapeSettings(),
			JPH::RVec3::sZero(), collector, {}, layerFilter, bodyFilter);

		// One hit per pair of touching sub shapes, in the order Jolt's threads left the broad phase in
		std::vector<BodyHandle> bodies;
		for (const JPH::CollideShapeResult& hit : collector.mHits)
		{
			bodies.push_back(ToHandle(hit.mBodyID2));
		}
		std::sort(bodies.begin(), bodies.end(), [](BodyHandle a, BodyHandle b)
			{
				return a.Value < b.Value;
			});
		bodies.erase(std::unique(bodies.begin(), bodies.end()), bodies.end());
		return bodies;
	}

	ConstraintHandle PhysicsWorld::CreateConstraint(const ConstraintDesc& desc)
	{
		if (!desc.Body.IsValid() || desc.Body == desc.Other)
		{
			LOG_ERROR("Physics: a constraint needs a body, and another body or the world");
			return {};
		}

		const JPH::Vec3 point = ToJolt(desc.Point);
		const JPH::Vec3 axis = ToJolt(NormalizedOr(desc.Axis, glm::vec3(0.0f, 1.0f, 0.0f)));
		JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
		switch (desc.Kind)
		{
		case EConstraintKind::Fixed:
		{
			JPH::FixedConstraintSettings fixed;
			fixed.mAutoDetectPoint = true;
			settings = new JPH::FixedConstraintSettings(fixed);
			break;
		}
		case EConstraintKind::Point:
		{
			JPH::PointConstraintSettings pivot;
			pivot.mPoint1 = pivot.mPoint2 = JPH::RVec3(point);
			settings = new JPH::PointConstraintSettings(pivot);
			break;
		}
		case EConstraintKind::Hinge:
		{
			JPH::HingeConstraintSettings hinge;
			hinge.mPoint1 = hinge.mPoint2 = JPH::RVec3(point);
			hinge.mHingeAxis1 = hinge.mHingeAxis2 = axis;
			hinge.mNormalAxis1 = hinge.mNormalAxis2 = axis.GetNormalizedPerpendicular();
			if (desc.bLimits)
			{
				hinge.mLimitsMin = std::clamp(desc.LimitMin, -JPH::JPH_PI, 0.0f);
				hinge.mLimitsMax = std::clamp(desc.LimitMax, 0.0f, JPH::JPH_PI);
			}
			settings = new JPH::HingeConstraintSettings(hinge);
			break;
		}
		case EConstraintKind::Slider:
		{
			JPH::SliderConstraintSettings slider;
			slider.mAutoDetectPoint = true;
			slider.SetSliderAxis(axis);
			if (desc.bLimits)
			{
				slider.mLimitsMin = std::min(desc.LimitMin, 0.0f);
				slider.mLimitsMax = std::max(desc.LimitMax, 0.0f);
			}
			settings = new JPH::SliderConstraintSettings(slider);
			break;
		}
		default:
		{
			// Jolt's body 1 is Other below, so its end is point 1
			JPH::DistanceConstraintSettings distance;
			distance.mPoint1 = JPH::RVec3(ToJolt(desc.OtherPoint));
			distance.mPoint2 = JPH::RVec3(point);
			distance.mMinDistance = std::max(desc.MinDistance, 0.0f);
			distance.mMaxDistance = std::max(desc.MaxDistance, distance.mMinDistance);
			settings = new JPH::DistanceConstraintSettings(distance);
			break;
		}
		}

		JPH::Ref<JPH::TwoBodyConstraint> constraint;
		{
			// The world, or the other body, comes first: Jolt's slider turns from body 1, which
			// should be the heavier
			const JPH::BodyID ids[] = { ToJolt(desc.Other), ToJolt(desc.Body) };
			const JPH::BodyLockMultiWrite lock(Impl->System.GetBodyLockInterface(), ids, 2);
			JPH::Body* body = lock.GetBody(1);
			JPH::Body* other = desc.Other.IsValid() ? lock.GetBody(0) : &JPH::Body::sFixedToWorld;
			if (body == nullptr || other == nullptr)
			{
				LOG_ERROR("Physics: a constraint's body doesn't exist");
				return {};
			}
			constraint = settings->Create(*other, *body);
		}

		Impl->System.AddConstraint(constraint);
		// Sleeping bodies would hang where they are until something woke them
		Impl->ActivateIfMoving(constraint->GetBody1());
		Impl->ActivateIfMoving(constraint->GetBody2());

		const ConstraintHandle handle{ Impl->NextConstraint++ };
		Impl->Constraints.emplace(handle.Value, constraint);
		return handle;
	}

	void PhysicsWorld::DestroyConstraint(ConstraintHandle constraint)
	{
		const auto it = Impl->Constraints.find(constraint.Value);
		if (it != Impl->Constraints.end())
		{
			Impl->RemoveConstraint(it);
		}
	}

	size_t PhysicsWorld::GetConstraintCount() const
	{
		return Impl->Constraints.size();
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
