#include "NyxPCH.h"
#include "Progress.h"

#include <algorithm>
#include <chrono>

namespace Nyx
{
	float ProgressReport::GetFraction() const
	{
		return Total > 0 ? static_cast<float>(static_cast<double>(std::min(Done, Total)) / static_cast<double>(Total)) : 0.0f;
	}

	void ProgressTask::Advance(uint64_t count)
	{
		if (State)
		{
			State->Done.fetch_add(count, std::memory_order_relaxed);
		}
	}

	void ProgressTask::SetDone(uint64_t done)
	{
		if (State)
		{
			State->Done.store(done, std::memory_order_relaxed);
		}
	}

	void ProgressTask::SetTotal(uint64_t total)
	{
		if (State)
		{
			State->Total.store(total, std::memory_order_relaxed);
		}
	}

	ProgressTracker& ProgressTracker::Get()
	{
		static ProgressTracker tracker;
		return tracker;
	}

	ProgressTask ProgressTracker::Start(std::string name, uint64_t total)
	{
		auto state = std::make_shared<Detail::ProgressState>();
		state->Name = std::move(name);
		state->Started = GetProgressClock();
		state->Total.store(total, std::memory_order_relaxed);
		std::lock_guard lock(Lock);
		Tasks.push_back(state);
		return ProgressTask(std::move(state));
	}

	std::vector<ProgressReport> ProgressTracker::GetReports() const
	{
		std::vector<ProgressReport> reports;
		std::lock_guard lock(Lock);
		std::erase_if(Tasks, [](const std::weak_ptr<Detail::ProgressState>& task) { return task.expired(); });
		for (const std::weak_ptr<Detail::ProgressState>& task : Tasks)
		{
			if (const std::shared_ptr<Detail::ProgressState> state = task.lock())
			{
				reports.push_back(ProgressReport{ .Name = state->Name,
					.Done = state->Done.load(std::memory_order_relaxed),
					.Total = state->Total.load(std::memory_order_relaxed),
					.Started = state->Started });
			}
		}
		return reports;
	}

	double GetProgressClock()
	{
		return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
	}
}
