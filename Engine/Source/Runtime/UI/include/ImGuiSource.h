#pragma once

#include <imgui.h>

// Enabled by the editor engine target and inherited by its consumers.
#ifndef NYX_UI_INSPECTION
	#define NYX_UI_INSPECTION 0
#endif

#if NYX_UI_INSPECTION
#include "SourceLocation.h"
#include <type_traits>
#include <utility>

struct ImGuiWindow;

namespace Nyx::UI
{
	struct SourceItem
	{
		SourceLocation CallSite;
		SourceLocation Declaration;
		ImGuiWindow* Window = nullptr;
		ImVec2 Min;
		ImVec2 Max;
		bool bWindow = false;
	};

	// Installed by the editor. Game builds can draw the same widgets without an inspector.
	using SourceItemCallback = void(*)(const SourceItem&, void*);
	void SetSourceItemCallback(SourceItemCallback callback, void* userData);
	void RegisterLastItem(SourceLocation location);
	void RegisterWindow(SourceLocation location);
	void RegisterRegion(ImVec2 min, ImVec2 max, SourceLocation location = std::source_location::current());
	bool IsInspectingSources();
	void SetInspectingSources(bool bInspecting);

	class SourceDeclarationScope
	{
	public:
		explicit SourceDeclarationScope(SourceLocation declaration);
		~SourceDeclarationScope();
		SourceDeclarationScope(const SourceDeclarationScope&) = delete;
		SourceDeclarationScope& operator=(const SourceDeclarationScope&) = delete;
	private:
		SourceLocation Previous;
	};

	// Evaluate the widget once, preserve its return value, then associate its bounds with
	// the caller. The macro below also works with ImGui's formatted text/overloaded widgets.
	template<typename DrawFunction>
	decltype(auto) DrawWithSource(DrawFunction&& draw, SourceLocation location)
	{
		if constexpr (std::is_void_v<std::invoke_result_t<DrawFunction>>)
		{
			std::forward<DrawFunction>(draw)();
			RegisterLastItem(location);
		}
		else
		{
			auto result = std::forward<DrawFunction>(draw)();
			RegisterLastItem(location);
			return result;
		}
	}

	bool Begin(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0,
		SourceLocation location = std::source_location::current());
	bool BeginChild(const char* name, ImVec2 size = {}, ImGuiChildFlags childFlags = 0,
		ImGuiWindowFlags flags = 0, SourceLocation location = std::source_location::current());
	bool BeginPopup(const char* name, ImGuiWindowFlags flags = 0,
		SourceLocation location = std::source_location::current());
	bool BeginPopupModal(const char* name, bool* open = nullptr, ImGuiWindowFlags flags = 0,
		SourceLocation location = std::source_location::current());
}

#define NYX_UI(widget) ::Nyx::UI::DrawWithSource([&]() { return (widget); }, ::Nyx::SourceLocation::Current())
#else
namespace Nyx::UI
{
	// These are ImGui's functions themselves, with no wrapper or source arguments.
	using ImGui::Begin;
	using ImGui::BeginChild;
	using ImGui::BeginPopup;
	using ImGui::BeginPopupModal;
}

#define NYX_UI(widget) (widget)
#endif
