#pragma once

namespace Nyx::Editor
{
	class ImGuiDebugTools
	{
	public:
		// Refresh debugger availability before ImGui processes input for the next frame.
		void BeforeFrame();
		void DrawMenu();
		void DrawWindows();

	private:
		bool bShowMetrics = false;
		bool bShowDebugLog = false;
		bool bShowIDStack = false;
		bool bShowStyleEditor = false;
		bool bShowDemo = false;
	};
}
