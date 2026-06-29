#pragma once

#include "ReflectionTypes.h"
#include "Entity.h"
#include "SceneSerializationTypes.h"

#include <string_view>
#include <vector>
#include <functional>

namespace Nyx::Engine
{
	struct SceneComponentTypeOps
	{
		const Nyx::Reflection::TypeMetadata* TypeMetadata = nullptr;
		const char* SerializedTypeName = nullptr;

		bool (*Has)(const Registry&, Entity) = nullptr;
		const void* (*GetConst)(const Registry&, Entity) = nullptr;
		void* (*AddDefault)(Registry&, Entity) = nullptr;

		std::function<void(void*, ScenePostLoadContext&)> PostLoadResolve;
	};

	class SceneComponentTypeRegistry
	{
	public:
		static SceneComponentTypeRegistry& Get();

		void Register(SceneComponentTypeOps ops);
		const std::vector<SceneComponentTypeOps>& GetAll() const;
		const SceneComponentTypeOps* FindBySerializedTypeName(std::string_view typeName) const;

	private:
		std::vector<SceneComponentTypeOps> Types;
	};

	template<typename T>
	SceneComponentTypeOps MakeSceneComponentTypeOps(
		const char* serializedTypeName = nullptr,
		void (*postLoadResolve)(T&, ScenePostLoadContext&) = nullptr)
	{
		SceneComponentTypeOps ops{};
		ops.TypeMetadata = &Nyx::Reflection::GetTypeMetadata<T>();
		ops.SerializedTypeName = serializedTypeName ? serializedTypeName : ops.TypeMetadata->Name;

		ops.Has = [](const Registry& registry, Entity entity) -> bool
			{
				return registry.Has<T>(entity);
			};

		ops.GetConst = [](const Registry& registry, Entity entity) -> const void*
			{
				return registry.Has<T>(entity) ? &registry.Get<T>(entity) : nullptr;
			};

		ops.AddDefault = [](Registry& registry, Entity entity) -> void*
			{
				return &registry.Add<T>(entity, T{});
			};

		if (postLoadResolve)
		{
			ops.PostLoadResolve = [postLoadResolve](void* component, ScenePostLoadContext& context)
				{
					postLoadResolve(*static_cast<T*>(component), context);
				};
		}

		return ops;
	}
}