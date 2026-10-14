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

namespace Nyx::Engine
{
	struct ShaderField;
}

namespace Nyx::Editor
{
	// The "Parameters" window (Design/shader-materials.md, stage 2): the material open for
	// editing (a MaterialDocument), its shader's parameters as widgets made from the shader's
	// layout, by category: sliders for [Range], swatches for [Color], checkboxes for bools and
	// [Switch], combos for [Enum], drags for other numbers, a path for each texture. A value
	// that differs from the shader's default has a reset button. Every change shows in the scene
	// views in the same frame; a drag is one undo step. Save writes the file; Revert reads it
	// again. While the window has focus, Ctrl+Z and Ctrl+Y undo and redo in the material, and
	// Ctrl+S saves it.
	//
	// The file is followed when it is read again, e.g. saved in a text editor, while nothing here
	// is unsaved. Opening another material asks first while there are unsaved values. Above the
	// parameters, the material's shader (another can be chosen, as an edit), its domain, and the
	// entities of the scene that use it.
	class MaterialParametersPanel
	{
	public:
		explicit MaterialParametersPanel(std::filesystem::path assetsFolder);

		// Opens the material ("Materials/Stone.nyxmat") and the window: at once, or, while the
		// one open has unsaved values, after asking in the window (the next Draw())
		void OpenMaterial(std::string path);

		// Every frame: the open material's values to the renderer, the file's to the document
		// when it was read again, and the window while open
		void Draw(Nyx::IMaterialSource& materials);

		// Undo, redo and save while the window has focus. True when it has: the keys are the
		// material's then, not the scene's.
		bool HandleHotkeys();

		MaterialDocument& GetDocument() { return Document; }

		// An entity that draws with the open material, which a click selects
		struct MaterialUser
		{
			std::string Name;
			std::function<void()> Select;
		};
		// Set before Draw(): the shaders a material can be on ("Shaders/Stone.slang"), and the
		// entities of the scene that use the open material
		std::vector<std::string> ShaderChoices;
		std::vector<MaterialUser> Users;
		// "Saved ...", "Opened ...", or what went wrong last
		const std::string& GetStatus() const { return Status; }

		bool bOpen = false;

	private:
		// The document's values to the renderer, the file's to the document
		void Sync(Nyx::IMaterialSource& materials);
		void Open(const std::string& path, Nyx::IMaterialSource& materials);
		void Save();
		void DrawDiscardPopup(Nyx::IMaterialSource& materials);
		// The shader, the domain, the entities that use it
		void DrawMaterial();
		void DrawParameters(const Nyx::MaterialView& view);
		void DrawFields(const std::vector<Nyx::Engine::ShaderField>& fields, const std::string& prefix, uint32_t base, const Nyx::MaterialView& view, const std::string* category);
		void DrawField(const Nyx::Engine::ShaderField& field, const std::string& name, uint32_t offset, const Nyx::MaterialView& view);
		// The value as typed text, for what has no widget of its own (and a value that doesn't
		// fit its widget, e.g. typed wrong in the file); set when the field is left
		void DrawTextValue(const std::string& name, const std::string& current, const char* hint, const std::string& label);
		// The values the file sets that the shader has no parameter for
		void DrawUnknownValues(const Nyx::MaterialView& view);

		std::filesystem::path AssetsFolder;
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
