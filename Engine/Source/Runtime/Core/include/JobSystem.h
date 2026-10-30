#pragma once

#include "NyxEngineAPI.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <thread>
#include <vector>

namespace Nyx
{
	// Which jobs a worker takes first: every High job queued before any Normal one, every Normal one
	// before any Low one. Within a priority, in the order they were scheduled.
	enum class EJobPriority : uint8_t
	{
		// What a frame or a step is waiting for, such as the physics step: short jobs only
		High,
		// What is wanted soon, such as the ground near a player
		Normal,
		// Work in the background, such as the far distance
		Low
	};

	constexpr uint32_t JobPriorityCount = 3;

	enum class EJobState : uint8_t
	{
		Queued,
		Running,
		Done,
		// Cancelled before it started: it never runs
		Cancelled
	};

	namespace Detail
	{
		struct JobState;
	}

	// Counts the jobs scheduled into it that haven't finished or been cancelled, so a caller can wait
	// for all of them (JobSystem::Wait()). It must outlive its jobs: wait for it, or cancel them,
	// before it goes. Waited for, it can take new jobs again.
	class NYXENGINE_API JobGroup
	{
	public:
		JobGroup() = default;
		~JobGroup();
		JobGroup(const JobGroup&) = delete;
		JobGroup& operator=(const JobGroup&) = delete;

		// Jobs not finished yet
		uint32_t GetPending() const { return Pending.load(std::memory_order_acquire); }
		bool IsDone() const { return GetPending() == 0; }

	private:
		friend class JobSystem;

		void Add();
		void Finish();

		std::atomic<uint32_t> Pending = 0;
	};

	// One scheduled job, to ask how it is or to cancel it. A copy refers to the same job; an empty one
	// to none.
	class NYXENGINE_API JobHandle
	{
	public:
		JobHandle() = default;

		bool IsValid() const { return State != nullptr; }
		// Queued for an empty handle
		EJobState GetState() const;
		// Finished or cancelled: it will run no more
		bool IsOver() const;

	private:
		friend class JobSystem;

		explicit JobHandle(std::shared_ptr<Detail::JobState> state) : State(std::move(state)) {}

		std::shared_ptr<Detail::JobState> State;
	};

	// A job without a handle, for code that keeps its own job objects, such as Jolt's: `Run(Data)` on a
	// worker. It can't be cancelled or waited for here; its owner does that.
	struct RawJob
	{
		void (*Run)(void* data) = nullptr;
		void* Data = nullptr;
	};

	// What a job system has done, for the frame stats and the tests
	struct JobSystemStats
	{
		uint32_t Queued[JobPriorityCount] = {};
		uint32_t Running = 0;
		uint64_t Finished = 0;
		uint64_t Cancelled = 0;
	};

	// Worker threads that run jobs, highest priority first. The program has one
	// (JobSystem::Get()), so everything that runs in the background shares the same cores: the
	// physics step, and later generation and loading. A job may run on any worker, at any time
	// after it was scheduled; it must not wait for another job (a worker waiting holds up the
	// others), and touches only what its scheduler gave it.
	//
	// Every worker starts with the floating-point state (rounding, denormals) of the thread that made
	// the job system, so a job gives the same numbers whichever thread runs it, and the same as on
	// that thread. Workers run a notch below normal priority (on Windows), so the main thread gets a
	// core whenever it is ready, even with a worker on every core.
	class NYXENGINE_API JobSystem
	{
	public:
		// One a core but the caller's, at least one
		static uint32_t GetDefaultWorkerCount();

		// The program's job system, made with the default number of workers on first use, from
		// whichever thread asks first: the main thread, in practice. It stops when the program ends.
		static JobSystem& Get();

		// `workerCount` 0: the default
		explicit JobSystem(uint32_t workerCount = 0);
		// Cancels what hasn't started, and waits for what is running
		~JobSystem();
		JobSystem(const JobSystem&) = delete;
		JobSystem& operator=(const JobSystem&) = delete;

		uint32_t GetWorkerCount() const { return static_cast<uint32_t>(Workers.size()); }
		// Whether the calling thread is one of this job system's workers
		bool IsWorkerThread() const;

		// Queues `work`, in `group` if given (which counts it until it is finished or cancelled)
		JobHandle Schedule(std::function<void()> work, EJobPriority priority = EJobPriority::Normal, JobGroup* group = nullptr);

		// Queues jobs without handles, all under one lock
		void Submit(std::span<const RawJob> jobs, EJobPriority priority);

		// Takes a queued job out, so it never runs; true if it did, false if it had started, finished
		// or been cancelled already
		bool Cancel(const JobHandle& job);

		// Returns once every job of the group has finished or been cancelled. Not from a worker: a
		// worker waiting for others would hold one of them up.
		void Wait(const JobGroup& group) const;

		// Runs `part(index)` for every index below `count`, each once, here and on workers free to
		// help, and returns once all have run. The caller takes parts too and only ever waits for
		// parts already running elsewhere, never for a job still queued, so a job may call it: helpers
		// that start after the parts are gone return at once. At most `maxHelpers` workers help.
		void ParallelFor(size_t count, const std::function<void(size_t)>& part, EJobPriority priority = EJobPriority::High, uint32_t maxHelpers = UINT32_MAX);

		JobSystemStats GetStats() const;

	private:
		struct Entry
		{
			RawJob Job;
			// For jobs with a handle: whether it still runs, and its group
			std::shared_ptr<Detail::JobState> State;
		};

		void WorkerMain(uint32_t index, uint32_t floatingPointState);
		// The next job to run, highest priority first; false once stopping with nothing to run
		bool Take(Entry& outEntry);

		mutable std::mutex Lock;
		std::condition_variable WorkAdded;
		std::deque<Entry> Queues[JobPriorityCount];
		bool bStopping = false;

		std::vector<std::thread> Workers;
		std::vector<std::thread::id> WorkerIds;

		std::atomic<uint32_t> Running = 0;
		std::atomic<uint64_t> Finished = 0;
		std::atomic<uint64_t> Cancelled = 0;
	};
}
