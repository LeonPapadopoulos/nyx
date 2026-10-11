#pragma once

#include "EditorPreferences.h"
#include "SourceLocation.h"

#include <future>
#include <string>

namespace Nyx::Editor
{
	class UISourceInspector;

	// The "Preferences" window, and opening UI source in the chosen source editor (F8). Opening
	// runs on a worker, so starting an IDE cannot stall a frame; the window opens to show why
	// opening failed.
	class PreferencesWindow
	{
	public:
		void OpenSource(SourceLocation location, ESourceEditor editor);

		void Draw(EditorPreferences& preferences, const UISourceInspector& sourceInspector);

		bool bOpen = false;

	private:
		std::future<std::string> PendingSourceOpen;
		std::string SourceNavigationStatus;
	};
}
