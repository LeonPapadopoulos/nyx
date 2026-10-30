// The job system (JobSystem): every job runs once, on a worker; higher priorities first, each in
// the order scheduled; cancelling a queued job, not a running or finished one; groups waited for;
// jobs without handles; parts shared out, by jobs too; what is queued when it goes is cancelled;
// workers take the floating-point state of the thread that made it; the physics step runs on its
// workers, the same whatever their number.
#include "JobSystem.h"
#include "Log.h"
#include "PhysicsWorld.h"
#include "TestSupport.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#if defined(_M_X64) || defined(__x86_64__)
	#include <xmmintrin.h>
#endif

namespace
{
	using Nyx::EJobPriority;
	using Nyx::EJobState;
	using Nyx::JobGroup;
	using Nyx::JobHandle;
	using Nyx::JobSystem;
	using Nyx::RawJob;

	using Nyx::Tests::Require;

	// Holds a worker until opened, so the jobs after it wait in the queue
	class Gate
	{
	public:
		void Hold()
		{
			bHolding.store(true);
			while (!bOpen.load())
			{
				std::this_thread::yield();
			}
		}

		// Until the worker is inside Hold()
		void WaitUntilHolding() const
		{
			while (!bHolding.load())
			{
				std::this_thread::yield();
			}
		}

		void Open() { bOpen.store(true); }

	private:
		std::atomic<bool> bHolding = false;
		std::atomic<bool> bOpen = false;
	};

	// A thousand jobs over four workers: each runs once, on a worker, and the group counts them
	void TestEveryJobRunsOnce()
	{
		JobSystem jobs(4);
		Require(jobs.GetWorkerCount() == 4, "The job system should have the workers asked for");
		Require(!jobs.IsWorkerThread(), "The test's thread isn't a worker");

		constexpr int Count = 1000;
		std::vector<std::atomic<int>> runs(Count);
		std::atomic<int> onWorkers = 0;
		JobGroup group;
		for (int i = 0; i < Count; ++i)
		{
			jobs.Schedule(
				[&, i]
				{
					runs[i].fetch_add(1);
					if (jobs.IsWorkerThread())
					{
						onWorkers.fetch_add(1);
					}
				},
				static_cast<EJobPriority>(i % 3), &group);
		}
		jobs.Wait(group);

		Require(group.IsDone(), "The group should be done once waited for");
		for (int i = 0; i < Count; ++i)
		{
			Require(runs[i].load() == 1, "Job " + std::to_string(i) + " ran " + std::to_string(runs[i].load()) + " times, not once");
		}
		Require(onWorkers.load() == Count, "Every job should run on a worker");
		Require(jobs.GetStats().Finished == Count, "The stats should count every job finished");

		// A group waited for takes new jobs
		std::atomic<int> again = 0;
		jobs.Schedule([&] { again.fetch_add(1); }, EJobPriority::Normal, &group);
		jobs.Wait(group);
		Require(again.load() == 1, "A group should take new jobs after it was waited for");
	}

	// With the only worker held, jobs queued in any order run High first, then Normal, then Low,
	// each priority in the order scheduled
	void TestPrioritiesFirst()
	{
		JobSystem jobs(1);
		Gate gate;
		JobGroup group;
		jobs.Schedule([&] { gate.Hold(); }, EJobPriority::Normal, &group);
		gate.WaitUntilHolding();

		std::mutex orderLock;
		std::string order;
		auto record = [&](char name)
		{
			return [&, name]
			{
				std::lock_guard lock(orderLock);
				order += name;
			};
		};
		jobs.Schedule(record('a'), EJobPriority::Low, &group);
		jobs.Schedule(record('b'), EJobPriority::Normal, &group);
		jobs.Schedule(record('c'), EJobPriority::High, &group);
		jobs.Schedule(record('d'), EJobPriority::Low, &group);
		jobs.Schedule(record('e'), EJobPriority::High, &group);
		jobs.Schedule(record('f'), EJobPriority::Normal, &group);

		const Nyx::JobSystemStats queued = jobs.GetStats();
		Require(queued.Queued[0] == 2 && queued.Queued[1] == 2 && queued.Queued[2] == 2, "The stats should count the jobs queued by priority");
		Require(queued.Running == 1, "The stats should count the job running");

		gate.Open();
		jobs.Wait(group);
		Require(order == "cebfad", "Jobs should run by priority, then in order: ran " + order);
	}

	// A queued job cancelled never runs, and its group no longer waits for it; a running or finished
	// job can't be cancelled, nor an empty handle
	void TestCancel()
	{
		JobSystem jobs(1);
		Gate gate;
		JobGroup group;
		const JobHandle holding = jobs.Schedule([&] { gate.Hold(); }, EJobPriority::Normal, &group);
		gate.WaitUntilHolding();

		std::atomic<int> runs = 0;
		const JobHandle queued = jobs.Schedule([&] { runs.fetch_add(1); }, EJobPriority::High, &group);
		const JobHandle kept = jobs.Schedule([&] { runs.fetch_add(10); }, EJobPriority::High, &group);
		Require(queued.GetState() == EJobState::Queued, "A job behind the held one should be queued");
		Require(group.GetPending() == 3, "The group should count its three jobs");

		Require(jobs.Cancel(queued), "A queued job should be cancelled");
		Require(queued.GetState() == EJobState::Cancelled && queued.IsOver(), "A cancelled job should say so");
		Require(group.GetPending() == 2, "The group shouldn't wait for a cancelled job");
		Require(!jobs.Cancel(queued), "A job can only be cancelled once");
		Require(holding.GetState() == EJobState::Running, "The held job should be running");
		Require(!jobs.Cancel(holding), "A running job can't be cancelled");
		Require(!jobs.Cancel(JobHandle{}), "An empty handle cancels nothing");

		gate.Open();
		jobs.Wait(group);
		Require(runs.load() == 10, "Only the job not cancelled should run");
		Require(kept.GetState() == EJobState::Done, "The job not cancelled should be done");
		Require(!jobs.Cancel(kept), "A finished job can't be cancelled");
		Require(jobs.GetStats().Cancelled == 1, "The stats should count one job cancelled");
	}

	// Jobs without handles run on the workers, by their own function and data
	void TestRawJobs()
	{
		JobSystem jobs(3);
		std::atomic<int> sum = 0;
		struct Work
		{
			std::atomic<int>* Sum;
			int Value;
		};
		std::vector<Work> work;
		for (int i = 1; i <= 100; ++i)
		{
			work.push_back(Work{ &sum, i });
		}
		std::vector<RawJob> raw;
		for (Work& item : work)
		{
			raw.push_back(RawJob{ .Run = [](void* data)
				{
					const Work& item = *static_cast<Work*>(data);
					item.Sum->fetch_add(item.Value);
				},
				.Data = &item });
		}
		jobs.Submit(raw, EJobPriority::Low);

		const auto start = std::chrono::steady_clock::now();
		while (sum.load() != 5050)
		{
			Require(std::chrono::steady_clock::now() - start < std::chrono::seconds(10), "Raw jobs should all run");
			std::this_thread::yield();
		}
	}

	// A job system that goes cancels what hasn't started and waits for what is running
	void TestGoingCancelsQueued()
	{
		std::atomic<int> runs = 0;
		std::vector<JobHandle> handles;
		JobGroup group;
		std::atomic<bool> started = false;
		{
			JobSystem jobs(1);
			jobs.Schedule(
				[&]
				{
					started.store(true);
					std::this_thread::sleep_for(std::chrono::milliseconds(50));
					runs.fetch_add(100);
				},
				EJobPriority::Normal, &group);
			while (!started.load())
			{
				std::this_thread::yield();
			}
			for (int i = 0; i < 10; ++i)
			{
				handles.push_back(jobs.Schedule([&] { runs.fetch_add(1); }, EJobPriority::Normal, &group));
			}
		}

		Require(runs.load() == 100, "The running job should finish and the queued ones never run");
		Require(group.IsDone(), "The group should wait for nothing once its job system went");
		for (const JobHandle& handle : handles)
		{
			Require(handle.GetState() == EJobState::Cancelled, "Jobs queued when the job system went should be cancelled");
		}
	}

	// Workers start with the floating-point state of the thread that made the job system, so a job
	// rounds as that thread does
	void TestWorkersTakeTheFloatingPointState()
	{
#if defined(_M_X64) || defined(__x86_64__)
		const uint32_t original = _mm_getcsr();
		// Flush-to-zero, denormals-are-zero and rounding towards zero: nothing a thread has by default
		const uint32_t changed = (original & ~_MM_ROUND_MASK) | _MM_ROUND_TOWARD_ZERO | _MM_FLUSH_ZERO_ON | 0x0040u;
		_mm_setcsr(changed);
		uint32_t seen = 0;
		{
			JobSystem jobs(2);
			_mm_setcsr(original);
			JobGroup group;
			jobs.Schedule([&] { seen = _mm_getcsr(); }, EJobPriority::Normal, &group);
			jobs.Wait(group);
		}
		Require(seen == changed, "A worker should have the floating-point state of the thread that made its job system");
#endif
	}

	// The physics step runs on a job system's workers (they finish its jobs), and a world gives the
	// same poses, to the bit, on one worker as on several
	void TestPhysicsOnTheWorkers()
	{
		auto simulate = [](JobSystem& jobs)
		{
			Nyx::PhysicsSettings settings;
			settings.Jobs = &jobs;
			Nyx::PhysicsWorld world(settings);

			Nyx::BodyDesc floor;
			floor.Shape.HalfExtents = glm::vec3(20.0f, 0.5f, 20.0f);
			floor.Position = glm::vec3(0.0f, -0.5f, 0.0f);
			floor.Motion = Nyx::EBodyMotion::Static;
			world.CreateBody(floor);

			std::vector<Nyx::BodyHandle> boxes;
			for (int i = 0; i < 200; ++i)
			{
				Nyx::BodyDesc box;
				box.Position = glm::vec3(float(i % 5) * 1.1f - 2.5f, 1.0f + float(i / 25) * 1.1f, float((i / 5) % 5) * 1.1f - 2.5f);
				boxes.push_back(world.CreateBody(box));
			}
			for (int step = 0; step < 120; ++step)
			{
				world.Step(1.0f / 60.0f);
			}

			std::vector<Nyx::BodyPose> poses;
			for (const Nyx::BodyHandle box : boxes)
			{
				poses.push_back(world.GetPose(box));
			}
			return poses;
		};

		JobSystem one(1);
		JobSystem several(6);
		const std::vector<Nyx::BodyPose> onOne = simulate(one);
		const uint64_t finishedBefore = several.GetStats().Finished;
		const std::vector<Nyx::BodyPose> onSeveral = simulate(several);

		Require(several.GetStats().Finished > finishedBefore, "The physics step's jobs should run on the job system's workers");
		Require(onOne.size() == onSeveral.size(), "Both worlds should have the same boxes");
		bool bMoved = false;
		for (size_t i = 0; i < onOne.size(); ++i)
		{
			Require(std::memcmp(&onOne[i], &onSeveral[i], sizeof(Nyx::BodyPose)) == 0,
				"Box " + std::to_string(i) + " should be where it is with one worker, to the bit");
			bMoved = bMoved || onOne[i].Position.y < 1.0f + float(i / 25) * 1.1f - 0.01f;
		}
		Require(bMoved, "The boxes should have fallen");
	}

	// Parts shared out: every part runs once, the caller taking some; jobs on every worker sharing
	// out their own parts at once all finish, as none waits for a queued job
	void TestParallelFor()
	{
		JobSystem jobs(4);
		constexpr size_t Parts = 1000;
		std::vector<std::atomic<int>> runs(Parts);
		std::atomic<int> here = 0;
		const std::thread::id caller = std::this_thread::get_id();
		jobs.ParallelFor(Parts, [&](size_t part)
			{
				runs[part].fetch_add(1);
				if (std::this_thread::get_id() == caller)
				{
					here.fetch_add(1);
				}
			});
		for (size_t part = 0; part < Parts; ++part)
		{
			Require(runs[part].load() == 1, "Part " + std::to_string(part) + " ran " + std::to_string(runs[part].load()) + " times, not once");
		}
		Require(here.load() > 0, "The caller should take parts too");

		// A job on every worker, each sharing out its own parts
		JobGroup group;
		std::atomic<int> sum = 0;
		for (int job = 0; job < 8; ++job)
		{
			jobs.Schedule(
				[&]
				{
					jobs.ParallelFor(64, [&](size_t) { sum.fetch_add(1); });
				},
				EJobPriority::Normal, &group);
		}
		jobs.Wait(group);
		Require(sum.load() == 8 * 64, "Jobs sharing out their parts at once should all finish, every part once");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	try
	{
		TestEveryJobRunsOnce();
		TestPrioritiesFirst();
		TestCancel();
		TestRawJobs();
		TestParallelFor();
		TestGoingCancelsQueued();
		TestWorkersTakeTheFloatingPointState();
		TestPhysicsOnTheWorkers();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Job system test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All job system tests passed.\n";
	return 0;
}
