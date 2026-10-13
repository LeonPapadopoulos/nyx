#include "GlfwInputBackend.h"

#include "Assertions.h"

#include <GLFW/glfw3.h>
#include <imgui.h>

#include <optional>

namespace Nyx::Input
{
	namespace
	{
		// GLFW's callbacks can't carry a pointer of ours (the window's user pointer is the
		// window's), so they find the backend here.
		GlfwInputBackend* Instance = nullptr;

		// ImGui's functions that make and destroy the OS windows of floating windows
		void (*ImGuiCreateWindow)(ImGuiViewport*) = nullptr;
		void (*ImGuiDestroyWindow)(ImGuiViewport*) = nullptr;

		std::optional<EKey> ToKey(int glfwKey)
		{
			switch (glfwKey)
			{
			case GLFW_KEY_SPACE:         return EKey::Space;
			case GLFW_KEY_APOSTROPHE:    return EKey::Apostrophe;
			case GLFW_KEY_COMMA:         return EKey::Comma;
			case GLFW_KEY_MINUS:         return EKey::Minus;
			case GLFW_KEY_PERIOD:        return EKey::Period;
			case GLFW_KEY_SLASH:         return EKey::Slash;
			case GLFW_KEY_0:             return EKey::Digit0;
			case GLFW_KEY_1:             return EKey::Digit1;
			case GLFW_KEY_2:             return EKey::Digit2;
			case GLFW_KEY_3:             return EKey::Digit3;
			case GLFW_KEY_4:             return EKey::Digit4;
			case GLFW_KEY_5:             return EKey::Digit5;
			case GLFW_KEY_6:             return EKey::Digit6;
			case GLFW_KEY_7:             return EKey::Digit7;
			case GLFW_KEY_8:             return EKey::Digit8;
			case GLFW_KEY_9:             return EKey::Digit9;
			case GLFW_KEY_SEMICOLON:     return EKey::Semicolon;
			case GLFW_KEY_EQUAL:         return EKey::Equal;
			case GLFW_KEY_A:             return EKey::A;
			case GLFW_KEY_B:             return EKey::B;
			case GLFW_KEY_C:             return EKey::C;
			case GLFW_KEY_D:             return EKey::D;
			case GLFW_KEY_E:             return EKey::E;
			case GLFW_KEY_F:             return EKey::F;
			case GLFW_KEY_G:             return EKey::G;
			case GLFW_KEY_H:             return EKey::H;
			case GLFW_KEY_I:             return EKey::I;
			case GLFW_KEY_J:             return EKey::J;
			case GLFW_KEY_K:             return EKey::K;
			case GLFW_KEY_L:             return EKey::L;
			case GLFW_KEY_M:             return EKey::M;
			case GLFW_KEY_N:             return EKey::N;
			case GLFW_KEY_O:             return EKey::O;
			case GLFW_KEY_P:             return EKey::P;
			case GLFW_KEY_Q:             return EKey::Q;
			case GLFW_KEY_R:             return EKey::R;
			case GLFW_KEY_S:             return EKey::S;
			case GLFW_KEY_T:             return EKey::T;
			case GLFW_KEY_U:             return EKey::U;
			case GLFW_KEY_V:             return EKey::V;
			case GLFW_KEY_W:             return EKey::W;
			case GLFW_KEY_X:             return EKey::X;
			case GLFW_KEY_Y:             return EKey::Y;
			case GLFW_KEY_Z:             return EKey::Z;
			case GLFW_KEY_LEFT_BRACKET:  return EKey::LeftBracket;
			case GLFW_KEY_BACKSLASH:     return EKey::Backslash;
			case GLFW_KEY_RIGHT_BRACKET: return EKey::RightBracket;
			case GLFW_KEY_GRAVE_ACCENT:  return EKey::GraveAccent;
			case GLFW_KEY_WORLD_1:       return EKey::World1;
			case GLFW_KEY_WORLD_2:       return EKey::World2;
			case GLFW_KEY_ESCAPE:        return EKey::Escape;
			case GLFW_KEY_ENTER:         return EKey::Enter;
			case GLFW_KEY_TAB:           return EKey::Tab;
			case GLFW_KEY_BACKSPACE:     return EKey::Backspace;
			case GLFW_KEY_INSERT:        return EKey::Insert;
			case GLFW_KEY_DELETE:        return EKey::Delete;
			case GLFW_KEY_RIGHT:         return EKey::Right;
			case GLFW_KEY_LEFT:          return EKey::Left;
			case GLFW_KEY_DOWN:          return EKey::Down;
			case GLFW_KEY_UP:            return EKey::Up;
			case GLFW_KEY_PAGE_UP:       return EKey::PageUp;
			case GLFW_KEY_PAGE_DOWN:     return EKey::PageDown;
			case GLFW_KEY_HOME:          return EKey::Home;
			case GLFW_KEY_END:           return EKey::End;
			case GLFW_KEY_CAPS_LOCK:     return EKey::CapsLock;
			case GLFW_KEY_SCROLL_LOCK:   return EKey::ScrollLock;
			case GLFW_KEY_NUM_LOCK:      return EKey::NumLock;
			case GLFW_KEY_PRINT_SCREEN:  return EKey::PrintScreen;
			case GLFW_KEY_PAUSE:         return EKey::Pause;
			case GLFW_KEY_F1:            return EKey::F1;
			case GLFW_KEY_F2:            return EKey::F2;
			case GLFW_KEY_F3:            return EKey::F3;
			case GLFW_KEY_F4:            return EKey::F4;
			case GLFW_KEY_F5:            return EKey::F5;
			case GLFW_KEY_F6:            return EKey::F6;
			case GLFW_KEY_F7:            return EKey::F7;
			case GLFW_KEY_F8:            return EKey::F8;
			case GLFW_KEY_F9:            return EKey::F9;
			case GLFW_KEY_F10:           return EKey::F10;
			case GLFW_KEY_F11:           return EKey::F11;
			case GLFW_KEY_F12:           return EKey::F12;
			case GLFW_KEY_KP_0:          return EKey::Numpad0;
			case GLFW_KEY_KP_1:          return EKey::Numpad1;
			case GLFW_KEY_KP_2:          return EKey::Numpad2;
			case GLFW_KEY_KP_3:          return EKey::Numpad3;
			case GLFW_KEY_KP_4:          return EKey::Numpad4;
			case GLFW_KEY_KP_5:          return EKey::Numpad5;
			case GLFW_KEY_KP_6:          return EKey::Numpad6;
			case GLFW_KEY_KP_7:          return EKey::Numpad7;
			case GLFW_KEY_KP_8:          return EKey::Numpad8;
			case GLFW_KEY_KP_9:          return EKey::Numpad9;
			case GLFW_KEY_KP_DECIMAL:    return EKey::NumpadDecimal;
			case GLFW_KEY_KP_DIVIDE:     return EKey::NumpadDivide;
			case GLFW_KEY_KP_MULTIPLY:   return EKey::NumpadMultiply;
			case GLFW_KEY_KP_SUBTRACT:   return EKey::NumpadSubtract;
			case GLFW_KEY_KP_ADD:        return EKey::NumpadAdd;
			case GLFW_KEY_KP_ENTER:      return EKey::NumpadEnter;
			case GLFW_KEY_KP_EQUAL:      return EKey::NumpadEqual;
			case GLFW_KEY_LEFT_SHIFT:    return EKey::LeftShift;
			case GLFW_KEY_LEFT_CONTROL:  return EKey::LeftControl;
			case GLFW_KEY_LEFT_ALT:      return EKey::LeftAlt;
			case GLFW_KEY_LEFT_SUPER:    return EKey::LeftSuper;
			case GLFW_KEY_RIGHT_SHIFT:   return EKey::RightShift;
			case GLFW_KEY_RIGHT_CONTROL: return EKey::RightControl;
			case GLFW_KEY_RIGHT_ALT:     return EKey::RightAlt;
			case GLFW_KEY_RIGHT_SUPER:   return EKey::RightSuper;
			case GLFW_KEY_MENU:          return EKey::Menu;
			default:                     return std::nullopt;
			}
		}

		std::optional<EMouseControl> ToMouseButton(int glfwButton)
		{
			switch (glfwButton)
			{
			case GLFW_MOUSE_BUTTON_LEFT:   return EMouseControl::Left;
			case GLFW_MOUSE_BUTTON_RIGHT:  return EMouseControl::Right;
			case GLFW_MOUSE_BUTTON_MIDDLE: return EMouseControl::Middle;
			case GLFW_MOUSE_BUTTON_4:      return EMouseControl::Button4;
			case GLFW_MOUSE_BUTTON_5:      return EMouseControl::Button5;
			default:                       return std::nullopt;
			}
		}
	}

	GlfwInputBackend::GlfwInputBackend(GLFWwindow* mainWindow)
		: MainWindow(mainWindow)
		, KeyboardId(MakeDeviceId())
		, MouseId(MakeDeviceId())
		, CursorWindow(mainWindow)
	{
		ASSERT(!Instance && "There is one GLFW input backend per program");
		Instance = this;

		Install(MainWindow);
		if (glfwGetWindowAttrib(MainWindow, GLFW_FOCUSED) == GLFW_TRUE)
		{
			FocusedWindows.insert(MainWindow);
		}

		// GLFW can't tell keyboards or mice apart: there is one of each, from the start.
		const uint64_t now = NowUs();
		for (const auto& [id, kind, name] : { std::tuple{ KeyboardId, EDeviceKind::Keyboard, "Keyboard" },
				 std::tuple{ MouseId, EDeviceKind::Mouse, "Mouse" } })
		{
			InputEvent event;
			event.Kind = EInputEventKind::DeviceAdded;
			event.TimeUs = now;
			event.Device.Id = id;
			event.Device.Kind = kind;
			event.Device.Name = name;
			event.Device.Backend = GetName();
			Pending.push_back(std::move(event));
		}
	}

	GlfwInputBackend::~GlfwInputBackend()
	{
		Instance = nullptr;
	}

	void GlfwInputBackend::HookImGuiWindows()
	{
		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		ImGuiCreateWindow = platformIO.Platform_CreateWindow;
		ImGuiDestroyWindow = platformIO.Platform_DestroyWindow;

		platformIO.Platform_CreateWindow = [](ImGuiViewport* viewport)
		{
			ImGuiCreateWindow(viewport);
			if (Instance && viewport->PlatformHandle)
			{
				Instance->Install(static_cast<GLFWwindow*>(viewport->PlatformHandle));
			}
		};

		platformIO.Platform_DestroyWindow = [](ImGuiViewport* viewport)
		{
			if (Instance && viewport->PlatformHandle)
			{
				Instance->Forget(static_cast<GLFWwindow*>(viewport->PlatformHandle));
			}
			ImGuiDestroyWindow(viewport);
		};
	}

	void GlfwInputBackend::Poll(std::vector<InputEvent>& outEvents)
	{
		// Focus moving from one of the program's windows to another (e.g. into a scene view
		// dragged out of the editor) loses it in one and gains it in the other within one
		// glfwPollEvents(): only report what it is now.
		const bool bFocused = !FocusedWindows.empty();
		if (bFocused != bReportedFocus)
		{
			InputEvent event;
			event.Kind = EInputEventKind::FocusChanged;
			event.TimeUs = NowUs();
			event.bFocused = bFocused;
			Pending.push_back(std::move(event));
			bReportedFocus = bFocused;
		}

		outEvents.insert(outEvents.end(), std::make_move_iterator(Pending.begin()), std::make_move_iterator(Pending.end()));
		Pending.clear();
	}

	void GlfwInputBackend::ApplyCursor(const CursorRequest& request)
	{
		GLFWwindow* window = request.Window ? static_cast<GLFWwindow*>(request.Window) : MainWindow;
		ECursorMode mode = request.Mode;
		if (!FindWindow(window))
		{
			// Its window is gone, e.g. a floating scene view closed while looking around with it
			window = MainWindow;
			mode = ECursorMode::Free;
		}

		if (window == CursorWindow && mode == CursorMode)
		{
			return;
		}

		if (CursorWindow != window && CursorMode != ECursorMode::Free)
		{
			SetCursorMode(CursorWindow, ECursorMode::Free);
		}
		SetCursorMode(window, mode);

		// ImGui sets the cursor of every OS window it has each frame, and only leaves them alone
		// while the main window's cursor is captured. Keep it out while one of ours isn't free.
		if (ImGui::GetCurrentContext())
		{
			ImGuiIO& io = ImGui::GetIO();
			if (mode != ECursorMode::Free && CursorMode == ECursorMode::Free)
			{
				bImGuiCursorChangesWereOff = (io.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0;
				io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
			}
			else if (mode == ECursorMode::Free && CursorMode != ECursorMode::Free && !bImGuiCursorChangesWereOff)
			{
				io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			}
		}

		CursorWindow = window;
		CursorMode = mode;

		// The cursor jumps when it is captured or let go: start Mouse/Delta from where it is now
		for (auto& [_, state] : Windows)
		{
			state.bHasLastCursor = false;
		}
	}

	void GlfwInputBackend::Install(GLFWwindow* window)
	{
		if (Windows.contains(window))
		{
			return;
		}

		WindowState state;
		state.PreviousKey = glfwSetKeyCallback(window, &GlfwInputBackend::OnKey);
		state.PreviousMouseButton = glfwSetMouseButtonCallback(window, &GlfwInputBackend::OnMouseButton);
		state.PreviousCursorPos = glfwSetCursorPosCallback(window, &GlfwInputBackend::OnCursorPos);
		state.PreviousScroll = glfwSetScrollCallback(window, &GlfwInputBackend::OnScroll);
		state.PreviousFocus = glfwSetWindowFocusCallback(window, &GlfwInputBackend::OnFocus);
		Windows.emplace(window, state);
	}

	void GlfwInputBackend::Forget(GLFWwindow* window)
	{
		if (window == MainWindow)
		{
			return;
		}

		Windows.erase(window);
		FocusedWindows.erase(window);

		if (window == CursorWindow)
		{
			if (CursorMode != ECursorMode::Free && ImGui::GetCurrentContext() && !bImGuiCursorChangesWereOff)
			{
				ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
			}
			CursorWindow = MainWindow;
			CursorMode = ECursorMode::Free;
		}
	}

	GlfwInputBackend::WindowState* GlfwInputBackend::FindWindow(GLFWwindow* window)
	{
		const auto found = Windows.find(window);
		return found != Windows.end() ? &found->second : nullptr;
	}

	void GlfwInputBackend::PushControl(ControlId control, glm::vec2 value)
	{
		InputEvent event;
		event.Kind = EInputEventKind::Control;
		event.TimeUs = NowUs();
		event.Device.Id = control.Device == EDeviceKind::Keyboard ? KeyboardId : MouseId;
		event.Control = control;
		event.Value = value;
		Pending.push_back(std::move(event));
	}

	void GlfwInputBackend::SetCursorMode(GLFWwindow* window, ECursorMode mode)
	{
		const int glfwMode = mode == ECursorMode::Captured ? GLFW_CURSOR_DISABLED
			: mode == ECursorMode::Hidden                  ? GLFW_CURSOR_HIDDEN
														   : GLFW_CURSOR_NORMAL;
		glfwSetInputMode(window, GLFW_CURSOR, glfwMode);

		// Raw motion: the mouse's own counts, without the system's pointer acceleration
		if (glfwRawMouseMotionSupported())
		{
			glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, mode == ECursorMode::Captured ? GLFW_TRUE : GLFW_FALSE);
		}
	}

	// Each callback reports to the backend, then calls the callback the window had before: on
	// ImGui's OS windows, ImGui's own.

	void GlfwInputBackend::OnKey(GLFWwindow* window, int key, int scancode, int action, int mods)
	{
		if (!Instance)
		{
			return;
		}

		if (action != GLFW_REPEAT)
		{
			if (const std::optional<EKey> mapped = ToKey(key))
			{
				Instance->PushControl(Key(*mapped), glm::vec2(action == GLFW_PRESS ? 1.0f : 0.0f, 0.0f));
			}
		}

		const WindowState* state = Instance->FindWindow(window);
		if (const auto previous = state ? state->PreviousKey : nullptr)
		{
			previous(window, key, scancode, action, mods);
		}
	}

	void GlfwInputBackend::OnMouseButton(GLFWwindow* window, int button, int action, int mods)
	{
		if (!Instance)
		{
			return;
		}

		if (const std::optional<EMouseControl> mapped = ToMouseButton(button))
		{
			Instance->PushControl(Mouse(*mapped), glm::vec2(action == GLFW_PRESS ? 1.0f : 0.0f, 0.0f));
		}

		const WindowState* state = Instance->FindWindow(window);
		if (const auto previous = state ? state->PreviousMouseButton : nullptr)
		{
			previous(window, button, action, mods);
		}
	}

	void GlfwInputBackend::OnCursorPos(GLFWwindow* window, double x, double y)
	{
		if (!Instance)
		{
			return;
		}

		WindowState* state = Instance->FindWindow(window);
		if (state)
		{
			// Positions are relative to their window: only differences within one window count
			if (state->bHasLastCursor)
			{
				const glm::dvec2 delta = glm::dvec2(x, y) - state->LastCursor;
				if (delta != glm::dvec2(0.0))
				{
					Instance->PushControl(Mouse(EMouseControl::Delta), glm::vec2(delta));
				}
			}
			state->LastCursor = glm::dvec2(x, y);
			state->bHasLastCursor = true;
		}

		if (const auto previous = state ? state->PreviousCursorPos : nullptr)
		{
			previous(window, x, y);
		}
	}

	void GlfwInputBackend::OnScroll(GLFWwindow* window, double xOffset, double yOffset)
	{
		if (!Instance)
		{
			return;
		}

		Instance->PushControl(Mouse(EMouseControl::Wheel), glm::vec2(static_cast<float>(xOffset), static_cast<float>(yOffset)));

		const WindowState* state = Instance->FindWindow(window);
		if (const auto previous = state ? state->PreviousScroll : nullptr)
		{
			previous(window, xOffset, yOffset);
		}
	}

	void GlfwInputBackend::OnFocus(GLFWwindow* window, int focused)
	{
		if (!Instance)
		{
			return;
		}

		if (focused == GLFW_TRUE)
		{
			Instance->FocusedWindows.insert(window);
		}
		else
		{
			Instance->FocusedWindows.erase(window);
		}

		const WindowState* state = Instance->FindWindow(window);
		if (const auto previous = state ? state->PreviousFocus : nullptr)
		{
			previous(window, focused);
		}
	}
}
