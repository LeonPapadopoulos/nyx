#pragma once

#include "EditableObjectTraits.h"
#include "EditableTargetResolver.h"
#include "Entity.h"
#include "SceneDocument.h"
#include "TransformComponent.h"
#include "PropertyValue.h"
#include "PropertyValueUtils.h"

#include <glm/gtc/quaternion.hpp>

namespace Nyx::Editor
{
	inline Nyx::Reflection::PropertyValue GetTransformRotationValue(const Nyx::Engine::TransformComponent& component)
	{
		return glm::normalize(component.Rotation);
	}

	inline void SetTransformRotationValue(Nyx::Engine::TransformComponent& component, const Nyx::Reflection::PropertyValue& value)
	{
		component.Rotation = glm::normalize(std::get<glm::quat>(value));
	}

	template<>
	struct EditableObjectTraits<Nyx::Engine::TransformComponent>
	{
		static constexpr bool Enabled = true;

		static const std::vector<EditablePropertyDesc<Nyx::Engine::TransformComponent>>& GetProperties()
		{
			static const std::vector<EditablePropertyDesc<Nyx::Engine::TransformComponent>> Properties =
			{
				NYX_EDITABLE_MEMBER_PROPERTY(Nyx::Engine::TransformComponent, Position, glm::vec3),
				EditablePropertyDesc<Nyx::Engine::TransformComponent>{
					"Rotation",
					& GetTransformRotationValue,
					& SetTransformRotationValue
				},
				NYX_EDITABLE_MEMBER_PROPERTY(Nyx::Engine::TransformComponent, Scale, glm::vec3)
			};

			return Properties;
		}
	};

	template<>
	struct EditableTargetResolver<Nyx::Engine::TransformComponent>
	{
		static constexpr bool Enabled = true;

		static Nyx::Engine::TransformComponent* Resolve(
			EditorTransactionContext& context,
			InspectorTargetId targetId)
		{
			if (!context.ActiveScene)
			{
				return nullptr;
			}

			Nyx::Engine::Entity entity{};
			entity.Value = static_cast<decltype(entity.Value)>(targetId.Value);

			auto& world = context.ActiveScene->GetRegistry();

			if (!world.IsAlive(entity) || !world.Has<Nyx::Engine::TransformComponent>(entity))
			{
				return nullptr;
			}

			return &world.Get<Nyx::Engine::TransformComponent>(entity);
		}
	};
}