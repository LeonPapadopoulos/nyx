#pragma once

#include "InputBackend.h"

#include <glm/glm.hpp>

#include <unordered_map>
#include <unordered_set>
#include <vector>

struct GLFWwindow;

namespace Nyx::Input
{
	// Keyboard and mouse through GLFW, from the main window and from every OS window ImGui makes
	// for floating windows: a scene view dragged out of the editor gets its keys too.
	//
	// The window makes it, in this order:
	// 1. right after creating its GLFW window, before ImGui's GLFW backend is set up: ImGui then
	//    calls this backend's callbacks after its own, as it does for any callbacks it finds;
	// 2. HookImGuiWindows() once ImGui's backend is set up: ImGui gives the OS windows it makes
	//    callbacks of its own, without calling others, so this backend adds its callbacks to each
	//    new one and calls ImGui's from them.
	//
	// There is one per program (GLFW's callbacks can't carry it); it must live until the window is
	// destroyed.
	class GlfwInputBackend final : public IInputBackend
	{
	public:
		explicit GlfwInputBackend(GLFWwindow* mainWindow);
		~GlfwInputBackend() override;

		GlfwInputBackend(const GlfwInputBackend&) = delete;
		GlfwInputBackend& operator=(const GlfwInputBackend&) = delete;

		void HookImGuiWindows();

		const char* GetName() const override
		{
			return "GLFW";
		}

		void Poll(std::vector<InputEvent>& outEvents) override;
		void ApplyCursor(const CursorRequest& request) override;

	private:
		// The callbacks a window had before this backend's, called after them
		struct WindowState
		{
			void (*PreviousKey)(GLFWwindow*, int, int, int, int) = nullptr;
			void (*PreviousMouseButton)(GLFWwindow*, int, int, int) = nullptr;
			void (*PreviousCursorPos)(GLFWwindow*, double, double) = nullptr;
			void (*PreviousScroll)(GLFWwindow*, double, double) = nullptr;
			void (*PreviousFocus)(GLFWwindow*, int) = nullptr;

			// Where the cursor was at its last event in this window, for Mouse/Delta
			glm::dvec2 LastCursor{ 0.0 };
			bool bHasLastCursor = false;
		};

		void Install(GLFWwindow* window);
		void Forget(GLFWwindow* window);
		WindowState* FindWindow(GLFWwindow* window);

		void PushControl(ControlId control, glm::vec3 value);
		void SetCursorMode(GLFWwindow* window, ECursorMode mode);

		static void OnKey(GLFWwindow* window, int key, int scancode, int action, int mods);
		static void OnMouseButton(GLFWwindow* window, int button, int action, int mods);
		static void OnCursorPos(GLFWwindow* window, double x, double y);
		static void OnScroll(GLFWwindow* window, double xOffset, double yOffset);
		static void OnFocus(GLFWwindow* window, int focused);

		GLFWwindow* MainWindow = nullptr;
		std::unordered_map<GLFWwindow*, WindowState> Windows;
		std::unordered_set<GLFWwindow*> FocusedWindows;
		bool bReportedFocus = true;

		InputDeviceId KeyboardId;
		InputDeviceId MouseId;
		std::vector<InputEvent> Pending;

		// The cursor as it was last applied
		GLFWwindow* CursorWindow = nullptr;
		ECursorMode CursorMode = ECursorMode::Free;
		bool bImGuiCursorChangesWereOff = false;
	};
}
