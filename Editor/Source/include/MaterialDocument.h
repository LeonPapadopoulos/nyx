#pragma once

#include "MaterialAsset.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace Nyx::Editor
{
	// A material file open for editing: its values, whether they differ from the file's, and an
	// undo history of its own, so that undo in the material doesn't undo the scene and the other
	// way round. Values are text, as the file holds them; a value removed takes the shader's
	// default. Every other line of the file stays as it is, and undo puts the file's text back as
	// it was, lines in their places.
	//
	// It knows nothing of the renderer: the Parameters window (MaterialParametersPanel) shows its
	// values in the scene views (IMaterialSource::SetMaterialValues()) and hands it the file when
	// that is read again.
	class MaterialDocument
	{
	public:
		// `path`: relative to `assetsFolder`, as materials are named ("Materials/Stone.nyxmat").
		// False when the file can't be read, with why in `outError`; the document is closed then.
		bool Open(const std::filesystem::path& assetsFolder, std::string path, std::string& outError);
		void Close();
		bool IsOpen() const { return !Path.empty(); }

		const std::string& GetPath() const { return Path; }
		const std::filesystem::path& GetFile() const { return File; }
		const Nyx::Engine::MaterialAsset& GetAsset() const { return Asset; }

		// Whether the values differ from the file's, as last opened, saved or followed
		bool IsDirty() const;

		// Goes up with every change of the values, e.g. for whoever draws them
		uint64_t GetRevision() const { return Revision; }

		// One edit: `name` set to `value`, or removed (nullopt) so that the shader's default
		// shows. Nothing is recorded when it changes nothing.
		void SetValue(const std::string& name, std::optional<std::string> value, std::string label);
		// Several lines in one edit, e.g. a parent instead of a shader
		void SetValues(const std::vector<std::pair<std::string, std::optional<std::string>>>& values, std::string label);

		// An edit over several frames, e.g. a slider being dragged: the value changes at once,
		// and EndPreview() records all of it as one undo step from where it started
		void PreviewValue(const std::string& name, std::optional<std::string> value);
		void EndPreview(std::string label);
		bool IsPreviewing() const { return PreviewStart.has_value(); }

		bool Undo();
		bool Redo();
		// The label of the step Undo() or Redo() would take; null when there is none
		const std::string* GetUndoLabel() const;
		const std::string* GetRedoLabel() const;

		// Writes the values to the file
		bool Save(std::string& outError);

		// The file as read again, e.g. saved in a text editor: taken when nothing is unsaved here,
		// and the undo history starts anew, as it couldn't take back what changed in the file.
		// True when taken; false when the values differ from the file's, which they keep.
		bool FollowFile(const Nyx::Engine::MaterialAsset& file);

	private:
		// The file's text before and after, which material files are small enough for
		struct Edit
		{
			std::string Label;
			std::string Before;
			std::string After;
		};

		void Apply(const std::string& name, const std::optional<std::string>& value);
		void SetText(const std::string& text);
		void Record(Edit edit);

		std::filesystem::path File;
		std::string Path;
		Nyx::Engine::MaterialAsset Asset;
		// The file's text as last opened, saved or followed
		std::string SavedText;
		uint64_t Revision = 0;

		std::vector<Edit> UndoStack;
		std::vector<Edit> RedoStack;

		// While previewing: the text before
		std::optional<std::string> PreviewStart;
	};
}
