#pragma once

#include <string>
#include <string_view>

namespace Nyx::Editor
{
	// Whether a name has `filter` in it, whatever the case; every name has an empty one
	bool NameMatchesFilter(std::string_view name, std::string_view filter);

	// A search field over the window's width and a button that clears it. bFocus: takes the keyboard
	// focus, once.
	void DrawSearchRow(std::string& filter, bool& bFocus, const char* tooltip);
}
