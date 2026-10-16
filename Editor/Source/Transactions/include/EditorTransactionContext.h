#pragma once

namespace Nyx::Editor
{
	class SceneDocument;

	struct EditorTransactionContext
	{
		SceneDocument* ActiveScene = nullptr;
	};
}