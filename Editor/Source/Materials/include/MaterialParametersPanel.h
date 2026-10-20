#pragma once

#include "MaterialDocument.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Nyx
{
	class IMaterialSource;
	struct MaterialView;
}

namespace Nyx
{
	struct ShaderField;
}

namespace Nyx::Editor
{
	// The Parameters window: edits the open material's values, with widgets made from its shader's layout
	class MaterialParametersPanel
	{
	public:
		// `assetFolders`: where material paths are looked for, in order (the project's assets, then
		// the engine's); a material is opened, and saved, where it is found
		explicit MaterialParametersPanel(std::vector<std::filesystem::path> assetFolders);

		// Opens the material ("Materials/Stone.nyxmat") and the window: at once, or, while the
		// one open has unsaved values, after asking in the window (the next Draw())
		void OpenMaterial(std::string path);

		// An entity that draws with the open material, which a click selects
		struct MaterialUser
		{
			std::string Name;
			std::function<void()> Select;
		};

		// What the window picks from and lists. The caller keeps the lists and builds them again
		// only when the assets or the scene change; a list left out is empty.
		struct Choices
		{
			// The shaders a material can be on ("Shaders/Stone.slang"), and the materials that can
			// be the parent ("Materials/Stone.nyxmat")
			const std::vector<std::string>* Shaders = nullptr;
			const std::vector<std::string>* Materials = nullptr;
			// The open material's instances, and the entities of the scene that use it
			const std::vector<std::string>* Instances = nullptr;
			const std::vector<MaterialUser>* Users = nullptr;
		};

		// Every frame: the open material's values to the renderer, the file's to the document
		// when it was read again, and the window while open
		void Draw(Nyx::IMaterialSource& materials, const Choices& choices = {});

		// Undo, redo and save while the window has focus. True when it has: the keys are the
		// material's then, not the scene's.
		bool HandleHotkeys();

		MaterialDocument& GetDocument() { return Document; }

		// What opens another material: a parent or an instance
		std::function<void(const std::string& path)> OpenOther;

		// Called with every material's values sent to the renderer: the open one's as they
		// change, a discarded one's file. The editor sends them to its games.
		std::function<void(const std::string& path, const Nyx::MaterialAsset& asset)> ValuesSent;
		// "Saved ...", "Opened ...", or what went wrong last
		const std::string& GetStatus() const { return Status; }

		bool bOpen = false;

	private:
		// The document's values to the renderer, the file's to the document
		void Sync(Nyx::IMaterialSource& materials);
		void SendValues(Nyx::IMaterialSource& materials, const std::string& path, const Nyx::MaterialAsset& asset);
		void Open(const std::string& path, Nyx::IMaterialSource& materials);
		void Save();
		void DrawDiscardPopup(Nyx::IMaterialSource& materials);
		// The parent or the shader, the domain, the instances, the entities that use it
		void DrawMaterial(const Nyx::MaterialView& view);
		void DrawParameters(const Nyx::MaterialView& view);
		void DrawFields(const std::vector<Nyx::ShaderField>& fields, const std::string& prefix, uint32_t base, const Nyx::MaterialView& view, const std::string* category);
		void DrawField(const Nyx::ShaderField& field, const std::string& name, uint32_t offset, const Nyx::MaterialView& view);
		// The value as typed text, for what has no widget of its own (and a value that doesn't
		// fit its widget, e.g. typed wrong in the file); set when the field is left
		void DrawTextValue(const std::string& name, const std::string& current, const char* hint, const std::string& label);
		// The values the file sets that the shader has no parameter for
		void DrawUnknownValues(const Nyx::MaterialView& view);

		// Draw()'s choices, for the functions it calls
		Choices CurrentChoices;

		std::vector<std::filesystem::path> AssetFolders;
		MaterialDocument Document;

		// What the renderer has: the file's revision taken, the document's revision sent
		uint64_t SeenFileRevision = 0;
		uint64_t SentRevision = 0;
		// A material whose unsaved values were discarded: the renderer draws its file again
		std::string Discarded;

		// The material asked to open, opened in the next Draw(), after asking while there are
		// unsaved values
		std::string PendingOpen;
		bool bAskToDiscard = false;

		bool bFocused = false;
		std::string Status;
		bool bStatusFailed = false;

		// The undo step's name for the edit being dragged, e.g. "Edit Roughness"
		std::string PreviewLabel;
		// The text fields' text while one is typed in, by parameter name
		std::map<std::string, std::string> TextFields;
		std::string ActiveTextField;
	};
}
