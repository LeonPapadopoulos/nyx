#include "JobSystem.h"

#include "Assertions.h"
#include "Profiler.h"

#include <algorithm>
#include <string>

#if defined(_M_X64) || defined(__x86_64__)
	#include <xmmintrin.h>
#endif

#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <windows.h>
#endif

namespace Nyx
{
	namespace Detail
	{
		struct JobState
		{
			std::atomic<EJobState> State = EJobState::Queued;
			std::function<void()> Work;
			JobGroup* Group = nullptr;
		};
	}

	namespace
	{
		// The floating-point control state of the calling thread: SSE's MXCSR (rounding,
		// flush-to-zero, denormals-are-zero, exception masks) on x64, nothing elsewhere
		uint32_t GetFloatingPointState()
		{
#if defined(_M_X64) || defined(__x86_64__)
			return _mm_getcsr();
#else
			return 0;
#endif
		}

		void SetFloatingPointState([[maybe_unused]] uint32_t state)
		{
#if defined(_M_X64) || defined(__x86_64__)
			_mm_setcsr(state);
#endif
		}

		// For the debugger and the profiler
		void NameThread([[maybe_unused]] uint32_t index)
		{
			const std::string name = "Nyx Worker " + std::to_string(index);
#if defined(_WIN32)
			const std::wstring wideName(name.begin(), name.end());
			SetThreadDescription(GetCurrentThread(), wideName.c_str());
#endif
			NYX_PROFILE_THREAD(name.c_str());
		}

		// A notch below the threads that made the job system: where every core has a worker, the main
		// thread's frame still gets its core whenever it is ready to run, rather than waiting its turn
		// behind the background work
		void LowerThreadPriority()
		{
#if defined(_WIN32)
			SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
		}
	}

	JobGroup::~JobGroup()
	{
		ASSERT(GetPending() == 0 && "A job group went while jobs of it were queued or running");
	}

	void JobGroup::Add()
	{
		Pending.fetch_add(1, std::memory_order_relaxed);
	}

	void JobGroup::Finish()
	{
		if (Pending.fetch_sub(1, std::memory_order_acq_rel) == 1)
		{
			Pending.notify_all();
		}
	}

	EJobState JobHandle::GetState() const
	{
		return State != nullptr ? State->State.load(std::memory_order_acquire) : EJobState::Queued;
	}

	bool JobHandle::IsOver() const
	{
		const EJobState state = GetState();
		return state == EJobState::Done || state == EJobState::Cancelled;
	}

	uint32_t JobSystem::GetDefaultWorkerCount()
	{
		const uint32_t cores = std::thread::hardware_concurrency();
		return cores > 1 ? cores - 1 : 1;
	}

	JobSystem& JobSystem::Get()
	{
		static JobSystem jobs;
		return jobs;
	}

	JobSystem::JobSystem(uint32_t workerCount)
	{
		const uint32_t count = workerCount != 0 ? workerCount : GetDefaultWorkerCount();
		const uint32_t floatingPointState = GetFloatingPointState();

		// Held while the workers start, so none takes a job before every worker's id is known
		std::lock_guard lock(Lock);
		Workers.reserve(count);
		WorkerIds.reserve(count);
		for (uint32_t index = 0; index < count; ++index)
		{
			Workers.emplace_back(&JobSystem::WorkerMain, this, index, floatingPointState);
			WorkerIds.push_back(Workers.back().get_id());
		}
	}

	JobSystem::~JobSystem()
	{
		{
			std::lock_guard lock(Lock);
			bStopping = true;
			for (std::deque<Entry>& queue : Queues)
			{
				for (Entry& entry : queue)
				{
					// Jobs without a handle belong to code that waits for them; none should be left
					ASSERT(entry.State != nullptr && "A job system went with jobs of its owners' still queued");
					EJobState queued = EJobState::Queued;
					if (entry.State != nullptr &&
						entry.State->State.compare_exchange_strong(queued, EJobState::Cancelled, std::memory_order_acq_rel))
					{
						Cancelled.fetch_add(1, std::memory_order_relaxed);
						if (entry.State->Group != nullptr)
						{
							entry.State->Group->Finish();
						}
					}
				}
				queue.clear();
			}
		}
		WorkAdded.notify_all();

		for (std::thread& worker : Workers)
		{
			worker.join();
		}
	}

	bool JobSystem::IsWorkerThread() const
	{
		const std::thread::id self = std::this_thread::get_id();
		return std::find(WorkerIds.begin(), WorkerIds.end(), self) != WorkerIds.end();
	}

	JobHandle JobSystem::Schedule(std::function<void()> work, EJobPriority priority, JobGroup* group)
	{
		auto state = std::make_shared<Detail::JobState>();
		state->Work = std::move(work);
		state->Group = group;
		if (group != nullptr)
		{
			group->Add();
		}

		{
			std::lock_guard lock(Lock);
			ASSERT(!bStopping && "A job was scheduled on a job system that is going");
			Queues[static_cast<uint32_t>(priority)].push_back(Entry{ .State = state });
		}
		WorkAdded.notify_one();
		return JobHandle(std::move(state));
	}

	void JobSystem::Submit(std::span<const RawJob> jobs, EJobPriority priority)
	{
		if (jobs.empty())
		{
			return;
		}

		{
			std::lock_guard lock(Lock);
			ASSERT(!bStopping && "Jobs were submitted to a job system that is going");
			std::deque<Entry>& queue = Queues[static_cast<uint32_t>(priority)];
			for (const RawJob& job : jobs)
			{
				queue.push_back(Entry{ .Job = job });
			}
		}

		if (jobs.size() == 1)
		{
			WorkAdded.notify_one();
		}
		else
		{
			WorkAdded.notify_all();
		}
	}

	bool JobSystem::Cancel(const JobHandle& job)
	{
		if (job.State == nullptr)
		{
			return false;
		}

		EJobState queued = EJobState::Queued;
		if (!job.State->State.compare_exchange_strong(queued, EJobState::Cancelled, std::memory_order_acq_rel))
		{
			return false;
		}

		// No worker touches the work of a cancelled job, so what it holds can go now; its entry is
		// skipped when a worker reaches it
		job.State->Work = nullptr;
		Cancelled.fetch_add(1, std::memory_order_relaxed);
		if (job.State->Group != nullptr)
		{
			job.State->Group->Finish();
		}
		return true;
	}

	void JobSystem::Wait(const JobGroup& group) const
	{
		ASSERT(!IsWorkerThread() && "A worker waited for jobs, which would hold up one of them");
		uint32_t pending = group.Pending.load(std::memory_order_acquire);
		while (pending != 0)
		{
			group.Pending.wait(pending, std::memory_order_acquire);
			pending = group.Pending.load(std::memory_order_acquire);
		}
	}

	void JobSystem::ParallelFor(size_t count, const std::function<void(size_t)>& part, EJobPriority priority, uint32_t maxHelpers)
	{
		const size_t helpers = std::min<size_t>({ count > 0 ? count - 1 : 0, Workers.size(), maxHelpers });
		if (helpers == 0)
		{
			for (size_t index = 0; index < count; ++index)
			{
				part(index);
			}
			return;
		}

		// The parts not taken yet, and those done: a helper reads the part only once it has taken an
		// index below the count, which the caller waits for, so it never outlives the call
		struct Shared
		{
			std::atomic<size_t> Next = 0;
			std::atomic<size_t> Done = 0;
			const std::function<void(size_t)>* Part = nullptr;
			size_t Count = 0;
		};
		auto shared = std::make_shared<Shared>();
		shared->Part = &part;
		shared->Count = count;
		auto take = [](Shared& state)
		{
			for (size_t index = state.Next.fetch_add(1, std::memory_order_relaxed); index < state.Count;
				 index = state.Next.fetch_add(1, std::memory_order_relaxed))
			{
				(*state.Part)(index);
				state.Done.fetch_add(1, std::memory_order_release);
			}
		};
		for (size_t helper = 0; helper < helpers; ++helper)
		{
			Schedule([shared, take] { take(*shared); }, priority);
		}
		take(*shared);
		while (shared->Done.load(std::memory_order_acquire) < count)
		{
			std::this_thread::yield();
		}
	}

	JobSystemStats JobSystem::GetStats() const
	{
		JobSystemStats stats;
		{
			std::lock_guard lock(Lock);
			for (uint32_t priority = 0; priority < JobPriorityCount; ++priority)
			{
				stats.Queued[priority] = static_cast<uint32_t>(Queues[priority].size());
			}
		}
		stats.Running = Running.load(std::memory_order_relaxed);
		stats.Finished = Finished.load(std::memory_order_relaxed);
		stats.Cancelled = Cancelled.load(std::memory_order_relaxed);
		return stats;
	}

	bool JobSystem::Take(Entry& outEntry)
	{
		std::unique_lock lock(Lock);
		for (;;)
		{
			for (std::deque<Entry>& queue : Queues)
			{
				if (!queue.empty())
				{
					outEntry = std::move(queue.front());
					queue.pop_front();
					return true;
				}
			}
			if (bStopping)
			{
				return false;
			}
			WorkAdded.wait(lock);
		}
	}

	void JobSystem::SetBeforeEachJob(std::function<void()> before)
	{
		std::lock_guard lock(Lock);
		BeforeEachJobs.push_back(std::make_unique<std::function<void()>>(std::move(before)));
		BeforeEachJob.store(BeforeEachJobs.back().get(), std::memory_order_release);
	}

	void JobSystem::WorkerMain(uint32_t index, uint32_t floatingPointState)
	{
		SetFloatingPointState(floatingPointState);
		NameThread(index);
		LowerThreadPriority();

		Entry entry;
		while (Take(entry))
		{
			if (entry.State == nullptr)
			{
				Running.fetch_add(1, std::memory_order_relaxed);
				entry.Job.Run(entry.Job.Data);
				Running.fetch_sub(1, std::memory_order_relaxed);
				Finished.fetch_add(1, std::memory_order_relaxed);
				continue;
			}

			Detail::JobState& state = *entry.State;
			EJobState queued = EJobState::Queued;
			if (!state.State.compare_exchange_strong(queued, EJobState::Running, std::memory_order_acq_rel))
			{
				// Cancelled while it waited
				entry.State.reset();
				continue;
			}

			Running.fetch_add(1, std::memory_order_relaxed);
			if (const std::function<void()>* before = BeforeEachJob.load(std::memory_order_acquire); before != nullptr && *before)
			{
				(*before)();
			}
			{
				NYX_PROFILE_SCOPE("Job");
				state.Work();
			}
			state.Work = nullptr;
			Running.fetch_sub(1, std::memory_order_relaxed);
			Finished.fetch_add(1, std::memory_order_relaxed);

			JobGroup* group = state.Group;
			state.State.store(EJobState::Done, std::memory_order_release);
			if (group != nullptr)
			{
				group->Finish();
			}
			entry.State.reset();
		}
	}
}
