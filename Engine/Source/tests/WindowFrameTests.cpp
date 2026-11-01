// A window's place on the screen through GLFW: one with a custom titlebar has no frame around its
// content, so where it is put is where it is, and putting it back where it was moves it nowhere; one
// with Windows' titlebar keeps its frame round the content.
#include "TestSupport.h"

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <Windows.h>

#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
	using Nyx::Tests::Require;

	std::string Describe(const RECT& rect)
	{
		return std::to_string(rect.left) + ", " + std::to_string(rect.top) + " to " + std::to_string(rect.right) + ", " + std::to_string(rect.bottom);
	}

	// Hidden, decorated and resizable, as the editor's and the game's are made
	GLFWwindow* MakeWindow(bool bCustomTitlebar)
	{
		glfwDefaultWindowHints();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
		glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		glfwWindowHint(GLFW_TITLEBAR, bCustomTitlebar ? GLFW_FALSE : GLFW_TRUE);
		GLFWwindow* window = glfwCreateWindow(800, 600, "Nyx window frame test", nullptr, nullptr);
		Require(window != nullptr, "The window should be made");
		return window;
	}

	// With a custom titlebar the window is its content: made at its size, placed and sized where
	// asked, no frame, and put back where it says it is, it stays there
	void TestCustomTitlebar()
	{
		GLFWwindow* window = MakeWindow(true);
		const HWND handle = glfwGetWin32Window(window);
		RECT rect;
		GetWindowRect(handle, &rect);
		Require(rect.right - rect.left == 800 && rect.bottom - rect.top == 600, "A custom titlebar window should be made at its content's size, not " + Describe(rect));

		glfwSetWindowPos(window, 100, 120);
		glfwSetWindowSize(window, 900, 700);
		GetWindowRect(handle, &rect);
		Require(rect.left == 100 && rect.top == 120 && rect.right == 1000 && rect.bottom == 820,
			"A custom titlebar window should be where it was put, its top on the screen: " + Describe(rect));

		int left = -1;
		int top = -1;
		int right = -1;
		int bottom = -1;
		glfwGetWindowFrameSize(window, &left, &top, &right, &bottom);
		Require(left == 0 && top == 0 && right == 0 && bottom == 0, "A custom titlebar window should have no frame");

		int x = 0;
		int y = 0;
		int width = 0;
		int height = 0;
		for (int round = 0; round < 3; ++round)
		{
			glfwGetWindowPos(window, &x, &y);
			glfwGetWindowSize(window, &width, &height);
			glfwSetWindowPos(window, x, y);
			glfwSetWindowSize(window, width, height);
		}
		GetWindowRect(handle, &rect);
		Require(rect.left == 100 && rect.top == 120 && rect.right == 1000 && rect.bottom == 820,
			"Put back where it said it was, a custom titlebar window should stay there: " + Describe(rect));
		glfwDestroyWindow(window);
	}

	// With Windows' titlebar, GLFW places and sizes the content, the frame round it
	void TestWindowsTitlebar()
	{
		GLFWwindow* window = MakeWindow(false);
		int left = 0;
		int top = 0;
		glfwGetWindowFrameSize(window, &left, &top, nullptr, nullptr);
		Require(top > left && left >= 0, "A window with Windows' titlebar should have a frame with the titlebar on top");

		glfwSetWindowPos(window, 100, 120);
		int x = 0;
		int y = 0;
		glfwGetWindowPos(window, &x, &y);
		Require(x == 100 && y == 120, "A window with Windows' titlebar should have its content where it was put");
		RECT rect;
		GetWindowRect(glfwGetWin32Window(window), &rect);
		Require(rect.top == 120 - top && rect.left == 100 - left, "The frame should be round the content: " + Describe(rect));
		glfwDestroyWindow(window);
	}
}

int main()
{
	try
	{
		Require(glfwInit() == GLFW_TRUE, "GLFW should start");
		TestCustomTitlebar();
		TestWindowsTitlebar();
		glfwTerminate();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Window frame test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All window frame tests passed.\n";
	return 0;
}
