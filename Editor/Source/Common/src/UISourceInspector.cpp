#include "UISourceInspector.h"
#include <imgui_internal.h>

namespace Nyx::Editor
{
	UISourceInspector::~UISourceInspector() { Detach(); }

	void UISourceInspector::Attach(std::function<void(SourceLocation)> openSource)
	{
		Detach();
		Context = ImGui::GetCurrentContext();
		OpenSource = std::move(openSource);
		if (!Context) return;
		for (auto type : { ImGuiContextHookType_NewFramePre, ImGuiContextHookType_NewFramePost,
			ImGuiContextHookType_EndFramePre, ImGuiContextHookType_Shutdown })
		{
			ImGuiContextHook hook{};
			hook.Type = type;
			hook.Callback = OnFrame;
			hook.UserData = this;
			Hooks.push_back(ImGui::AddContextHook(Context, &hook));
		}
		UI::SetSourceItemCallback(OnItem, this);
	}

	void UISourceInspector::Detach()
	{
		if (Context)
		{
			for (ImGuiID hook : Hooks) ImGui::RemoveContextHook(Context, hook);
			UI::SetSourceItemCallback(nullptr, nullptr);
			SetActive(false);
		}
		Context = nullptr;
		Hooks.clear();
		HoveredItem.reset();
		OpenSource = {};
	}

	void UISourceInspector::SetActive(bool active)
	{
		bActive = active;
		UI::SetInspectingSources(active);
		if (active && Context)
		{
			// F8 can activate after ImGui's picker has already processed this frame's input.
			Context->DebugItemPickerActive = false;
			Context->DebugItemPickerBreakId = 0;
		}
	}

	void UISourceInspector::OnFrame(ImGuiContext* context, ImGuiContextHook* hook)
	{
		auto& inspector = *static_cast<UISourceInspector*>(hook->UserData);
		switch (hook->Type)
		{
		case ImGuiContextHookType_NewFramePre: inspector.BeforeFrame(); break;
		case ImGuiContextHookType_NewFramePost: inspector.AfterFrameStarted(); break;
		case ImGuiContextHookType_EndFramePre: inspector.DrawOverlay(); break;
		case ImGuiContextHookType_Shutdown:
			inspector.SetActive(false);
			UI::SetSourceItemCallback(nullptr, nullptr);
			inspector.Context = nullptr;
			break;
		default: break;
		}
	}

	void UISourceInspector::BeforeFrame()
	{
		HoveredItem.reset();
		bPickRequested = false;
		if (!bActive) return;

		// Consume input before ImGui processes it, including titlebar/docking behavior.
		// Keep movement for hit testing, Shift for the alternate destination, and key-up
		// events so keys held before inspection cannot become stuck afterwards.
		auto& events = Context->InputEventsQueue;
		int kept = 0;
		for (const ImGuiInputEvent& event : events)
		{
			if (event.Type == ImGuiInputEventType_MouseButton)
			{
				if (event.MouseButton.Button == ImGuiMouseButton_Left && event.MouseButton.Down)
					bPickRequested = true;
				continue;
			}
			if (event.Type == ImGuiInputEventType_MouseWheel || event.Type == ImGuiInputEventType_Text) continue;
			if (event.Type == ImGuiInputEventType_Key && event.Key.Down &&
				event.Key.Key != ImGuiKey_F8 && event.Key.Key != ImGuiKey_Escape &&
				event.Key.Key != ImGuiKey_LeftShift && event.Key.Key != ImGuiKey_RightShift &&
				event.Key.Key != ImGuiMod_Shift) continue;
			events[kept++] = event;
		}
		events.resize(kept);
	}

	void UISourceInspector::AfterFrameStarted()
	{
		if (bToggleRequested || ImGui::IsKeyPressed(ImGuiKey_F8, false))
		{
			bToggleRequested = false;
			if (bActive) SetActive(false);
			else if (Context->ActiveId == 0 && !ImGui::IsAnyMouseDown())
			{
				Status.clear();
				SetActive(true);
			}
			else
			{
				Status = "Finish the current edit or drag before inspecting UI.";
				StatusExpiresAt = ImGui::GetTime() + 2.0;
			}
		}
		if (bActive && ImGui::IsKeyPressed(ImGuiKey_Escape, false)) SetActive(false);
		if (bActive)
		{
			// F8 can arrive in the same batch as a navigation key on the activation frame.
			Context->NavActivateId = 0;
			Context->NavActivateDownId = 0;
			Context->NavActivatePressedId = 0;
			Context->IO.InputQueueCharacters.clear();
		}
	}

	void UISourceInspector::OnItem(const UI::SourceItem& item, void* userData)
	{
		auto& inspector = *static_cast<UISourceInspector*>(userData);
		if (!inspector.bActive || !item.CallSite.IsValid()) return;
		const auto* hoveredWindow = inspector.Context->HoveredWindow;
		if (hoveredWindow != item.Window) return;
		if (!ImRect(item.Min, item.Max).Contains(ImGui::GetMousePos())) return;
		// A panel is a fallback for its background and ImGui-owned window chrome.
		if (item.bWindow && inspector.HoveredItem && !inspector.HoveredItem->bWindow) return;
		inspector.HoveredItem = item;
	}

	void UISourceInspector::DrawOverlay()
	{
		if (!bActive)
		{
			if (!Status.empty() && ImGui::GetTime() < StatusExpiresAt)
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted(Status.c_str());
				ImGui::EndTooltip();
			}
			return;
		}
		ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
		SourceLocation target;
		if (HoveredItem)
		{
			const auto& item = *HoveredItem;
			target = !ImGui::GetIO().KeyShift && item.Declaration.IsValid() ? item.Declaration : item.CallSite;
			ImDrawList* draw = ImGui::GetForegroundDrawList(item.Window->Viewport);
			draw->AddRectFilled(item.Min, item.Max, IM_COL32(145, 175, 255, 32), 3.0f);
			draw->AddRect(item.Min, item.Max, IM_COL32(170, 195, 255, 255), 3.0f, 0, 2.0f);
		}
		ImGui::BeginTooltip();
		ImGui::TextUnformatted("Inspect UI | F8 / Escape to cancel");
		if (target.IsValid())
		{
			ImGui::TextUnformatted(HoveredItem->bWindow ? "Panel source (background / window chrome)" : "Click to open source");
			ImGui::Text("%s:%u", target.File, target.Line);
			if (HoveredItem->Declaration.IsValid()) ImGui::TextUnformatted("Shift-click: widget implementation");
		}
		else ImGui::TextUnformatted("No source registered here. Open menus before entering inspection.");
		ImGui::EndTooltip();

		if (bPickRequested && target.IsValid())
		{
			SetActive(false);
			if (OpenSource) OpenSource(target);
		}
	}
}
