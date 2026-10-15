#pragma once

#include <imgui.h>

#include <functional>

namespace Nyx::Tests
{
	// A headless ImGui context for the duration of a test
	class HeadlessUI
	{
	public:
		HeadlessUI()
		{
			ImGui::CreateContext();
			ImGuiIO& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.DisplaySize = ImVec2(1280, 800);
			io.DeltaTime = 1.0f / 60.0f;
			unsigned char* pixels = nullptr;
			int width = 0;
			int height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		}

		~HeadlessUI()
		{
			ImGui::DestroyContext();
		}

		HeadlessUI(const HeadlessUI&) = delete;
		HeadlessUI& operator=(const HeadlessUI&) = delete;

		void Frame(const std::function<void()>& draw)
		{
			ImGui::NewFrame();
			draw();
			ImGui::Render();
		}

		// One frame in which the key (with Ctrl, and Shift when asked) is pressed, then one in
		// which it is released
		void Press(ImGuiKey key, bool bCtrl, bool bShift, const std::function<void()>& draw)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, bCtrl);
			io.AddKeyEvent(ImGuiMod_Shift, bShift);
			io.AddKeyEvent(key, true);
			Frame(draw);
			io.AddKeyEvent(key, false);
			io.AddKeyEvent(ImGuiMod_Shift, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame(draw);
		}
	};
}
