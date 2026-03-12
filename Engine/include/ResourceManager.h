#pragma once
#include "Assertions.h"

#include <typeindex>
#include <unordered_map>
#include <string>
#include <memory>
#include <thread>
#include <queue>
#include <mutex>
#include <filesystem>

class ResourceManager;

namespace Engine
{
	/////////////////////////////////////////////////////////////////////////////////////////////////
	//** ResourceHandle

	template<typename T>
	class ResourceHandle
	{
	public:
		ResourceHandle()
			: ResourceManager(nullptr)
		{
		}

		ResourceHandle(const std::string& id, ResourceManager* manager)
			: ResourceId(id)
			, ResourceManager(manager)
		{
		}

		T* Get() const
		{
			return ResourceManager ? ResourceManager->GetResource<T>(ResourceId) : nullptr;
		}

		bool IsValid() const
		{
			return ResourceManager && ResourceManager->HasResource<T>(ResourceId);
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
		ResourceManager* ResourceManager;
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

	class ResourceManager
	{
	public:
		template<typename T>
		ResourceHandle<T> Load(const std::string& resourceId)
		{
			static_assert(std::is_base_of<Resource, T>::value, "T must derive from Resource");

			auto& typeResources = Resources[std::type_index(typeid(T))];
			auto it = typeResources.find(resourceId);

			if (it != typeResources.end())
			{
				RefCounts[resourceId]++;
				return ResourceHandle<T>(resourceId, this);
			}

			auto resource = std::make_shared<T>(resourceId);
			if (!resource->Load())
			{
				return ResourceHandle<T>();
			}

			typeResources[resourceId] = resource;
			RefCounts[resourceId] = 1;

			return ResourceHandle<T>(resourceId, this);
		}

		template<typename T>
		T* GetResource(const std::string& resourceId)
		{
			auto& typeResources = Resources[std::type_index(typeid(T))];
			auto it = typeResources.find(resourceId);

			if (it != typeResources.end())
			{
				return static_cast<T*>(it->second.get());
			}

			return nullptr;
		}

		template<typename T>
		bool HasResource(const std::string& resourceId)
		{
			auto resourceIt = Resources.find(std::type_index(typeid(T)));
			return resourceIt != resources.end();
		}

		template<typename T>
		void Release(const std::string& resourceId)
		{
			auto resourceTypeIt = Resources.find(std::type_index(typeid(T)));
			if (resourceTypeIt == Resources.end())
			{
				return;
			}

			auto& refCounts = resourceId->second;
			auto it = refCounts.find(resourceId);
			if (it != refCounts.end())
			{
				it->second--;

				// check if resource has no remaining references
				if (it->second <= 0)
				{
					for (auto& [type, typeResources] : Resources)
					{
						auto resourceIt = typeResources.find(resourceId);
						if (resourceIt != typeResources.end())
						{
							resourceIt->second->Unload();
							typeResources.erase(resourceIt);
							break;
						}
					}

					// Clean up reference counting entry
					refCounts.erase(it);
				}
			}
		}

		void UnloadAll()
		{
			for (auto& [type, typeResources] : Resources)
			{
				for (auto& [id, resource] : typeResources)
				{
					resource->Unload();
				}
				typeResources.clear();
			}
			RefCounts.clear();
		}

	private:
		std::unordered_map<std::type_index, std::unordered_map<std::string, std::shared_ptr<Resource>>> Resources;

		struct ResourceData
		{
			std::shared_ptr<Resource> Resources;
			int RefCount;
		};
		std::unordered_map<std::type_index, std::unordered_map<std::string, ResourceData>> RefCounts;
	};

	/////////////////////////////////////////////////////////////////////////////////////////////////
	//** AsyncResourceManager

	class AsyncResourceManager
	{
	public:
		AsyncResourceManager()
		{
			Start();
		}

		~AsyncResourceManager()
		{
			Stop();
		}

		void Start()
		{
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
			std::lock_guard<std::mutex> lock(QueueMutex);
			TaskQueue.push([this, resourceId, callback]()
				{
					auto handle = ResourceManager->Load<T>(resourceId);
					callback(handle);
				});
			Condition.notify_one();
		}

	private:
		void DoWork()
		{
			while (bRunning)
			{
				std::function<void()> Task;
				{
					std::unique_lock<std::mutex> lock(QueueMutex);
					Condition.wait(lock, [this]()
						{
							return !TaskQueue.empty() || !bRunning;
						});

					if (!bRunning && TaskQueue.empty())
					{
						return;
					}

					Task = std::move(TaskQueue.front());
					TaskQueue.pop();
				}

				Task();
			}
		}

	private:
		ResourceManager* ResourceManager;
		std::thread WorkerThread;
		std::queue<std::function<void()>> TaskQueue;
		std::mutex QueueMutex;
		std::condition_variable Condition;
		bool bRunning = false;
	};

	/////////////////////////////////////////////////////////////////////////////////////////////////
	//** HotReloadResourceManager

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
			std::string filepath = GetFilePath<T>(resourceId);
			try
			{
				FileTimestamps[filePath] = std::filesystem::last_write_time(filepath);
			}
			catch (const std::filesystem::filesystem_error& error)
			{
				// File doesn't exist or can't be accessed
			}

			return handle;
		}

	private:
		template<typename T>
		std::string GetFilePath(const std::string& resourceId)
		{
			ASSERT(false && "This is still WIP! Directories and Types not fixed yet!");

			if constexpr (std::is_same_v<T, Texture>) 
			{
				return "textures/" + resourceId + ".ktx";
			}
			else if constexpr (std::is_same_v<T, Mesh>) 
			{
				return "models/" + resourceId + ".gltf";
			}
			else if constexpr (std::is_same_v<T, Shader>) 
			{
				// Simplified for example
				return "shaders/" + resourceId + ".spv";
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
				for (auto& [filePath, timestamp] : FileTimestamps)
				{
					try
					{
						auto currentTimestamp = std::filesystem::last_write_time(filePath);
						if (currentTimestamp != timestamp)
						{
							ReloadResource(filePath);
							timestamp = currentTimestamp;
						}
					}
					catch (const std::filesystem::filesystem_error& error)
					{
						// file doesn't exist or can't be accessed
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
		std::unordered_map<std::string, std::filesystem::file_time_type> FileTimestamps;
		std::thread WatcherThread;
		bool bRunning = false;
	};

} // Engine
