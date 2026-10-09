#pragma once

#include "ImGuiSource.h"
#include <functional>
#include <optional>
#include <string>
#include <vector>

struct ImGuiContextHook;

namespace Nyx::Editor
{
	class UISourceInspector
	{
	public:
		~UISourceInspector();
		void Attach(std::function<void(SourceLocation)> openSource);
		void Detach();
		void RequestToggle() { bToggleRequested = true; }
		bool IsActive() const { return bActive; }
		const std::string& GetStatus() const { return Status; }
		const std::optional<UI::SourceItem>& GetHoveredItem() const { return HoveredItem; }

	private:
		static void OnFrame(ImGuiContext* context, ImGuiContextHook* hook);
		static void OnItem(const UI::SourceItem& item, void* userData);
		void BeforeFrame();
		void AfterFrameStarted();
		void DrawOverlay();
		void SetActive(bool active);

		ImGuiContext* Context = nullptr;
		std::vector<ImGuiID> Hooks;
		std::function<void(SourceLocation)> OpenSource;
		std::optional<UI::SourceItem> HoveredItem;
		std::string Status;
		double StatusExpiresAt = 0.0;
		bool bActive = false;
		bool bToggleRequested = false;
		bool bPickRequested = false;
	};
}
