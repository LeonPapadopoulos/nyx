// ResourceManager and AsyncResourceManager with small stand-in resources that count their loads.
// Nothing in the engine uses the managers yet, so these tests are the only code that compiles them.
#include "ResourceManager.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using namespace Nyx::Engine;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	// Counts loads and unloads across all instances, so a test can see what the manager did.
	struct LoadCounts
	{
		int Loads = 0;
		int Unloads = 0;
	};

	template<typename Tag>
	class CountingResource : public Resource
	{
	public:
		explicit CountingResource(const std::string& id)
			: Resource(id)
		{
		}

		static inline LoadCounts Counts;

	protected:
		// An id starting with "missing" fails to load, as a missing file would.
		bool DoLoad() override
		{
			if (GetId().rfind("missing", 0) == 0)
			{
				return false;
			}
			++Counts.Loads;
			return true;
		}

		bool DoUnload() override
		{
			++Counts.Unloads;
			return true;
		}
	};

	struct MeshTag;
	struct TextureTag;
	using TestMesh = CountingResource<MeshTag>;
	using TestTexture = CountingResource<TextureTag>;

	void ResetCounts()
	{
		TestMesh::Counts = {};
		TestTexture::Counts = {};
	}

	void TestLoadTwiceLoadsOnce()
	{
		ResetCounts();
		ResourceManager manager;

		ResourceHandle<TestMesh> first = manager.Load<TestMesh>("cube");
		ResourceHandle<TestMesh> second = manager.Load<TestMesh>("cube");

		Require(first.IsValid() && second.IsValid(), "Both handles should be valid");
		Require(first.Get() == second.Get() && first.Get() != nullptr, "Both handles should point at the same resource");
		Require(first->GetId() == "cube", "The resource should keep its id");
		Require(TestMesh::Counts.Loads == 1, "Loading the same id twice should load it once");
	}

	void TestHasResourceChecksTheId()
	{
		ResetCounts();
		ResourceManager manager;
		manager.Load<TestMesh>("cube");

		Require(manager.HasResource<TestMesh>("cube"), "A loaded resource should be found");
		Require(!manager.HasResource<TestMesh>("sphere"), "An id that was never loaded should not be found");
		Require(!manager.HasResource<TestTexture>("cube"), "The same id of another type should not be found");
		Require(!ResourceHandle<TestMesh>("sphere", &manager).IsValid(), "A handle to an id that was never loaded should be invalid");
	}

	void TestFailedLoadStoresNothing()
	{
		ResetCounts();
		ResourceManager manager;

		ResourceHandle<TestMesh> handle = manager.Load<TestMesh>("missing-cube");

		Require(!handle.IsValid() && handle.Get() == nullptr, "A failed load should give an invalid handle");
		Require(!manager.HasResource<TestMesh>("missing-cube"), "A failed load should store nothing");
	}

	void TestReleaseUnloadsAfterLastReference()
	{
		ResetCounts();
		ResourceManager manager;
		ResourceHandle<TestMesh> handle = manager.Load<TestMesh>("cube");
		manager.Load<TestMesh>("cube");

		manager.Release<TestMesh>("cube");
		Require(handle.IsValid() && TestMesh::Counts.Unloads == 0, "The resource should stay while one reference is left");

		manager.Release<TestMesh>("cube");
		Require(!handle.IsValid() && TestMesh::Counts.Unloads == 1, "The last release should unload the resource");

		manager.Release<TestMesh>("cube");
		Require(TestMesh::Counts.Unloads == 1, "Releasing an unloaded resource should do nothing");

		Require(manager.Load<TestMesh>("cube").IsValid() && TestMesh::Counts.Loads == 2, "A released resource should load again");
	}

	void TestReleaseKeepsOtherTypes()
	{
		ResetCounts();
		ResourceManager manager;
		manager.Load<TestMesh>("brick");
		manager.Load<TestTexture>("brick");

		manager.Release<TestTexture>("brick");

		Require(TestTexture::Counts.Unloads == 1, "The released texture should be unloaded");
		Require(TestMesh::Counts.Unloads == 0 && manager.HasResource<TestMesh>("brick"),
			"Releasing a texture should not unload a mesh with the same id");
	}

	void TestUnloadAll()
	{
		ResetCounts();
		ResourceManager manager;
		manager.Load<TestMesh>("cube");
		manager.Load<TestTexture>("brick");

		manager.UnloadAll();

		Require(TestMesh::Counts.Unloads == 1 && TestTexture::Counts.Unloads == 1, "UnloadAll should unload everything");
		Require(!manager.HasResource<TestMesh>("cube") && !manager.HasResource<TestTexture>("brick"), "UnloadAll should store nothing");
	}

	void TestAsyncLoad()
	{
		ResetCounts();
		ResourceManager manager;
		std::promise<bool> loaded;

		{
			AsyncResourceManager asyncManager(manager);
			asyncManager.LoadAsync<TestMesh>("cube", [&loaded](ResourceHandle<TestMesh> handle)
				{
					loaded.set_value(handle.IsValid());
				});

			std::future<bool> result = loaded.get_future();
			Require(result.wait_for(std::chrono::seconds(5)) == std::future_status::ready, "The async load should finish");
			Require(result.get(), "The async load should give a valid handle");
		}

		Require(manager.HasResource<TestMesh>("cube"), "The async load should store the resource in the manager");
	}

	void TestAsyncStopFinishesQueuedLoads()
	{
		ResetCounts();
		ResourceManager manager;
		std::atomic<int> callbacks = 0;

		{
			AsyncResourceManager asyncManager(manager);
			for (int index = 0; index < 20; ++index)
			{
				asyncManager.LoadAsync<TestMesh>("mesh" + std::to_string(index), [&callbacks](ResourceHandle<TestMesh>)
					{
						++callbacks;
					});
			}
		}

		Require(callbacks == 20, "Stopping should finish the loads already queued");
		Require(TestMesh::Counts.Loads == 20, "Every queued load should have run once");
	}
}

int main()
{
	try
	{
		TestLoadTwiceLoadsOnce();
		TestHasResourceChecksTheId();
		TestFailedLoadStoresNothing();
		TestReleaseUnloadsAfterLastReference();
		TestReleaseKeepsOtherTypes();
		TestUnloadAll();
		TestAsyncLoad();
		TestAsyncStopFinishesQueuedLoads();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Resource manager test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All resource manager tests passed.\n";
	return 0;
}
