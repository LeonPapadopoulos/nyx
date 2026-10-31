// The swapchain's present mode for vsync on and off, out of what a surface offers, without a GPU
#include "TestSupport.h"
#include "VulkanSwapchain.h"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
	using namespace Nyx;

	using Nyx::Tests::Require;

	vk::PresentModeKHR Choose(const std::vector<vk::PresentModeKHR>& offered, bool bVSync)
	{
		return VulkanSwapchain::ChooseSwapPresentMode(offered, bVSync);
	}

	void TestPresentModes()
	{
		using enum vk::PresentModeKHR;
		const std::vector<vk::PresentModeKHR> all = { eImmediate, eMailbox, eFifo, eFifoRelaxed };
		Require(Choose(all, true) == eFifo, "Vsync should present in FIFO mode");
		Require(Choose(all, false) == eMailbox, "Without vsync, mailbox where there is one");
		Require(Choose({ eFifo, eFifoRelaxed, eImmediate }, false) == eImmediate, "Without vsync and without a mailbox, immediate");
		Require(Choose({ eFifo }, false) == eFifo, "With FIFO alone, FIFO either way");
	}
}

int main()
{
	try
	{
		TestPresentModes();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Present mode test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All present mode tests passed.\n";
	return 0;
}
