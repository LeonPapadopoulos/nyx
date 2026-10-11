#pragma once
#include "Assertions.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <type_traits>
#include <typeindex>
#include <unordered_map>

namespace Nyx
{
	class Mesh;
	class Texture;
}

namespace Nyx
{
	namespace Engine
	{
		class ResourceManager;

		/////////////////////////////////////////////////////////////////////////////////////////////////
		//** ResourceHandle

		template<typename T>
		class ResourceHandle
		{
		public:
			ResourceHandle() = default;

			ResourceHandle(const std::string& id, ResourceManager* manager)
				: ResourceId(id)
				, Manager(manager)
			{
			}

			T* Get() const
			{
				return Manager ? Manager->template GetResource<T>(ResourceId) : nullptr;
			}

			bool IsValid() const
			{
				return Manager && Manager->template HasResource<T>(ResourceId);
			}

			const std::string& GetId() const
			{
				return ResourceId;
			}

			T* operator->() const
			{
				return Get();
			}

			T& operator*() const
			{
				return *Get();
			}

			operator bool() const
			{
				return IsValid();
			}

		private:
			std::string ResourceId;
			ResourceManager* Manager = nullptr;
		};

		/////////////////////////////////////////////////////////////////////////////////////////////////
		//** Resource

		class Resource
		{
		public:
			explicit Resource(const std::string& id)
				: ResourceId(id)
			{
			}
			virtual ~Resource() = default;

			const std::string& GetId() const
			{
				return ResourceId;
			}

			bool IsLoaded() const
			{
				return bLoaded;
			}

			bool Load()
			{
				bLoaded = DoLoad();
				return bLoaded;
			}

			void Unload()
			{
				DoUnload();
				bLoaded = false;
			}

		protected:
			// Option for resource-specific loading/unloading behavior
			virtual bool DoLoad() = 0;
			virtual bool DoUnload() = 0;

		private:
			std::string ResourceId;
			bool bLoaded = false;
		};

		/////////////////////////////////////////////////////////////////////////////////////////////////
		//** ResourceManager

		// Keeps one loaded resource per type and id, counted by Load() and Release(). The maps are
		// guarded by a mutex, so AsyncResourceManager can load on its thread while others read.
		class ResourceManager
		{
		public:
			template<typename T>
			ResourceHandle<T> Load(const std::string& resourceId)
			{
				static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

				{
					std::lock_guard<std::mutex> lock(Mutex);
					if (ResourceEntry* entry = FindEntry<T>(resourceId))
					{
						++entry->RefCount;
						return ResourceHandle<T>(resourceId, this);
					}
				}

				// Loading can take long, so it happens without the lock.
				auto resource = std::make_shared<T>(resourceId);
				if (!resource->Load())
				{
					return ResourceHandle<T>();
				}

				std::lock_guard<std::mutex> lock(Mutex);
				ResourceEntry& entry = Resources[std::type_index(typeid(T))][resourceId];
				if (entry.Resource)
				{
					// Another thread loaded the same resource meanwhile, so this copy isn't needed.
					resource->Unload();
				}
				else
				{
					entry.Resource = resource;
				}
				++entry.RefCount;

				return ResourceHandle<T>(resourceId, this);
			}

			template<typename T>
			T* GetResource(const std::string& resourceId)
			{
				std::lock_guard<std::mutex> lock(Mutex);
				ResourceEntry* entry = FindEntry<T>(resourceId);
				return entry ? static_cast<T*>(entry->Resource.get()) : nullptr;
			}

			template<typename T>
			bool HasResource(const std::string& resourceId)
			{
				std::lock_guard<std::mutex> lock(Mutex);
				return FindEntry<T>(resourceId) != nullptr;
			}

			// Unloads the resource when its last Load() is released.
			template<typename T>
			void Release(const std::string& resourceId)
			{
				std::shared_ptr<Resource> unloaded;
				{
					std::lock_guard<std::mutex> lock(Mutex);
					auto typeIt = Resources.find(std::type_index(typeid(T)));
					if (typeIt == Resources.end())
					{
						return;
					}

					auto it = typeIt->second.find(resourceId);
					if (it == typeIt->second.end() || --it->second.RefCount > 0)
					{
						return;
					}

					unloaded = std::move(it->second.Resource);
					typeIt->second.erase(it);
				}
				unloaded->Unload();
			}

			void UnloadAll()
			{
				std::lock_guard<std::mutex> lock(Mutex);
				for (auto& [type, typeResources] : Resources)
				{
					for (auto& [id, entry] : typeResources)
					{
						entry.Resource->Unload();
					}
				}
				Resources.clear();
			}

		private:
			struct ResourceEntry
			{
				std::shared_ptr<Resource> Resource;
				int RefCount = 0;
			};

			// Call with the mutex held.
			template<typename T>
			ResourceEntry* FindEntry(const std::string& resourceId)
			{
				auto typeIt = Resources.find(std::type_index(typeid(T)));
				if (typeIt == Resources.end())
				{
					return nullptr;
				}

				auto it = typeIt->second.find(resourceId);
				return it != typeIt->second.end() ? &it->second : nullptr;
			}

			std::mutex Mutex;
			std::unordered_map<std::type_index, std::unordered_map<std::string, ResourceEntry>> Resources;
		};

		/////////////////////////////////////////////////////////////////////////////////////////////////
		//** AsyncResourceManager

		// Loads resources into a ResourceManager on its own thread. Stopping finishes the loads
		// already queued, so every callback runs.
		class AsyncResourceManager
		{
		public:
			explicit AsyncResourceManager(ResourceManager& manager)
				: Manager(manager)
			{
				Start();
			}

			~AsyncResourceManager()
			{
				Stop();
			}

			AsyncResourceManager(const AsyncResourceManager&) = delete;
			AsyncResourceManager& operator=(const AsyncResourceManager&) = delete;

			void Start()
			{
				if (WorkerThread.joinable())
				{
					return;
				}

				bRunning = true;
				WorkerThread = std::thread([this]()
					{
						DoWork();
					});
			}

			void Stop()
			{
				{
					std::lock_guard<std::mutex> lock(QueueMutex);
					bRunning = false;
				}
				Condition.notify_one();
				if (WorkerThread.joinable())
				{
					WorkerThread.join();
				}
			}

			template<typename T>
			void LoadAsync(const std::string& resourceId, std::function<void(ResourceHandle<T>)> callback)
			{
				{
					std::lock_guard<std::mutex> lock(QueueMutex);
					TaskQueue.push([this, resourceId, callback]()
						{
							ResourceHandle<T> handle = Manager.Load<T>(resourceId);
							callback(handle);
						});
				}
				Condition.notify_one();
			}

		private:
			void DoWork()
			{
				while (true)
				{
					std::function<void()> task;
					{
						std::unique_lock<std::mutex> lock(QueueMutex);
						Condition.wait(lock, [this]()
							{
								return !TaskQueue.empty() || !bRunning;
							});

						if (TaskQueue.empty())
						{
							return;
						}

						task = std::move(TaskQueue.front());
						TaskQueue.pop();
					}

					task();
				}
			}

		private:
			ResourceManager& Manager;
			std::thread WorkerThread;
			std::queue<std::function<void()>> TaskQueue;
			std::mutex QueueMutex;
			std::condition_variable Condition;

			// Guarded by QueueMutex.
			bool bRunning = false;
		};

		/////////////////////////////////////////////////////////////////////////////////////////////////
		//** HotReloadResourceManager

		// Work in progress: file paths and reloading aren't written yet, and both assert.
		class HotReloadResourceManager : public ResourceManager
		{
		public:
			HotReloadResourceManager()
			{
				StartWatcher();
			}

			~HotReloadResourceManager()
			{
				StopWatcher();
			}

			void StartWatcher()
			{
				if (WatcherThread.joinable())
				{
					return;
				}

				bRunning = true;
				WatcherThread = std::thread([this]()
					{
						DoWatch();
					});
			}

			void StopWatcher()
			{
				bRunning = false;
				if (WatcherThread.joinable())
				{
					WatcherThread.join();
				}
			}

			template<typename T>
			ResourceHandle<T> Load(const std::string& resourceId)
			{
				auto handle = ResourceManager::Load<T>(resourceId);

				// Store file timestamp
				std::string filePath = GetFilePath<T>(resourceId);
				std::error_code error;
				const auto timestamp = std::filesystem::last_write_time(filePath, error);
				if (!error)
				{
					std::lock_guard<std::mutex> lock(TimestampMutex);
					FileTimestamps[filePath] = timestamp;
				}

				return handle;
			}

		private:
			template<typename T>
			std::string GetFilePath(const std::string& resourceId)
			{
				ASSERT(false && "This is still WIP! Directories and Types not fixed yet!");

				if constexpr (std::is_same_v<T, Nyx::Texture>)
				{
					return "textures/" + resourceId + ".ktx";
				}
				else if constexpr (std::is_same_v<T, Nyx::Mesh>)
				{
					return "models/" + resourceId + ".gltf";
				}
				else
				{
					return "";
				}
			}

			void DoWatch()
			{
				while (bRunning)
				{
					// check for file changes
					{
						std::lock_guard<std::mutex> lock(TimestampMutex);
						for (auto& [filePath, timestamp] : FileTimestamps)
						{
							std::error_code error;
							const auto currentTimestamp = std::filesystem::last_write_time(filePath, error);
							if (!error && currentTimestamp != timestamp)
							{
								ReloadResource(filePath);
								timestamp = currentTimestamp;
							}
						}
					}

					// sleep to avoid high CPU usage
					std::this_thread::sleep_for(std::chrono::seconds(1));
				}
			}

			void ReloadResource(const std::string& filePath)
			{
				ASSERT(false && "Not implemented yet!");
				// extract resource id and type from file path
				// reload resource
				// ...
			}

		private:
			std::mutex TimestampMutex;
			std::unordered_map<std::string, std::filesystem::file_time_type> FileTimestamps;
			std::thread WatcherThread;
			std::atomic<bool> bRunning = false;
		};
	}

} // Engine
