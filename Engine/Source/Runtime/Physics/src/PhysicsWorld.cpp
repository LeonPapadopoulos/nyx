#include "NyxPCH.h"
#include "PhysicsWorld.h"

#include "DebugDraw.h"
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
#include <Jolt/Physics/Character/CharacterVirtual.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
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

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cfloat>
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
			case EShapeKind::Mesh:
			{
				if (!shape.Mesh || shape.Mesh->Indices.size() < 3 || shape.Mesh->Indices.size() % 3 != 0)
				{
					LOG_ERROR("Physics: a mesh shape needs whole triangles");
					return nullptr;
				}
				JPH::VertexList vertices;
				vertices.reserve(shape.Mesh->Vertices.size());
				for (const glm::vec3& vertex : shape.Mesh->Vertices)
				{
					vertices.push_back(JPH::Float3(vertex.x, vertex.y, vertex.z));
				}
				JPH::IndexedTriangleList triangles;
				triangles.reserve(shape.Mesh->Indices.size() / 3);
				for (size_t index = 0; index + 2 < shape.Mesh->Indices.size(); index += 3)
				{
					const uint32_t a = shape.Mesh->Indices[index];
					const uint32_t b = shape.Mesh->Indices[index + 1];
					const uint32_t c = shape.Mesh->Indices[index + 2];
					if (std::max({ a, b, c }) >= vertices.size())
					{
						LOG_ERROR("Physics: a mesh shape's index {0} is past its {1} vertices", std::max({ a, b, c }), vertices.size());
						return nullptr;
					}
					triangles.push_back(JPH::IndexedTriangle(a, b, c));
				}
				// Sanitized by Jolt: degenerate and doubled triangles go
				JPH::MeshShapeSettings settings(std::move(vertices), std::move(triangles));
				settings.mActiveEdgeCosThresholdAngle = std::cos(std::clamp(shape.Mesh->SmoothEdgeAngle, 0.0f, glm::pi<float>()));
				result = settings.Create();
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

		// A point where two bodies touched in a step, for the debug view
		struct ContactPoint
		{
			BodyPair Bodies;
			// Halfway between both surfaces
			glm::vec3 Point{ 0.0f };
			// From the lower handle's body to the other's
			glm::vec3 Normal{ 0.0f };
		};

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
				KeepPoints(body1, body2, manifold);
			}

			void OnContactPersisted(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold, JPH::ContactSettings&) override
			{
				KeepPoints(body1, body2, manifold);
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

			// The points of the step's contacts, if kept
			std::vector<ContactPoint> TakePoints()
			{
				std::vector<ContactPoint> points;
				const std::scoped_lock lock(Mutex);
				points.swap(PendingPoints);
				return points;
			}

			// Set between steps only, read during them
			bool bKeepPoints = false;

		private:
			void KeepPoints(const JPH::Body& body1, const JPH::Body& body2, const JPH::ContactManifold& manifold)
			{
				if (!bKeepPoints)
				{
					return;
				}
				const BodyPair bodies{ ToHandle(body1.GetID()).Value, ToHandle(body2.GetID()).Value };
				const glm::vec3 normal = ToGlm(manifold.mWorldSpaceNormal);
				const std::scoped_lock lock(Mutex);
				for (JPH::uint i = 0; i < manifold.mRelativeContactPointsOn1.size(); ++i)
				{
					const JPH::RVec3 point = manifold.mBaseOffset + 0.5f * (manifold.mRelativeContactPointsOn1[i] + manifold.mRelativeContactPointsOn2[i]);
					PendingPoints.push_back(ContactPoint{ bodies, ToGlm(JPH::Vec3(point)), normal });
				}
			}

			std::mutex Mutex;
			std::vector<ContactEvent> Pending;
			std::vector<ContactPoint> PendingPoints;
		};

		// An order that doesn't depend on which thread found which pair
		void SortContactPoints(std::vector<ContactPoint>& points)
		{
			std::sort(points.begin(), points.end(), [](const ContactPoint& a, const ContactPoint& b)
				{
					return std::tie(a.Bodies, a.Point.x, a.Point.y, a.Point.z) < std::tie(b.Bodies, b.Point.x, b.Point.y, b.Point.z);
				});
		}

		constexpr int DebugCircleSegments = 24;

		// Part of a circle around `center` in the plane of the unit axes `x` and `y`, from angle `from`
		// to `to` (radians, from x towards y), in `segments` lines
		void DrawArc(DebugDraw& drawing, const glm::vec3& center, const glm::vec3& x, const glm::vec3& y, float radius, float from,
			float to, int segments, const glm::vec3& color)
		{
			glm::vec3 previous = center + (x * std::cos(from) + y * std::sin(from)) * radius;
			for (int segment = 1; segment <= segments; ++segment)
			{
				const float angle = from + (to - from) * static_cast<float>(segment) / static_cast<float>(segments);
				const glm::vec3 point = center + (x * std::cos(angle) + y * std::sin(angle)) * radius;
				drawing.Line(previous, point, color);
				previous = point;
			}
		}

		// A capsule along the unit axis `y`, `x` and `z` at right angles to it: 2 rings where its ends
		// begin, 4 lines along its sides and its 2 round ends
		void DrawCapsule(DebugDraw& drawing, const glm::vec3& center, const glm::vec3& x, const glm::vec3& y, const glm::vec3& z,
			float radius, float halfHeightOfCylinder, const glm::vec3& color)
		{
			constexpr float Turn = glm::two_pi<float>();
			constexpr float HalfTurn = glm::pi<float>();
			const glm::vec3 top = center + y * halfHeightOfCylinder;
			const glm::vec3 bottom = center - y * halfHeightOfCylinder;
			DrawArc(drawing, top, x, z, radius, 0.0f, Turn, DebugCircleSegments, color);
			DrawArc(drawing, bottom, x, z, radius, 0.0f, Turn, DebugCircleSegments, color);
			for (const glm::vec3& side : { x, -x, z, -z })
			{
				drawing.Line(bottom + side * radius, top + side * radius, color);
			}
			DrawArc(drawing, top, x, y, radius, 0.0f, HalfTurn, DebugCircleSegments / 2, color);
			DrawArc(drawing, top, z, y, radius, 0.0f, HalfTurn, DebugCircleSegments / 2, color);
			DrawArc(drawing, bottom, x, y, radius, HalfTurn, Turn, DebugCircleSegments / 2, color);
			DrawArc(drawing, bottom, z, y, radius, HalfTurn, Turn, DebugCircleSegments / 2, color);
		}

		// A mesh body's triangles' edges where the body is, or for one of more than `maxTriangles`, the
		// edges of its bounds
		void DrawMeshShape(DebugDraw& drawing, const JPH::Body& body, const glm::vec3& color, uint32_t maxTriangles)
		{
			const JPH::Shape& shape = *body.GetShape();
			if (shape.GetStats().mNumTriangles > maxTriangles)
			{
				const JPH::AABox bounds = body.GetWorldSpaceBounds();
				drawing.Box(ToGlm(bounds.mMin), ToGlm(bounds.mMax), color);
				return;
			}

			JPH::Shape::GetTrianglesContext context;
			shape.GetTrianglesStart(context, JPH::AABox::sBiggest(), JPH::Vec3(body.GetCenterOfMassPosition()), body.GetRotation(), JPH::Vec3::sOne());
			constexpr int Batch = JPH::Shape::cGetTrianglesMinTrianglesRequested;
			JPH::Float3 corners[Batch * 3];
			for (int count = shape.GetTrianglesNext(context, Batch, corners); count > 0; count = shape.GetTrianglesNext(context, Batch, corners))
			{
				for (int triangle = 0; triangle < count; ++triangle)
				{
					const glm::vec3 a = ToGlm(JPH::Vec3(corners[triangle * 3]));
					const glm::vec3 b = ToGlm(JPH::Vec3(corners[triangle * 3 + 1]));
					const glm::vec3 c = ToGlm(JPH::Vec3(corners[triangle * 3 + 2]));
					drawing.Line(a, b, color);
					drawing.Line(b, c, color);
					drawing.Line(c, a, color);
				}
			}
		}

		// A body's shape where the body is, in its axes: a box's 12 edges; a sphere's 3 circles; a
		// capsule (DrawCapsule()); a mesh (DrawMeshShape())
		void DrawBodyShape(DebugDraw& drawing, const JPH::Body& body, const glm::vec3& color, uint32_t maxMeshTriangles)
		{
			const glm::vec3 center = ToGlm(JPH::Vec3(body.GetPosition()));
			const glm::quat rotation = ToGlm(body.GetRotation());
			const glm::vec3 x = rotation * glm::vec3(1.0f, 0.0f, 0.0f);
			const glm::vec3 y = rotation * glm::vec3(0.0f, 1.0f, 0.0f);
			const glm::vec3 z = rotation * glm::vec3(0.0f, 0.0f, 1.0f);
			constexpr float Turn = glm::two_pi<float>();

			const JPH::Shape& shape = *body.GetShape();
			switch (shape.GetSubType())
			{
			case JPH::EShapeSubType::Box:
			{
				const glm::vec3 halfExtents = ToGlm(static_cast<const JPH::BoxShape&>(shape).GetHalfExtent());
				glm::mat4 transform = glm::mat4_cast(rotation);
				transform[3] = glm::vec4(center, 1.0f);
				drawing.Box(transform, -halfExtents, halfExtents, color);
				break;
			}
			case JPH::EShapeSubType::Sphere:
			{
				const float radius = static_cast<const JPH::SphereShape&>(shape).GetRadius();
				DrawArc(drawing, center, x, y, radius, 0.0f, Turn, DebugCircleSegments, color);
				DrawArc(drawing, center, y, z, radius, 0.0f, Turn, DebugCircleSegments, color);
				DrawArc(drawing, center, z, x, radius, 0.0f, Turn, DebugCircleSegments, color);
				break;
			}
			case JPH::EShapeSubType::Capsule:
			{
				const JPH::CapsuleShape& capsule = static_cast<const JPH::CapsuleShape&>(shape);
				DrawCapsule(drawing, center, x, y, z, capsule.GetRadius(), capsule.GetHalfHeightOfCylinder(), color);
				break;
			}
			case JPH::EShapeSubType::Mesh:
			{
				DrawMeshShape(drawing, body, color, maxMeshTriangles);
				break;
			}
			default:
				// A character's body, drawn with its character; PhysicsWorld makes no other shapes
				break;
			}
		}

		const glm::vec3& ColorOf(const JPH::Body& body, const PhysicsDebugOptions& options)
		{
			if (body.IsSensor())
			{
				return options.SensorColor;
			}
			if (body.IsStatic())
			{
				return options.StaticColor;
			}
			if (body.IsKinematic())
			{
				return options.KinematicColor;
			}
			return body.IsActive() ? options.AwakeColor : options.AsleepColor;
		}

		// Where a constraint holds on to a body, in world space; the world's "body" is at the origin
		glm::vec3 AnchorOf(const JPH::Body& body, JPH::Mat44Arg constraintToBody)
		{
			return ToGlm(JPH::Vec3(body.GetCenterOfMassTransform() * constraintToBody.GetTranslation()));
		}

		// Where a constraint holds one of its bodies, as Jolt's settings take it: a point and two axes,
		// in world space or the body's local space about its centre of mass
		struct JointFrame
		{
			JPH::Vec3 Point = JPH::Vec3::sZero();
			JPH::Vec3 AxisX = JPH::Vec3::sAxisX();
			JPH::Vec3 AxisY = JPH::Vec3::sAxisY();
		};

		// A frame in a body's own space, about its position, moved to be about its centre of mass; the
		// world's as it is
		JointFrame ToJointFrame(const ConstraintFrame& frame, const JPH::Body& body)
		{
			const float length = glm::length(frame.Rotation);
			const JPH::Quat rotation = length > 1e-6f ? ToJolt(frame.Rotation) : JPH::Quat::sIdentity();
			const JPH::Vec3 centerOfMass = &body == &JPH::Body::sFixedToWorld ? JPH::Vec3::sZero() : body.GetShape()->GetCenterOfMass();
			return JointFrame{ .Point = ToJolt(frame.Point) - centerOfMass, .AxisX = rotation * JPH::Vec3::sAxisX(), .AxisY = rotation * JPH::Vec3::sAxisY() };
		}

		JPH::MotorSettings ToJolt(const ConstraintMotor& motor)
		{
			const float limit = motor.MaxForce > 0.0f ? motor.MaxForce : FLT_MAX;
			return JPH::MotorSettings(std::max(motor.Frequency, 0.0f), std::max(motor.Damping, 0.0f), limit, limit);
		}

		JPH::EMotorState ToJolt(EConstraintMotor mode)
		{
			switch (mode)
			{
			case EConstraintMotor::Velocity: return JPH::EMotorState::Velocity;
			case EConstraintMotor::Position: return JPH::EMotorState::Position;
			default:                         return JPH::EMotorState::Off;
			}
		}

		// A hinge's or slider's; others have none
		void ApplyMotor(JPH::TwoBodyConstraint& constraint, const ConstraintMotor& motor)
		{
			if (constraint.GetSubType() == JPH::EConstraintSubType::Hinge)
			{
				JPH::HingeConstraint& hinge = static_cast<JPH::HingeConstraint&>(constraint);
				hinge.GetMotorSettings() = ToJolt(motor);
				hinge.SetMotorState(ToJolt(motor.Mode));
				hinge.SetTargetAngularVelocity(motor.Target);
				hinge.SetTargetAngle(motor.Target);
			}
			else if (constraint.GetSubType() == JPH::EConstraintSubType::Slider)
			{
				JPH::SliderConstraint& slider = static_cast<JPH::SliderConstraint&>(constraint);
				slider.GetMotorSettings() = ToJolt(motor);
				slider.SetMotorState(ToJolt(motor.Mode));
				slider.SetTargetVelocity(motor.Target);
				slider.SetTargetPosition(motor.Target);
			}
		}

		// The impulses a constraint gave its body 2, Nyx's Body, in its last step, in world space. Jolt
		// keeps each part's total lambda: a vector in world space, or numbers along axes it works out
		// from the bodies each step, worked out again here from where the step left them.
		ConstraintLoad ImpulsesOf(const JPH::TwoBodyConstraint& constraint)
		{
			const JPH::Body& body1 = *constraint.GetBody1();
			const JPH::Body& body2 = *constraint.GetBody2();
			JPH::Vec3 force = JPH::Vec3::sZero();
			JPH::Vec3 torque = JPH::Vec3::sZero();
			switch (constraint.GetSubType())
			{
			case JPH::EConstraintSubType::Point:
				force = static_cast<const JPH::PointConstraint&>(constraint).GetTotalLambdaPosition();
				break;
			case JPH::EConstraintSubType::Fixed:
			{
				const JPH::FixedConstraint& fixed = static_cast<const JPH::FixedConstraint&>(constraint);
				force = fixed.GetTotalLambdaPosition();
				torque = fixed.GetTotalLambdaRotation();
				break;
			}
			case JPH::EConstraintSubType::Hinge:
			{
				// The rotation part holds body 2's hinge axis along body 1's, pushing along the
				// perpendiculars of body 2's; the limits and the motor turn about body 1's
				const JPH::HingeConstraint& hinge = static_cast<const JPH::HingeConstraint&>(constraint);
				const JPH::Vec3 axis1 = body1.GetRotation() * hinge.GetLocalSpaceHingeAxis1();
				const JPH::Vec3 axis2 = body2.GetRotation() * hinge.GetLocalSpaceHingeAxis2();
				const JPH::Vec3 across = axis2.GetNormalizedPerpendicular();
				const JPH::Vec3 acrossToo = axis2.Cross(across);
				const JPH::Vector<2> rotation = hinge.GetTotalLambdaRotation();
				force = hinge.GetTotalLambdaPosition();
				torque = across.Cross(axis1) * rotation[0] + acrossToo.Cross(axis1) * rotation[1] +
					axis1 * (hinge.GetTotalLambdaRotationLimits() + hinge.GetTotalLambdaMotor());
				break;
			}
			case JPH::EConstraintSubType::Slider:
			{
				// Its axes are body 1's: along, and the two across that hold body 2 on the axis
				const JPH::SliderConstraint& slider = static_cast<const JPH::SliderConstraint&>(constraint);
				const JPH::Mat44 axes = JPH::Mat44::sRotation(body1.GetRotation()).Multiply3x3(slider.GetConstraintToBody1Matrix());
				const JPH::Vector<2> position = slider.GetTotalLambdaPosition();
				force = axes.GetColumn3(1) * position[0] + axes.GetColumn3(2) * position[1] +
					axes.GetColumn3(0) * (slider.GetTotalLambdaPositionLimits() + slider.GetTotalLambdaMotor());
				torque = slider.GetTotalLambdaRotation();
				break;
			}
			case JPH::EConstraintSubType::Distance:
			{
				// Along the line from body 1's end to body 2's
				const glm::vec3 end1 = AnchorOf(body1, constraint.GetConstraintToBody1Matrix());
				const glm::vec3 end2 = AnchorOf(body2, constraint.GetConstraintToBody2Matrix());
				const JPH::Vec3 along = ToJolt(end2 - end1).NormalizedOr(JPH::Vec3::sAxisY());
				force = along * static_cast<const JPH::DistanceConstraint&>(constraint).GetTotalLambdaPosition();
				break;
			}
			default:
				break;
			}
			return ConstraintLoad{ .Force = ToGlm(force), .Torque = ToGlm(torque) };
		}

		// Jolt keeps the impulses of a constraint that is off, or whose bodies sleep, as they were the
		// last step it held
		ConstraintLoad LoadOf(const JPH::TwoBodyConstraint& constraint, float seconds)
		{
			if (seconds <= 0.0f)
			{
				return {};
			}
			const ConstraintLoad impulses = ImpulsesOf(constraint);
			return ConstraintLoad{ .Force = impulses.Force / seconds, .Torque = impulses.Torque / seconds };
		}

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

		// An upright capsule standing on the origin, `fraction` of the given size: a character's, or
		// its body's inside it
		JPH::ShapeRefC MakeCharacterShape(float radius, float halfHeightOfCylinder, float fraction)
		{
			const JPH::Vec3 center(0.0f, radius + halfHeightOfCylinder, 0.0f);
			const JPH::ShapeSettings::ShapeResult result = JPH::RotatedTranslatedShapeSettings(center, JPH::Quat::sIdentity(),
				new JPH::CapsuleShape(halfHeightOfCylinder * fraction, radius * fraction))
															   .Create();
			if (result.HasError())
			{
				LOG_ERROR("Physics: can't make a character's shape: {0}", result.GetError().c_str());
				return nullptr;
			}
			return result.Get();
		}

		// `from` moved towards `to` by at most `maxStep`
		JPH::Vec3 MoveTowards(JPH::Vec3Arg from, JPH::Vec3Arg to, float maxStep)
		{
			const JPH::Vec3 difference = to - from;
			const float length = difference.Length();
			return length <= maxStep ? JPH::Vec3(to) : from + difference * (maxStep / length);
		}

		ECharacterGround ToGround(JPH::CharacterBase::EGroundState state)
		{
			switch (state)
			{
			case JPH::CharacterBase::EGroundState::OnGround:      return ECharacterGround::OnGround;
			case JPH::CharacterBase::EGroundState::OnSteepGround: return ECharacterGround::OnSteepGround;
			default:                                              return ECharacterGround::InAir;
			}
		}

		// Characters walk into static and moving bodies, and through sensors, which still see their
		// bodies
		const QueryFilter CharacterFilter{ .bStatic = true, .bMoving = true, .bSensors = false };

		// A character's body inside its capsule is this much of its size: a little smaller, so it
		// doesn't touch what the character stands on or walks along, and dynamic bodies meet the
		// capsule first
		constexpr float CharacterBodyFraction = 0.9f;
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
			MaxConstraintBreaks = settings.MaxConstraintBreaks;
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

		struct Joint
		{
			JPH::Ref<JPH::TwoBodyConstraint> Constraint;
			// ConstraintDesc's, and how long its load has been past them
			float BreakForce = 0.0f;
			float BreakTorque = 0.0f;
			float BreakSeconds = 0.0f;
			float OverloadSeconds = 0.0f;
		};

		using ConstraintMap = std::map<uint32_t, Joint>;

		JPH::TwoBodyConstraint* FindConstraint(ConstraintHandle constraint) const
		{
			const auto found = Constraints.find(constraint.Value);
			return found != Constraints.end() ? found->second.Constraint.GetPtr() : nullptr;
		}

		// The body's constraints, which Jolt would have point at it once it is gone
		void RemoveConstraintsOf(const JPH::BodyID& body)
		{
			for (auto it = Constraints.begin(); it != Constraints.end();)
			{
				const JPH::TwoBodyConstraint& constraint = *it->second.Constraint;
				const bool bJoined = constraint.GetBody1()->GetID() == body || constraint.GetBody2()->GetID() == body;
				it = bJoined ? RemoveConstraint(it) : std::next(it);
			}
		}

		ConstraintMap::iterator RemoveConstraint(ConstraintMap::iterator it)
		{
			const JPH::Ref<JPH::TwoBodyConstraint> constraint = it->second.Constraint;
			System.RemoveConstraint(constraint);
			// What hung from it falls now, even if it was asleep
			ActivateIfMoving(constraint->GetBody1());
			ActivateIfMoving(constraint->GetBody2());
			return Constraints.erase(it);
		}

		// After a step: constraints whose load has been past their thresholds long enough break,
		// switching themselves off, in handle order
		void BreakOverloadedConstraints(float seconds)
		{
			for (auto& [handle, joint] : Constraints)
			{
				const bool bBreakable = joint.BreakForce > 0.0f || joint.BreakTorque > 0.0f;
				if (!bBreakable || !joint.Constraint->GetEnabled())
				{
					continue;
				}

				const ConstraintLoad load = LoadOf(*joint.Constraint, seconds);
				const bool bOverloaded = (joint.BreakForce > 0.0f && glm::length(load.Force) > joint.BreakForce) ||
					(joint.BreakTorque > 0.0f && glm::length(load.Torque) > joint.BreakTorque);
				joint.OverloadSeconds = bOverloaded ? joint.OverloadSeconds + seconds : 0.0f;
				// Half a step's slack, so the step that reaches the time counts despite rounding
				if (!bOverloaded || joint.OverloadSeconds < joint.BreakSeconds - seconds * 0.5f)
				{
					continue;
				}

				joint.Constraint->SetEnabled(false);
				joint.OverloadSeconds = 0.0f;
				ActivateIfMoving(joint.Constraint->GetBody1());
				ActivateIfMoving(joint.Constraint->GetBody2());
				if (ConstraintBreaks.size() < MaxConstraintBreaks)
				{
					ConstraintBreaks.push_back(ConstraintBreak{ .Constraint = ConstraintHandle{ handle }, .Load = load });
				}
			}
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
		// The last step's contact points, sorted, while kept (SetKeepContactPoints())
		std::vector<ContactPoint> ContactPoints;

		// By handle; ordered, so that removing a body's constraints removes them in the same order
		// every time, as Jolt solves them in the order they are in
		ConstraintMap Constraints;
		uint32_t NextConstraint = 0;
		// Broken since taken (TakeConstraintBreaks()), up to the most kept
		std::vector<ConstraintBreak> ConstraintBreaks;
		uint32_t MaxConstraintBreaks = 0;
		// How long the last step was, to make the constraints' impulses forces
		float LastStepSeconds = 0.0f;

		struct Character
		{
			JPH::Ref<JPH::CharacterVirtual> Virtual;
			// Its capsule, for the debug view
			float Radius = 0.0f;
			float HalfHeightOfCylinder = 0.0f;
		};

		JPH::CharacterVirtual* FindCharacter(CharacterHandle character) const
		{
			const auto found = Characters.find(character.Value);
			return found != Characters.end() ? found->second.Virtual.GetPtr() : nullptr;
		}

		// By handle, so they are drawn in the same order every time. After the system: a character
		// takes its body out of the system when it goes.
		std::map<uint32_t, Character> Characters;
		uint32_t NextCharacter = 0;
	};

	PhysicsWorld::PhysicsWorld(const PhysicsSettings& settings)
	{
		InitializeJoltOnce();
		Impl = std::make_unique<Implementation>(settings);
	}

	PhysicsWorld::~PhysicsWorld() = default;

	BodyHandle PhysicsWorld::CreateBody(const BodyDesc& desc)
	{
		// Jolt can't give triangles a mass, nor collide two meshes
		if (desc.Shape.Kind == EShapeKind::Mesh && desc.Motion != EBodyMotion::Static)
		{
			LOG_ERROR("Physics: a mesh body can only be static");
			return {};
		}
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

		Impl->RemoveConstraintsOf(ToJolt(body));

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
		Impl->LastStepSeconds = seconds;
		Impl->BreakOverloadedConstraints(seconds);

		// Jolt's threads are done: the step's contact events can be put in order
		Impl->ContactPoints = Impl->Contacts.TakePoints();
		SortContactPoints(Impl->ContactPoints);
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
		// Solid convex shapes, so a ray that starts inside one hits it at once; triangles one-sided,
		// so a ray from behind passes through, and every hit's normal points back at the caster
		JPH::RayCastSettings settings;
		settings.mBackFaceModeTriangles = JPH::EBackFaceMode::IgnoreBackFaces;
		settings.mTreatConvexAsSolid = true;
		JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
		const QueryLayerFilter layerFilter(filter);
		const QueryBodyFilter bodyFilter(filter);
		Impl->System.GetNarrowPhaseQuery().CastRay(ray, settings, collector, {}, layerFilter, bodyFilter);
		if (!collector.HadHit())
		{
			return std::nullopt;
		}
		const JPH::RayCastResult& result = collector.mHit;

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
		if (shape.Kind == EShapeKind::Mesh)
		{
			LOG_ERROR("Physics: a mesh can't be cast, only a box, sphere or capsule");
			return std::nullopt;
		}
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
		if (shape.Kind == EShapeKind::Mesh)
		{
			LOG_ERROR("Physics: a mesh can't be overlapped, only a box, sphere or capsule");
			return {};
		}
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

			// Where it holds Other (Jolt's body 1) and Body (body 2): in world space both alike, where
			// the bodies are now being the rest, or each in its body's space
			JPH::EConstraintSpace space = JPH::EConstraintSpace::WorldSpace;
			JointFrame frame1;
			JointFrame frame2;
			// In world space a fixed joint and a slider hold the bodies where they are
			bool bAutoDetectPoint = false;
			if (desc.Space == EConstraintSpace::Local)
			{
				space = JPH::EConstraintSpace::LocalToBodyCOM;
				frame1 = ToJointFrame(desc.OtherFrame, *other);
				frame2 = ToJointFrame(desc.BodyFrame, *body);
			}
			else
			{
				const JPH::Vec3 axis = ToJolt(NormalizedOr(desc.Axis, glm::vec3(0.0f, 1.0f, 0.0f)));
				frame1 = frame2 = JointFrame{ .Point = ToJolt(desc.Point), .AxisX = axis, .AxisY = axis.GetNormalizedPerpendicular() };
				frame1.Point = desc.Kind == EConstraintKind::Distance ? ToJolt(desc.OtherPoint) : frame1.Point;
				bAutoDetectPoint = desc.Kind == EConstraintKind::Fixed || desc.Kind == EConstraintKind::Slider;
			}
			const bool bSpring = desc.SpringFrequency > 0.0f;
			const JPH::SpringSettings spring(JPH::ESpringMode::FrequencyAndDamping, std::max(desc.SpringFrequency, 0.0f), std::max(desc.SpringDamping, 0.0f));

			JPH::Ref<JPH::TwoBodyConstraintSettings> settings;
			switch (desc.Kind)
			{
			case EConstraintKind::Fixed:
			{
				JPH::FixedConstraintSettings fixed;
				fixed.mSpace = space;
				fixed.mAutoDetectPoint = bAutoDetectPoint;
				fixed.mPoint1 = JPH::RVec3(frame1.Point);
				fixed.mAxisX1 = frame1.AxisX;
				fixed.mAxisY1 = frame1.AxisY;
				fixed.mPoint2 = JPH::RVec3(frame2.Point);
				fixed.mAxisX2 = frame2.AxisX;
				fixed.mAxisY2 = frame2.AxisY;
				settings = new JPH::FixedConstraintSettings(fixed);
				break;
			}
			case EConstraintKind::Point:
			{
				JPH::PointConstraintSettings pivot;
				pivot.mSpace = space;
				pivot.mPoint1 = JPH::RVec3(frame1.Point);
				pivot.mPoint2 = JPH::RVec3(frame2.Point);
				settings = new JPH::PointConstraintSettings(pivot);
				break;
			}
			case EConstraintKind::Hinge:
			{
				JPH::HingeConstraintSettings hinge;
				hinge.mSpace = space;
				hinge.mPoint1 = JPH::RVec3(frame1.Point);
				hinge.mHingeAxis1 = frame1.AxisX;
				hinge.mNormalAxis1 = frame1.AxisY;
				hinge.mPoint2 = JPH::RVec3(frame2.Point);
				hinge.mHingeAxis2 = frame2.AxisX;
				hinge.mNormalAxis2 = frame2.AxisY;
				if (desc.bLimits)
				{
					hinge.mLimitsMin = std::clamp(desc.LimitMin, -JPH::JPH_PI, 0.0f);
					hinge.mLimitsMax = std::clamp(desc.LimitMax, 0.0f, JPH::JPH_PI);
				}
				if (bSpring)
				{
					hinge.mLimitsSpringSettings = spring;
				}
				hinge.mMotorSettings = ToJolt(desc.Motor);
				settings = new JPH::HingeConstraintSettings(hinge);
				break;
			}
			case EConstraintKind::Slider:
			{
				JPH::SliderConstraintSettings slider;
				slider.mSpace = space;
				slider.mAutoDetectPoint = bAutoDetectPoint;
				slider.mPoint1 = JPH::RVec3(frame1.Point);
				slider.mSliderAxis1 = frame1.AxisX;
				slider.mNormalAxis1 = frame1.AxisY;
				slider.mPoint2 = JPH::RVec3(frame2.Point);
				slider.mSliderAxis2 = frame2.AxisX;
				slider.mNormalAxis2 = frame2.AxisY;
				if (desc.bLimits)
				{
					slider.mLimitsMin = std::min(desc.LimitMin, 0.0f);
					slider.mLimitsMax = std::max(desc.LimitMax, 0.0f);
				}
				if (bSpring)
				{
					slider.mLimitsSpringSettings = spring;
				}
				slider.mMotorSettings = ToJolt(desc.Motor);
				settings = new JPH::SliderConstraintSettings(slider);
				break;
			}
			default:
			{
				JPH::DistanceConstraintSettings distance;
				distance.mSpace = space;
				distance.mPoint1 = JPH::RVec3(frame1.Point);
				distance.mPoint2 = JPH::RVec3(frame2.Point);
				distance.mMinDistance = std::max(desc.MinDistance, 0.0f);
				distance.mMaxDistance = std::max(desc.MaxDistance, distance.mMinDistance);
				if (bSpring)
				{
					distance.mLimitsSpringSettings = spring;
				}
				settings = new JPH::DistanceConstraintSettings(distance);
				break;
			}
			}
			settings->mEnabled = desc.bEnabled;
			constraint = settings->Create(*other, *body);
		}
		ApplyMotor(*constraint, desc.Motor);

		Impl->System.AddConstraint(constraint);
		// Sleeping bodies would hang where they are until something woke them
		Impl->ActivateIfMoving(constraint->GetBody1());
		Impl->ActivateIfMoving(constraint->GetBody2());

		const ConstraintHandle handle{ Impl->NextConstraint++ };
		Impl->Constraints.emplace(handle.Value,
			Implementation::Joint{ .Constraint = constraint, .BreakForce = desc.BreakForce, .BreakTorque = desc.BreakTorque, .BreakSeconds = desc.BreakSeconds });
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

	bool PhysicsWorld::HasConstraint(ConstraintHandle constraint) const
	{
		return Impl->FindConstraint(constraint) != nullptr;
	}

	void PhysicsWorld::SetConstraintEnabled(ConstraintHandle constraint, bool bEnabled)
	{
		const auto it = Impl->Constraints.find(constraint.Value);
		if (it == Impl->Constraints.end())
		{
			return;
		}
		Implementation::Joint& joint = it->second;
		if (bEnabled && !joint.Constraint->GetEnabled())
		{
			// What it held while it was last on is no guess for what it holds now
			joint.Constraint->ResetWarmStart();
			joint.OverloadSeconds = 0.0f;
		}
		joint.Constraint->SetEnabled(bEnabled);
		Impl->ActivateIfMoving(joint.Constraint->GetBody1());
		Impl->ActivateIfMoving(joint.Constraint->GetBody2());
	}

	bool PhysicsWorld::IsConstraintEnabled(ConstraintHandle constraint) const
	{
		const JPH::TwoBodyConstraint* found = Impl->FindConstraint(constraint);
		return found != nullptr && found->GetEnabled();
	}

	void PhysicsWorld::SetConstraintMotor(ConstraintHandle constraint, const ConstraintMotor& motor)
	{
		if (JPH::TwoBodyConstraint* found = Impl->FindConstraint(constraint))
		{
			ApplyMotor(*found, motor);
			Impl->ActivateIfMoving(found->GetBody1());
			Impl->ActivateIfMoving(found->GetBody2());
		}
	}

	ConstraintLoad PhysicsWorld::GetConstraintLoad(ConstraintHandle constraint) const
	{
		const JPH::TwoBodyConstraint* found = Impl->FindConstraint(constraint);
		return found != nullptr ? LoadOf(*found, Impl->LastStepSeconds) : ConstraintLoad{};
	}

	std::vector<ConstraintBreak> PhysicsWorld::TakeConstraintBreaks()
	{
		return std::exchange(Impl->ConstraintBreaks, {});
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

	CharacterHandle PhysicsWorld::CreateCharacter(const CharacterDesc& desc)
	{
		const float radius = std::max(desc.Radius, 0.01f);
		const float halfHeightOfCylinder = std::max(0.5f * desc.Height - radius, 0.001f);
		const JPH::ShapeRefC shape = MakeCharacterShape(radius, halfHeightOfCylinder, 1.0f);
		const JPH::ShapeRefC bodyShape = MakeCharacterShape(radius, halfHeightOfCylinder, CharacterBodyFraction);
		if (!shape || !bodyShape)
		{
			return {};
		}

		JPH::Ref<JPH::CharacterVirtualSettings> settings = new JPH::CharacterVirtualSettings();
		settings->mShape = shape;
		settings->mInnerBodyShape = bodyShape;
		settings->mInnerBodyLayer = ObjectLayers::Moving;
		settings->mMaxSlopeAngle = std::clamp(desc.MaxSlope, 0.0f, glm::half_pi<float>());
		settings->mMass = std::max(desc.Mass, 0.0f);
		settings->mMaxStrength = std::max(desc.MaxPushForce, 0.0f);
		// Only its lower round end stands on things; what touches it higher up is a wall
		settings->mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);

		JPH::Ref<JPH::CharacterVirtual> character =
			new JPH::CharacterVirtual(settings, JPH::RVec3(ToJolt(desc.Position)), JPH::Quat::sIdentity(), desc.UserData, &Impl->System);
		if (character->GetInnerBodyID().IsInvalid())
		{
			LOG_ERROR("Physics: no room for another character's body ({0} bodies at most)", Impl->System.GetMaxBodies());
			return {};
		}
		Impl->bBroadPhaseStale = true;

		// What it stands on from the start, so its first move knows
		const QueryLayerFilter layerFilter(CharacterFilter);
		character->RefreshContacts(Impl->System.GetDefaultBroadPhaseLayerFilter(ObjectLayers::Moving), layerFilter, {}, {}, Impl->Temp);

		const CharacterHandle handle{ Impl->NextCharacter++ };
		Impl->Characters.emplace(handle.Value, Implementation::Character{ character, radius, halfHeightOfCylinder });
		return handle;
	}

	void PhysicsWorld::DestroyCharacter(CharacterHandle character)
	{
		const auto found = Impl->Characters.find(character.Value);
		if (found == Impl->Characters.end())
		{
			return;
		}
		// Its body goes with it, and so must what was joined to that
		Impl->RemoveConstraintsOf(found->second.Virtual->GetInnerBodyID());
		Impl->Characters.erase(found);
	}

	size_t PhysicsWorld::GetCharacterCount() const
	{
		return Impl->Characters.size();
	}

	void PhysicsWorld::MoveCharacter(CharacterHandle handle, const CharacterMove& move, float seconds)
	{
		JPH::CharacterVirtual* character = Impl->FindCharacter(handle);
		if (character == nullptr || seconds <= 0.0f)
		{
			return;
		}

		// What it stands on may have moved since, e.g. a lift
		character->UpdateGroundVelocity();

		const JPH::Vec3 up = character->GetUp();
		const JPH::Vec3 gravity = Impl->System.GetGravity() * move.GravityFactor;
		const JPH::Vec3 velocity = character->GetLinearVelocity();
		const JPH::Vec3 groundVelocity = character->GetGroundVelocity();
		JPH::Vec3 vertical = velocity.Dot(up) * up;
		JPH::Vec3 horizontal = velocity - vertical;

		// Only along the ground, and not up ground too steep
		JPH::Vec3 walk = ToJolt(move.WalkVelocity);
		walk = character->CancelVelocityTowardsSteepSlopes(walk - walk.Dot(up) * up);

		// It walks on walkable ground, unless it is leaving it, e.g. jumped off in the last move
		const bool bLeavingGround = (velocity - groundVelocity).Dot(up) > 0.1f;
		if (character->GetGroundState() == JPH::CharacterBase::EGroundState::OnGround && !bLeavingGround)
		{
			// Carried by what it stands on, and walking on it
			const JPH::Vec3 groundHorizontal = groundVelocity - groundVelocity.Dot(up) * up;
			horizontal = groundHorizontal + MoveTowards(horizontal - groundHorizontal, walk, move.GroundAcceleration * seconds);
			// Not falling into the ground, which would slide it down slopes it can stand on; the
			// ground snap keeps it on the ground going down
			vertical = groundVelocity.Dot(up) * up;
			if (move.JumpSpeed > 0.0f)
			{
				vertical += move.JumpSpeed * up + gravity * seconds;
			}
		}
		else
		{
			horizontal = MoveTowards(horizontal, walk, move.AirAcceleration * seconds);
			vertical += gravity * seconds;
		}
		character->SetLinearVelocity(horizontal + vertical);

		JPH::CharacterVirtual::ExtendedUpdateSettings settings;
		settings.mStickToFloorStepDown = -up * std::max(move.GroundSnapDistance, 0.0f);
		settings.mWalkStairsStepUp = up * std::max(move.StepHeight, 0.0f);
		const QueryLayerFilter layerFilter(CharacterFilter);
		character->ExtendedUpdate(seconds, gravity, settings, Impl->System.GetDefaultBroadPhaseLayerFilter(ObjectLayers::Moving), layerFilter,
			{}, {}, Impl->Temp);
	}

	CharacterState PhysicsWorld::GetCharacterState(CharacterHandle handle) const
	{
		const JPH::CharacterVirtual* character = Impl->FindCharacter(handle);
		if (character == nullptr)
		{
			return {};
		}
		const ECharacterGround ground = ToGround(character->GetGroundState());
		return CharacterState{
			.Position = ToGlm(JPH::Vec3(character->GetPosition())),
			.Velocity = ToGlm(character->GetLinearVelocity()),
			.Ground = ground,
			.GroundNormal = ground == ECharacterGround::InAir ? glm::vec3(0.0f, 1.0f, 0.0f) : ToGlm(character->GetGroundNormal()),
		};
	}

	void PhysicsWorld::TeleportCharacter(CharacterHandle handle, const glm::vec3& position)
	{
		JPH::CharacterVirtual* character = Impl->FindCharacter(handle);
		if (character == nullptr)
		{
			return;
		}
		character->SetPosition(JPH::RVec3(ToJolt(position)));
		character->SetLinearVelocity(JPH::Vec3::sZero());
		// What it stands on there
		const QueryLayerFilter layerFilter(CharacterFilter);
		character->RefreshContacts(Impl->System.GetDefaultBroadPhaseLayerFilter(ObjectLayers::Moving), layerFilter, {}, {}, Impl->Temp);
	}

	BodyHandle PhysicsWorld::GetCharacterBody(CharacterHandle handle) const
	{
		const JPH::CharacterVirtual* character = Impl->FindCharacter(handle);
		return character != nullptr ? ToHandle(character->GetInnerBodyID()) : BodyHandle{};
	}

	void PhysicsWorld::SetGravity(const glm::vec3& gravity)
	{
		Impl->System.SetGravity(ToJolt(gravity));
	}

	glm::vec3 PhysicsWorld::GetGravity() const
	{
		return ToGlm(Impl->System.GetGravity());
	}

	void PhysicsWorld::DrawDebug(DebugDraw& drawing, const PhysicsDebugOptions& options) const
	{
		if constexpr (!DebugDraw::Enabled)
		{
			return;
		}
		NYX_PROFILE_FUNCTION();

		if (options.bShapes)
		{
			// In the order of their handles
			JPH::BodyIDVector ids;
			Impl->System.GetBodies(ids);
			const JPH::BodyLockInterfaceNoLock& locks = Impl->System.GetBodyLockInterfaceNoLock();
			for (const JPH::BodyID& id : ids)
			{
				const JPH::BodyLockRead lock(locks, id);
				if (lock.Succeeded())
				{
					DrawBodyShape(drawing, lock.GetBody(), ColorOf(lock.GetBody(), options), options.MaxMeshTriangles);
				}
			}
		}

		if (options.bShapes)
		{
			// Upright, standing on their positions
			for (const auto& [handle, character] : Impl->Characters)
			{
				const glm::vec3 feet = ToGlm(JPH::Vec3(character.Virtual->GetPosition()));
				const glm::vec3 center = feet + glm::vec3(0.0f, character.Radius + character.HalfHeightOfCylinder, 0.0f);
				DrawCapsule(drawing, center, glm::vec3(1.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
					character.Radius, character.HalfHeightOfCylinder, options.CharacterColor);
			}
		}

		if (options.bContacts)
		{
			for (const ContactPoint& contact : Impl->ContactPoints)
			{
				drawing.Cross(contact.Point, 0.1f, options.ContactColor);
				drawing.Arrow(contact.Point, contact.Point + contact.Normal * 0.3f, options.ContactColor);
			}
		}

		if (options.bConstraints)
		{
			for (const auto& [handle, joint] : Impl->Constraints)
			{
				const JPH::Ref<JPH::TwoBodyConstraint>& constraint = joint.Constraint;
				if (!constraint->GetEnabled())
				{
					continue;
				}
				const glm::vec3 anchor1 = AnchorOf(*constraint->GetBody1(), constraint->GetConstraintToBody1Matrix());
				const glm::vec3 anchor2 = AnchorOf(*constraint->GetBody2(), constraint->GetConstraintToBody2Matrix());
				drawing.Line(anchor1, anchor2, options.ConstraintColor);
				drawing.Cross(anchor1, 0.1f, options.ConstraintColor);
				drawing.Cross(anchor2, 0.1f, options.ConstraintColor);
			}
		}
	}

	void PhysicsWorld::SetKeepContactPoints(bool bKeep)
	{
		Impl->Contacts.bKeepPoints = bKeep;
	}
}
