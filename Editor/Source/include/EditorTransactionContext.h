#pragma once

namespace Nyx
{
	class SceneDocument;
}

namespace Nyx::Editor
{
	struct EditorTransactionContext
	{
		Nyx::SceneDocument* ActiveScene = nullptr;
	};
}