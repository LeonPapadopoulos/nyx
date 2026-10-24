#include "NyxPCH.h"
#include "PhysicsConstraints.h"

#include "ConstraintComponent.h"
#include "Profiler.h"
#include "TransformComponent.h"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <cstring>
#include <optional>

namespace Nyx
{
	namespace
	{
		EConstraintKind ToPhysics(EJointKind kind)
		{
			switch (kind)
			{
			case EJointKind::Fixed:    return EConstraintKind::Fixed;
			case EJointKind::Hinge:    return EConstraintKind::Hinge;
			case EJointKind::Slider:   return EConstraintKind::Slider;
			case EJointKind::Distance: return EConstraintKind::Distance;
			default:                   return EConstraintKind::Point;
			}
		}

		EConstraintMotor ToPhysics(EJointMotor mode)
		{
			switch (mode)
			{
			case EJointMotor::Velocity: return EConstraintMotor::Velocity;
			case EJointMotor::Position: return EConstraintMotor::Position;
			default:                    return EConstraintMotor::Off;
			}
		}

		// A hinge's angles are degrees in the component, radians in the physics
		float AngleScale(EJointKind kind)
		{
			return kind == EJointKind::Hinge ? glm::pi<float>() / 180.0f : 1.0f;
		}

		ConstraintMotor DescribeMotor(const ConstraintComponent& component)
		{
			return ConstraintMotor{
				.Mode = ToPhysics(component.MotorMode),
				.Target = component.MotorTarget * AngleScale(component.Kind),
				.Frequency = component.MotorFrequency,
				.Damping = component.MotorDamping,
				.MaxForce = component.MotorMaxForce,
			};
		}

		bool operator==(const ConstraintMotor& a, const ConstraintMotor& b)
		{
			return a.Mode == b.Mode && a.Target == b.Target && a.Frequency == b.Frequency && a.Damping == b.Damping && a.MaxForce == b.MaxForce;
		}

		glm::vec3 ScaleOf(const Registry& world, Entity entity)
		{
			return world.Has<TransformComponent>(entity) ? world.Get<TransformComponent>(entity).Scale : glm::vec3(1.0f);
		}

		ConstraintDesc DescribeConstraint(const Registry& world, const ConstraintComponent& component, Entity bodyEntity,
			std::optional<Entity> otherEntity, BodyHandle body, BodyHandle other)
		{
			const float angleScale = AngleScale(component.Kind);
			return ConstraintDesc{
				.Kind = ToPhysics(component.Kind),
				.Body = body,
				.Other = other,
				.Space = EConstraintSpace::Local,
				// Points scale with their entities, as the shapes do; frames turn with them alone
				.BodyFrame = ConstraintFrame{ .Point = component.BodyPoint * ScaleOf(world, bodyEntity), .Rotation = component.BodyRotation },
				.OtherFrame = ConstraintFrame{ .Point = otherEntity ? component.OtherPoint * ScaleOf(world, *otherEntity) : component.OtherPoint,
					.Rotation = component.OtherRotation },
				.bLimits = component.bLimits,
				.LimitMin = component.LimitMin * angleScale,
				.LimitMax = component.LimitMax * angleScale,
				.MinDistance = component.MinDistance,
				.MaxDistance = component.MaxDistance,
				.SpringFrequency = component.SpringFrequency,
				.SpringDamping = component.SpringDamping,
				.Motor = DescribeMotor(component),
				.bEnabled = component.bEnabled,
				.BreakForce = component.BreakForce,
				.BreakTorque = component.BreakTorque,
				.BreakSeconds = component.BreakSeconds,
			};
		}

		// FNV-1a over what makes the constraint: all but whether it is on and its motor, which reach
		// it without making it again
		uint64_t HashConstraint(const ConstraintDesc& desc)
		{
			uint64_t hash = 14695981039346656037ull;
			const auto add = [&hash](const auto& value)
			{
				unsigned char bytes[sizeof(value)];
				std::memcpy(bytes, &value, sizeof(value));
				for (const unsigned char byte : bytes)
				{
					hash = (hash ^ byte) * 1099511628211ull;
				}
			};
			const auto addFrame = [&add](const ConstraintFrame& frame)
			{
				add(frame.Point.x);
				add(frame.Point.y);
				add(frame.Point.z);
				add(frame.Rotation.w);
				add(frame.Rotation.x);
				add(frame.Rotation.y);
				add(frame.Rotation.z);
			};
			add(desc.Kind);
			add(desc.Body.Value);
			add(desc.Other.Value);
			addFrame(desc.BodyFrame);
			addFrame(desc.OtherFrame);
			add(desc.bLimits);
			add(desc.LimitMin);
			add(desc.LimitMax);
			add(desc.MinDistance);
			add(desc.MaxDistance);
			add(desc.SpringFrequency);
			add(desc.SpringDamping);
			add(desc.BreakForce);
			add(desc.BreakTorque);
			add(desc.BreakSeconds);
			return hash;
		}

		ConstraintStateComponent& StateOf(Registry& world, Entity holder)
		{
			return world.Has<ConstraintStateComponent>(holder) ? world.Get<ConstraintStateComponent>(holder) : world.Add<ConstraintStateComponent>(holder);
		}
	}

	void PhysicsConstraints::SyncBeforeStep(Registry& world, PhysicsWorld& physics, const PhysicsBodies& bodies, const EntityGuidLookup& guids)
	{
		NYX_PROFILE_SCOPE("PhysicsConstraints::SyncBeforeStep");
		for (auto& [key, tracked] : Constraints)
		{
			tracked.bSeen = false;
		}

		// In the order of their entities, whatever order the world keeps them in
		std::vector<Entity> holders;
		world.Each<ConstraintComponent>([&](Entity entity, ConstraintComponent&)
			{
				holders.push_back(entity);
			});
		std::sort(holders.begin(), holders.end(), [](Entity a, Entity b)
			{
				return a.Value < b.Value;
			});

		for (const Entity holder : holders)
		{
			ConstraintComponent& component = world.Get<ConstraintComponent>(holder);
			const std::optional<Entity> bodyEntity = component.Body.IsValid() ? FindEntityByGuid(world, guids, component.Body) : std::optional(holder);
			const std::optional<Entity> otherEntity = component.Other.IsValid() ? FindEntityByGuid(world, guids, component.Other) : std::nullopt;
			const BodyHandle body = bodyEntity ? bodies.FindBody(*bodyEntity) : BodyHandle{};
			const BodyHandle other = otherEntity ? bodies.FindBody(*otherEntity) : BodyHandle{};
			// An Other named but not found, or without a body, isn't the world
			const bool bBodiesThere = body.IsValid() && (!component.Other.IsValid() || other.IsValid()) && body != other;

			TrackedConstraint& tracked = Constraints.try_emplace(holder.Value, TrackedConstraint{ .Owner = holder }).first->second;
			tracked.bSeen = true;

			ConstraintStateComponent& state = StateOf(world, holder);
			if (!bBodiesThere)
			{
				// Kept without one until both are there
				physics.DestroyConstraint(tracked.Constraint);
				tracked.Constraint = {};
				state.bHolding = false;
				continue;
			}

			const ConstraintDesc desc = DescribeConstraint(world, component, *bodyEntity, otherEntity, body, other);
			const uint64_t hash = HashConstraint(desc);
			// Its values or bodies changed, or a body was made again or went, taking it along
			if (tracked.Constraint.IsValid() && (tracked.DescriptionHash != hash || !physics.HasConstraint(tracked.Constraint)))
			{
				physics.DestroyConstraint(tracked.Constraint);
				tracked.Constraint = {};
			}

			if (!tracked.Constraint.IsValid())
			{
				tracked.Constraint = physics.CreateConstraint(desc);
				tracked.DescriptionHash = hash;
				tracked.bEnabled = desc.bEnabled;
				tracked.Motor = desc.Motor;
				state.bBroken = state.bBroken && !desc.bEnabled;
			}
			else
			{
				if (tracked.bEnabled != component.bEnabled)
				{
					physics.SetConstraintEnabled(tracked.Constraint, component.bEnabled);
					tracked.bEnabled = component.bEnabled;
					// Switched on again, it is mended
					state.bBroken = state.bBroken && !component.bEnabled;
				}
				if (!(tracked.Motor == desc.Motor))
				{
					physics.SetConstraintMotor(tracked.Constraint, desc.Motor);
					tracked.Motor = desc.Motor;
				}
			}
			state.bHolding = tracked.Constraint.IsValid() && component.bEnabled;
		}

		// Constraints whose component or entity is gone
		for (auto it = Constraints.begin(); it != Constraints.end();)
		{
			if (!it->second.bSeen)
			{
				physics.DestroyConstraint(it->second.Constraint);
				world.Remove<ConstraintStateComponent>(it->second.Owner);
				it = Constraints.erase(it);
			}
			else
			{
				++it;
			}
		}
	}

	void PhysicsConstraints::ReadBackAfterStep(Registry& world, const PhysicsWorld& physics)
	{
		NYX_PROFILE_SCOPE("PhysicsConstraints::ReadBackAfterStep");
		Broken.clear();
		for (auto& [key, tracked] : Constraints)
		{
			// A system may have destroyed it, or taken its parts, during the step
			if (!world.IsAlive(tracked.Owner) || !world.Has<ConstraintComponent>(tracked.Owner) || !world.Has<ConstraintStateComponent>(tracked.Owner))
			{
				continue;
			}
			ConstraintStateComponent& state = world.Get<ConstraintStateComponent>(tracked.Owner);
			const bool bThere = tracked.Constraint.IsValid() && physics.HasConstraint(tracked.Constraint);
			const bool bOn = bThere && physics.IsConstraintEnabled(tracked.Constraint);

			// On before the step and off after it: broken, which only the step does
			if (bThere && tracked.bEnabled && !bOn)
			{
				const ConstraintLoad load = physics.GetConstraintLoad(tracked.Constraint);
				world.Get<ConstraintComponent>(tracked.Owner).bEnabled = false;
				tracked.bEnabled = false;
				state.bBroken = true;
				Broken.push_back(ConstraintBrokenEvent{ .Holder = tracked.Owner, .Load = load });
			}

			state.bHolding = bOn;
			const ConstraintLoad load = bOn ? physics.GetConstraintLoad(tracked.Constraint) : ConstraintLoad{};
			state.Force = load.Force;
			state.Torque = load.Torque;
		}
	}

	ConstraintHandle PhysicsConstraints::FindConstraint(Entity holder) const
	{
		const auto found = Constraints.find(holder.Value);
		return found != Constraints.end() ? found->second.Constraint : ConstraintHandle{};
	}

	size_t PhysicsConstraints::GetConstraintCount() const
	{
		return static_cast<size_t>(std::count_if(Constraints.begin(), Constraints.end(), [](const auto& entry)
			{
				return entry.second.Constraint.IsValid();
			}));
	}
}
