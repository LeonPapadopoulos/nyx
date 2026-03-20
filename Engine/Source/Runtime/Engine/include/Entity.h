#pragma once

#include "NyxEngineAPI.h"
#include "TypedHandle.h"
#include "Assertions.h"

#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <typeindex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Nyx
{
	namespace Engine
	{
		struct EntityTag {};
		using Entity = TypedHandle<EntityTag>;

		class Registry;

		constexpr uint32_t RegistryInvalidIndex = std::numeric_limits<uint32_t>::max();

		struct IComponentPool
		{
			virtual ~IComponentPool() = default;
			virtual void RemoveIfPresent(Entity entity) = 0;
			virtual void Clear() = 0;
		};

		template<typename T>
		class ComponentPool final : public IComponentPool
		{
			friend class Registry;

		public:
			~ComponentPool() override = default;

			template<typename... Args>
			T& Emplace(Entity entity, Args&&... args)
			{
				const uint32_t entityIndex = entity.Index();

				if (entityIndex >= Sparse.size())
				{
					Sparse.resize(entityIndex + 1, RegistryInvalidIndex);
				}

				ASSERT(Sparse[entityIndex] == RegistryInvalidIndex && "Entity already has this component.");

				const uint32_t denseIndex = static_cast<uint32_t>(Components.size());

				Sparse[entityIndex] = denseIndex;
				Entities.push_back(entity);
				Components.emplace_back(std::forward<Args>(args)...);

				return Components.back();
			}

			bool Has(Entity entity) const
			{
				const uint32_t entityIndex = entity.Index();

				if (entityIndex >= Sparse.size())
				{
					return false;
				}

				const uint32_t denseIndex = Sparse[entityIndex];
				if (denseIndex == RegistryInvalidIndex)
				{
					return false;
				}

				// Compare full handle, not just index, so stale-generation handles fail.
				return Entities[denseIndex] == entity;
			}

			T& Get(Entity entity)
			{
				ASSERT(Has(entity) && "Entity does not have this component.");
				return Components[Sparse[entity.Index()]];
			}

			const T& Get(Entity entity) const
			{
				ASSERT(Has(entity) && "Entity does not have this component.");
				return Components[Sparse[entity.Index()]];
			}

			bool Remove(Entity entity)
			{
				if (!Has(entity))
				{
					return false;
				}

				const uint32_t entityIndex = entity.Index();
				const uint32_t denseIndex = Sparse[entityIndex];
				const uint32_t lastDenseIndex = static_cast<uint32_t>(Components.size() - 1);

				if (denseIndex != lastDenseIndex)
				{
					Components[denseIndex] = std::move(Components[lastDenseIndex]);

					const Entity movedEntity = Entities[lastDenseIndex];
					Entities[denseIndex] = movedEntity;
					Sparse[movedEntity.Index()] = denseIndex;
				}

				Components.pop_back();
				Entities.pop_back();
				Sparse[entityIndex] = RegistryInvalidIndex;

				return true;
			}

			void RemoveIfPresent(Entity entity) override
			{
				Remove(entity);
			}

			void Clear() override
			{
				Sparse.clear();
				Entities.clear();
				Components.clear();
			}

		private:
			std::vector<uint32_t> Sparse;
			std::vector<Entity> Entities;
			std::vector<T> Components;
		};

		class NYXENGINE_API Registry
		{
		public:
			Registry() = default;
			~Registry();

			Registry(const Registry&) = delete;
			Registry& operator=(const Registry&) = delete;

			Registry(Registry&&) = default;
			Registry& operator=(Registry&&) = default;

		public:
			Entity CreateEntity();
			bool DestroyEntity(Entity entity);
			bool IsAlive(Entity entity) const;
			void Clear();

		public:
			template<typename T, typename... Args>
			T& Add(Entity entity, Args&&... args);

			template<typename T, typename... Args>
			T& Emplace(Entity entity, Args&&... args);

			template<typename T>
			bool Has(Entity entity) const;

			template<typename T>
			T& Get(Entity entity);

			template<typename T>
			const T& Get(Entity entity) const;

			template<typename T>
			bool Remove(Entity entity);

			template<typename T, typename Func>
			void Each(Func&& func);

		private:
			struct EntitySlot
			{
				// Start at 1 so TypedHandle Value == 0 can remain invalid
				uint32_t Generation = 1;
				uint32_t NextFreeIndex = RegistryInvalidIndex;
				bool bAlive = false;
			};

		private:
			void ValidateEntityAlive(Entity entity) const;

			template<typename T>
			ComponentPool<T>* TryGetPool();

			template<typename T>
			const ComponentPool<T>* TryGetPool() const;

			template<typename T>
			ComponentPool<T>& GetOrCreatePool();

		private:
			std::vector<EntitySlot> EntitySlots;
			uint32_t FreeListHead = RegistryInvalidIndex;
			std::unordered_map<std::type_index, std::unique_ptr<IComponentPool>> ComponentPools;
		};

		// ----------------------------------------------------------------------------------------
		// Template Implementation
		// ----------------------------------------------------------------------------------------

		template<typename T, typename... Args>
		T& Registry::Add(Entity entity, Args&&... args)
		{
			return Emplace<T>(entity, std::forward<Args>(args)...);
		}

		template<typename T, typename... Args>
		T& Registry::Emplace(Entity entity, Args&&... args)
		{
			ValidateEntityAlive(entity);

			ComponentPool<T>& pool = GetOrCreatePool<T>();
			return pool.Emplace(entity, std::forward<Args>(args)...);
		}

		template<typename T>
		bool Registry::Has(Entity entity) const
		{
			if (!IsAlive(entity))
			{
				return false;
			}

			const ComponentPool<T>* pool = TryGetPool<T>();
			return pool ? pool->Has(entity) : false;
		}

		template<typename T>
		T& Registry::Get(Entity entity)
		{
			ValidateEntityAlive(entity);

			ComponentPool<T>* pool = TryGetPool<T>();
			ASSERT(pool && "Component pool does not exist.");

			return pool->Get(entity);
		}

		template<typename T>
		const T& Registry::Get(Entity entity) const
		{
			ValidateEntityAlive(entity);

			const ComponentPool<T>* pool = TryGetPool<T>();
			ASSERT(pool && "Component pool does not exist.");

			return pool->Get(entity);
		}

		template<typename T>
		bool Registry::Remove(Entity entity)
		{
			if (!IsAlive(entity))
			{
				return false;
			}

			ComponentPool<T>* pool = TryGetPool<T>();
			return pool ? pool->Remove(entity) : false;
		}

		template<typename T, typename Func>
		void Registry::Each(Func && func)
		{
			ComponentPool<T>* pool = TryGetPool<T>();
			if (!pool)
			{
				return;
			}

			for (size_t i = 0; i < pool->Components.size(); ++i)
			{
				func(pool->Entities[i], pool->Components[i]);
			}
		}

		template<typename T>
		ComponentPool<T>* Registry::TryGetPool()
		{
			const auto it = ComponentPools.find(std::type_index(typeid(T)));
			if (it == ComponentPools.end())
			{
				return nullptr;
			}

			return static_cast<ComponentPool<T>*>(it->second.get());
		}

		template<typename T>
		const ComponentPool<T>* Registry::TryGetPool() const
		{
			const auto it = ComponentPools.find(std::type_index(typeid(T)));
			if (it == ComponentPools.end())
			{
				return nullptr;
			}

			return static_cast<const ComponentPool<T>*>(it->second.get());
		}

		template<typename T>
		ComponentPool<T>& Registry::GetOrCreatePool()
		{
			const std::type_index key(typeid(T));

			const auto it = ComponentPools.find(key);
			if (it != ComponentPools.end())
			{
				return *static_cast<ComponentPool<T>*>(it->second.get());
			}

			auto newPool = std::make_unique<ComponentPool<T>>();
			ComponentPool<T>* rawPtr = newPool.get();

			ComponentPools.emplace(key, std::move(newPool));
			return *rawPtr;
		}
	}
}