#include "SearchRow.h"

#include "ImGuiSource.h"
#include "UIWidgets.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>

namespace Nyx::Editor
{
	bool NameMatchesFilter(std::string_view name, std::string_view filter)
	{
		const auto lower = [](char c)
		{
			return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		};
		return filter.empty() ||
			std::search(name.begin(), name.end(), filter.begin(), filter.end(),
				[&](char a, char b)
				{
					return lower(a) == lower(b);
				}) != name.end();
	}

	void DrawSearchRow(std::string& filter, bool& bFocus, const char* tooltip)
	{
		const float clearSize = ImGui::GetFrameHeight();
		if (bFocus)
		{
			ImGui::SetKeyboardFocusHere();
			bFocus = false;
		}
		ImGui::SetNextItemWidth(std::max(1.0f, ImGui::GetContentRegionAvail().x - clearSize - ImGui::GetStyle().ItemSpacing.x));
		NYX_UI(Nyx::UI::SearchField("##Search", &filter, "Search"));
		ImGui::SetItemTooltip("%s", tooltip);

		ImGui::SameLine();
		ImGui::BeginDisabled(filter.empty());
		if (NYX_UI(Nyx::UI::IconButton("##ClearSearch", Nyx::UI::EIcon::Clear, glm::vec2(clearSize))))
		{
			filter.clear();
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Clear the search");
	}
}
