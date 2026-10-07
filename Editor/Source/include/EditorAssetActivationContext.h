#pragma once

#include "AssetTypeRegistry.h"

namespace Nyx::Editor
{
	class EditorLayer;

	class EditorAssetActivationContext final : public IAssetActivationContext
	{
	public:
		explicit EditorAssetActivationContext(EditorLayer& editorLayer)
			: Editor(editorLayer)
		{
		}

		EditorLayer& Editor;
	};
}