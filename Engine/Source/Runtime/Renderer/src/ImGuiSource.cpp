#include "ImGuiSource.h"
#include <imgui_internal.h>

static_assert(NYX_UI_INSPECTION, "ImGui source inspection must only be compiled into the editor engine.");

namespace Nyx::UI
{
	namespace
	{
		SourceItemCallback Callback = nullptr;
		void* CallbackUserData = nullptr;
		SourceLocation Declaration;
		bool bInspecting = false;
	}

	void SetSourceItemCallback(SourceItemCallback callback, void* userData)
	{
		Callback = callback;
		CallbackUserData = userData;
	}

	bool IsInspectingSources() { return bInspecting; }
	void SetInspectingSources(bool value) { bInspecting = value; }

	SourceDeclarationScope::SourceDeclarationScope(SourceLocation declaration) : Previous(Declaration)
	{
		Declaration = declaration;
	}

	SourceDeclarationScope::~SourceDeclarationScope() { Declaration = Previous; }

	void RegisterRegion(ImVec2 min, ImVec2 max, SourceLocation location)
	{
		if (!Callback || !bInspecting)
		{
			return;
		}
		ImGuiWindow* window = ImGui::GetCurrentWindow();
		ImRect bounds(min, max);
		bounds.ClipWith(window->ClipRect);
		Callback({ location, Declaration, window, bounds.Min, bounds.Max, false }, CallbackUserData);
	}

	void RegisterLastItem(SourceLocation location)
	{
		if (Callback && bInspecting)
		{
			RegisterRegion(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), location);
		}
	}

	void RegisterWindow(SourceLocation location)
	{
		if (Callback && bInspecting)
		{
			ImGuiWindow* window = ImGui::GetCurrentWindow();
			Callback({ location, {}, window, window->Rect().Min, window->Rect().Max, true }, CallbackUserData);
		}
	}

	bool Begin(const char* name, bool* open, ImGuiWindowFlags flags, SourceLocation location)
	{
		const bool visible = ImGui::Begin(name, open, flags);
		RegisterWindow(location);
		return visible;
	}

	bool BeginChild(const char* name, ImVec2 size, ImGuiChildFlags childFlags, ImGuiWindowFlags flags, SourceLocation location)
	{
		const bool visible = ImGui::BeginChild(name, size, childFlags, flags);
		RegisterWindow(location);
		return visible;
	}

	bool BeginPopup(const char* name, ImGuiWindowFlags flags, SourceLocation location)
	{
		const bool visible = ImGui::BeginPopup(name, flags);
		if (visible) RegisterWindow(location);
		return visible;
	}

	bool BeginPopupModal(const char* name, bool* open, ImGuiWindowFlags flags, SourceLocation location)
	{
		const bool visible = ImGui::BeginPopupModal(name, open, flags);
		if (visible) RegisterWindow(location);
		return visible;
	}
}
