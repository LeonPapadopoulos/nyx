#pragma once

#include "MaterialParametersPanel.h"
#include "MaterialPreviewPanel.h"
#include "Paths.h"
#include "ShaderLogPanel.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx
{
	class IEditorRenderer;
}

namespace Nyx::Editor
{
	class AssetDatabase;
	class SceneEditor;
	class Workspaces;

	// The Materials workspace: the Parameters, Preview and Shader Log windows, what they are fed
	// from the assets and the scene, and New Material
	class MaterialWorkspace
	{
	public:
		// Both must outlive the workspace
		MaterialWorkspace(AssetDatabase& assets, Workspaces& layouts);

		// Opens the material ("Materials/Stone.nyxmat") in the Parameters window, and the Materials
		// workspace
		void OpenMaterial(const std::string& path);

		// The asset browser's New Material: asks for a name, then makes a shader and a material
		void AskNewMaterial();
		void CreateMaterialForShader(const std::string& shaderPath);
		void CreateMaterialInstance(const std::string& parentPath);

		// New Material's name question, while asked; early in the frame, before the windows
		void DrawNewMaterialPopup();

		// The Parameters, Preview and Shader Log windows. `editing` gives the entities that use the
		// open material, and the selected mesh for the preview.
		void Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing);

		// The Parameters window's undo and save keys; true while it has focus
		bool HandleHotkeys();

		// Opens a shader file at a line, for the Shader Log, the preview and a new material's shader
		void SetOpenFileAtLine(OpenFileAtLine open);

		// Before the renderer goes
		void Release();

		MaterialParametersPanel& GetParameters() { return Parameters; }
		MaterialPreviewPanel& GetPreview() { return Preview; }
		ShaderLogPanel& GetShaderLog() { return ShaderLog; }

	private:
		bool CreateNewMaterial();

		// The shaders and materials the Parameters window picks from, again when the assets changed
		void UpdateChoices();

		// The materials whose parent is `path`, read from their files at most once a second
		const std::vector<std::string>& FindInstancesOf(const std::string& path);

		AssetDatabase& Assets;
		Workspaces& Layouts;
		OpenFileAtLine OpenAtLine;

		// The material open for editing
		MaterialParametersPanel Parameters{ Nyx::Paths::GetAssetsDir() };
		// The material open, on a shape in a world of its own
		MaterialPreviewPanel Preview;
		// Every shader's last compile, and what the compiler said
		ShaderLogPanel ShaderLog;

		// What the Parameters window picks from, built from this revision of the assets (0: not yet)
		uint64_t ChoicesRevision = 0;
		std::vector<std::string> Shaders;
		std::vector<std::string> Materials;
		// The entities that use the open material, again every frame, as the scene changes any time
		std::vector<MaterialParametersPanel::MaterialUser> Users;

		// FindInstancesOf()'s last answer: instance files change on disk without a rescan
		std::string InstancesOf;
		std::vector<std::string> FoundInstances;
		double InstancesFoundAt = -1.0;

		// New Material's question
		bool bAskNewMaterialName = false;
		std::string NewMaterialName;
		std::string NewMaterialError;
	};
}
