// The progress tracker (Progress.h): a task is reported from its start, with its name, total and
// what is done, until the last copy of its handle goes; tasks in the order they started; advanced
// from many threads at once, every step counted; an empty handle does nothing.
#include "JobSystem.h"
#include "Log.h"
#include "Progress.h"
#include "TestSupport.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::ProgressReport;
	using Nyx::ProgressTask;
	using Nyx::ProgressTracker;
	using Nyx::Tests::Require;

	const ProgressReport* Find(const std::vector<ProgressReport>& reports, const std::string& name)
	{
		for (const ProgressReport& report : reports)
		{
			if (report.Name == name)
			{
				return &report;
			}
		}
		return nullptr;
	}

	void TestATaskIsReportedWhileItLasts()
	{
		ProgressTracker tracker;
		const double before = Nyx::GetProgressClock();
		{
			ProgressTask task = tracker.Start("Opening", 40);
			std::vector<ProgressReport> reports = tracker.GetReports();
			Require(reports.size() == 1 && reports[0].Name == "Opening" && reports[0].Total == 40 && reports[0].Done == 0,
				"A task started should be reported with its name and total, none of it done");
			Require(reports[0].Started >= before && reports[0].Started <= Nyx::GetProgressClock(), "A task should say when it started");
			Require(reports[0].GetFraction() == 0.0f && reports[0].GetLeft() == 40, "A task just started should have all of it left");

			task.Advance(10);
			task.Advance();
			reports = tracker.GetReports();
			Require(reports[0].Done == 11 && reports[0].GetLeft() == 29, "A task advanced should report what is done");
			task.SetDone(30);
			task.SetTotal(60);
			reports = tracker.GetReports();
			Require(reports[0].Done == 30 && reports[0].Total == 60 && reports[0].GetFraction() == 0.5f, "A task's done and total should be set");

			// A copy keeps it going after the first handle goes
			ProgressTask copy = task;
			task = ProgressTask();
			Require(tracker.GetReports().size() == 1, "A task should last while a copy of its handle does");
			copy.Advance(100);
			Require(tracker.GetReports()[0].GetFraction() == 1.0f && tracker.GetReports()[0].GetLeft() == 0, "A task done past its total should be all done");
		}
		Require(tracker.GetReports().empty(), "A task should end when its last handle goes");
	}

	void TestTasksInTheOrderTheyStarted()
	{
		ProgressTracker tracker;
		ProgressTask first = tracker.Start("First", 1);
		ProgressTask second = tracker.Start("Second", 2);
		ProgressTask third = tracker.Start("Third", 3);
		second = ProgressTask();
		const std::vector<ProgressReport> reports = tracker.GetReports();
		Require(reports.size() == 2 && reports[0].Name == "First" && reports[1].Name == "Third", "The tasks going on should be reported in the order they started");
		Require(Find(reports, "Second") == nullptr, "A task ended should not be reported");
	}

	void TestAdvancedFromManyThreads()
	{
		ProgressTracker tracker;
		Nyx::JobSystem jobs(8);
		ProgressTask task = tracker.Start("Cells", 10000);
		jobs.ParallelFor(10000, [&](size_t) { task.Advance(); });
		Require(tracker.GetReports()[0].Done == 10000, "Every step should be counted, from however many threads");
	}

	void TestAnEmptyHandleDoesNothing()
	{
		ProgressTask empty;
		Require(!empty.IsValid(), "A handle made empty should be empty");
		empty.Advance();
		empty.SetDone(3);
		empty.SetTotal(4);
		Require(ProgressReport{}.GetFraction() == 0.0f, "A report without a total should be none done");
	}
}

int main()
{
	Nyx::Logger::Get().Init();
	try
	{
		TestATaskIsReportedWhileItLasts();
		TestTasksInTheOrderTheyStarted();
		TestAdvancedFromManyThreads();
		TestAnEmptyHandleDoesNothing();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Progress test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All progress tests passed.\n";
	return 0;
}
