#pragma once

#include "EntityGuid.h"
#include "ReflectionMacros.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>

namespace Nyx
{
	// How a constraint joins its bodies, as PhysicsWorld's EConstraintKind
	NYX_ENUM()
	enum class EJointKind : uint8_t
	{
		// Holds them as they are to each other at the rest
		Fixed,
		// Joins them at a point, around which they turn freely: a ball joint
		Point,
		// Joins them at a point, turning about the frames' X axis only: a door
		Hinge,
		// Lets them move along the frames' X axis only, without turning: a drawer
		Slider,
		// Keeps the two points between a least and a greatest distance apart: a rope or a rod
		Distance
	};

	// What drives a hinge or slider, as PhysicsWorld's EConstraintMotor
	NYX_ENUM()
	enum class EJointMotor : uint8_t
	{
		Off,
		// At the target speed
		Velocity,
		// To the target, like a spring
		Position
	};

	// A joint between two entities' bodies, or one's and the world. Each end is a frame, a point and
	// axes, in its own entity's space: the rest is where the two frames meet, and the bodies are
	// pulled there wherever they are placed. The frames' X axis is what a hinge turns about and a
	// slider moves along; their Y is where a hinge's angle is 0. PhysicsConstraints (Sim) makes the
	// constraint once both bodies are there, makes it again when its values, its entities or their
	// bodies change, and switches it on and off and drives its motor without making it again. What it
	// holds after each step is in the entity's ConstraintStateComponent. The component can sit on
	// either entity, or a third one, so an entity can hold as many as it has holders.
	NYX_REFLECT(Component, meta = (DisplayName = "Constraint"))
	struct ConstraintComponent
	{
		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Constraint", Tooltip = "Fixed holds them together, Point is a ball joint, Hinge turns about the frames' X, Slider moves along it, Distance keeps the points within a range"))
		EJointKind Kind = EJointKind::Point;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Constraint", Tooltip = "The entity whose body it holds; none: this entity's"))
		EntityGuid Body;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Constraint", Tooltip = "The entity it joins the body to; none: the world"))
		EntityGuid Other;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Constraint", Tooltip = "Off, it holds nothing but keeps its rest; it switches itself off when it breaks"))
		bool bEnabled = true;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rest", DisplayName = "Body Point", DragSpeed = 0.01, Tooltip = "Where it holds the body, in the body's entity's space, scaled with it"))
		glm::vec3 BodyPoint{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rest", DisplayName = "Body Rotation", UI = Degrees, Tooltip = "Turns the frame's axes on the body: X is what a hinge turns about and a slider moves along"))
		glm::quat BodyRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rest", DisplayName = "Other Point", DragSpeed = 0.01, Tooltip = "Where it holds the other entity, in its space, scaled with it; in world space for the world"))
		glm::vec3 OtherPoint{ 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Rest", DisplayName = "Other Rotation", UI = Degrees, Tooltip = "Turns the frame's axes on the other entity, or in the world"))
		glm::quat OtherRotation{ 1.0f, 0.0f, 0.0f, 0.0f };

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Limits", Tooltip = "Hinge and slider: whether it stops between Limit Min and Limit Max from the rest"))
		bool bLimits = false;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Limit Min", DragSpeed = 0.1, Tooltip = "A hinge's in degrees, -180 to 0; a slider's in metres, 0 or less"))
		float LimitMin = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Limit Max", DragSpeed = 0.1, Tooltip = "A hinge's in degrees, 0 to 180; a slider's in metres, 0 or more"))
		float LimitMax = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Min Distance", DragSpeed = 0.01, Tooltip = "Distance: how close the points may come, in metres"))
		float MinDistance = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Max Distance", DragSpeed = 0.01, Tooltip = "Distance: how far apart the points may go, in metres"))
		float MaxDistance = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Spring Frequency", DragSpeed = 0.1, Tooltip = "Above 0: a distance's range, or a hinge's or slider's limits, are a spring of this many Hz rather than hard"))
		float SpringFrequency = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Limits", DisplayName = "Spring Damping", DragSpeed = 0.01, Tooltip = "How much of the spring's swing is damped: 1, none past the limit"))
		float SpringDamping = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motor", DisplayName = "Motor", Tooltip = "Hinge and slider: Velocity turns or slides it at the target speed, Position pulls it to the target"))
		EJointMotor MotorMode = EJointMotor::Off;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motor", DisplayName = "Target", DragSpeed = 0.1, Tooltip = "Velocity: degrees or metres a second; Position: degrees or metres from the rest"))
		float MotorTarget = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motor", DisplayName = "Frequency", DragSpeed = 0.1, Tooltip = "Position: how fast it pulls to the target, in Hz"))
		float MotorFrequency = 2.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motor", DisplayName = "Damping", DragSpeed = 0.01, Tooltip = "Position: how much of the swing about the target is damped: 1, none past it"))
		float MotorDamping = 1.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Motor", DisplayName = "Max Force", DragSpeed = 1.0, Tooltip = "The most it pushes, in N m for a hinge and N for a slider; 0: no limit"))
		float MotorMaxForce = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Breaking", DisplayName = "Break Force", DragSpeed = 1.0, Tooltip = "Above 0: it breaks, switching itself off, once it has held the body with more than this many newtons for Break Seconds"))
		float BreakForce = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Breaking", DisplayName = "Break Torque", DragSpeed = 1.0, Tooltip = "Above 0: the same for the torque it holds the body from turning with, in N m"))
		float BreakTorque = 0.0f;

		NYX_PROPERTY(Edit, Undo, Serialize, meta = (Category = "Breaking", DisplayName = "Break Seconds", DragSpeed = 0.01, Tooltip = "How long the load must stay past the break force or torque; 0: one step"))
		float BreakSeconds = 0.0f;
	};

	// What a constraint did in the last step, beside its ConstraintComponent. Runtime only: not
	// reflected, so never saved, edited or sent. PhysicsConstraints adds it with the constraint and
	// fills it after each step.
	struct ConstraintStateComponent
	{
		// Whether it holds now: made, both its bodies there, and on
		bool bHolding = false;
		// It broke and switched itself off (its bEnabled is false), until switched on again
		bool bBroken = false;
		// What it did to its body in the last step it held, in world space: the force (N) it held it
		// with and the torque (N m) it kept it from turning with; nothing while it doesn't hold
		glm::vec3 Force{ 0.0f };
		glm::vec3 Torque{ 0.0f };
	};
}

#include "Generated/Runtime/ConstraintComponent.reflect.h"
