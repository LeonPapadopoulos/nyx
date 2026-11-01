#pragma once

#include "NyxEngineAPI.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Nyx
{
	namespace Detail
	{
		struct ProgressState
		{
			std::string Name;
			double Started = 0.0;
			std::atomic<uint64_t> Done = 0;
			std::atomic<uint64_t> Total = 0;
		};
	}

	// A task as the tracker last heard of it
	struct ProgressReport
	{
		std::string Name;
		uint64_t Done = 0;
		uint64_t Total = 0;
		// When it started, in seconds of the steady clock (GetProgressClock())
		double Started = 0.0;

		// Done of the total, from 0 to 1; 0 while the total is 0
		float GetFraction() const;
		uint64_t GetLeft() const { return Total > Done ? Total - Done : 0; }
	};

	// One task while it lasts, e.g. a scene being opened or a scatter being made: it ends when the
	// last copy of its handle goes. Advance it from any thread. An empty handle does nothing.
	class NYXENGINE_API ProgressTask
	{
	public:
		ProgressTask() = default;

		bool IsValid() const { return State != nullptr; }
		void Advance(uint64_t count = 1);
		void SetDone(uint64_t done);
		void SetTotal(uint64_t total);

	private:
		friend class ProgressTracker;

		explicit ProgressTask(std::shared_ptr<Detail::ProgressState> state) : State(std::move(state)) {}

		std::shared_ptr<Detail::ProgressState> State;
	};

	// Long work going on, by name, for a progress bar: the editor's status bar shows it, and a game
	// may for a loading screen. Tasks are started, advanced and ended from any thread. Each counts in
	// units of its own, e.g. cells or entities, so tasks are compared by how many of theirs are left.
	class NYXENGINE_API ProgressTracker
	{
	public:
		// The program's tracker
		static ProgressTracker& Get();

		// A task named `name` with `total` steps to go, none done yet
		ProgressTask Start(std::string name, uint64_t total);

		// The tasks going on, in the order they started
		std::vector<ProgressReport> GetReports() const;

	private:
		mutable std::mutex Lock;
		// Pruned of the ended ones as they are reported
		mutable std::vector<std::weak_ptr<Detail::ProgressState>> Tasks;
	};

	// The steady clock in seconds, what reports' start times are in
	NYXENGINE_API double GetProgressClock();
}
