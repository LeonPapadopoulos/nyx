#pragma once

#include "InspectorDrawContext.h"
#include "TransactionObjectRef.h"

#include <map>
#include <string>

namespace Nyx::Editor
{
	class AssetDatabase;
	class ReflectedAssetDocument;
	class ReflectedAssetEditors;
	class TransactionSystem;

	// A window for each asset open in ReflectedAssetEditors, titled by its type and name: its values in
	// the details table with the editor's undo, a problem under the value it is about, Save (Ctrl+S
	// while it has the focus), and a dirty marker. Named entries are added with a name of their own,
	// renamed by their Name and taken away by their row's menu. Closing a window closes its asset,
	// dropping changes not saved. When its file changed on disk while it had changes, it says so, with
	// Reload.
	class ReflectedAssetWindows
	{
	public:
		// `assets`: what asset references pick from
		void Draw(ReflectedAssetEditors& editors, TransactionSystem& transactions, const AssetDatabase* assets);

		// Ctrl+S saves the asset whose window has the focus; true when it did. Undo and redo are the
		// editor's, so they pass.
		bool HandleHotkeys(ReflectedAssetEditors& editors);

	private:
		struct Window
		{
			InspectorDrawContext DrawContext;
			// The asset it drew last: another one, once read again, ends the edit in progress
			ObjectRef Asset;
			// Why the last save or reload failed; empty once one worked
			std::string Error;
		};

		void Save(ReflectedAssetDocument& document, Window& window);

		// By the asset's path
		std::map<std::string, Window> Windows;
		std::string FocusedPath;
	};
}
