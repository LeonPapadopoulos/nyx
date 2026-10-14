#include "MaterialDocument.h"

#include <utility>

namespace Nyx::Editor
{
	bool MaterialDocument::Open(const std::filesystem::path& assetsFolder, std::string path, std::string& outError)
	{
		Close();
		const std::filesystem::path file = (assetsFolder / path).make_preferred();
		Nyx::Engine::MaterialAsset asset;
		if (!asset.Load(file, outError))
		{
			return false;
		}
		File = file;
		Path = std::move(path);
		Asset = std::move(asset);
		SavedText = Asset.ToText();
		++Revision;
		return true;
	}

	void MaterialDocument::Close()
	{
		File.clear();
		Path.clear();
		Asset = Nyx::Engine::MaterialAsset{};
		SavedText.clear();
		UndoStack.clear();
		RedoStack.clear();
		PreviewStart.reset();
		++Revision;
	}

	bool MaterialDocument::IsDirty() const
	{
		return IsOpen() && Asset.ToText() != SavedText;
	}

	void MaterialDocument::SetValue(const std::string& name, std::optional<std::string> value, std::string label)
	{
		EndPreview("Edit");
		std::string before = Asset.ToText();
		Apply(name, value);
		std::string after = Asset.ToText();
		if (after != before)
		{
			Record(Edit{ std::move(label), std::move(before), std::move(after) });
		}
	}

	void MaterialDocument::PreviewValue(const std::string& name, std::optional<std::string> value)
	{
		if (!PreviewStart)
		{
			PreviewStart = Asset.ToText();
		}
		const std::string* current = Asset.FindValue(name);
		if (value != (current ? std::optional<std::string>(*current) : std::nullopt))
		{
			Apply(name, value);
		}
	}

	void MaterialDocument::EndPreview(std::string label)
	{
		if (!PreviewStart)
		{
			return;
		}
		std::string before = std::move(*PreviewStart);
		PreviewStart.reset();
		std::string after = Asset.ToText();
		if (after != before)
		{
			Record(Edit{ std::move(label), std::move(before), std::move(after) });
		}
	}

	bool MaterialDocument::Undo()
	{
		EndPreview("Edit");
		if (UndoStack.empty())
		{
			return false;
		}
		Edit edit = std::move(UndoStack.back());
		UndoStack.pop_back();
		SetText(edit.Before);
		RedoStack.push_back(std::move(edit));
		return true;
	}

	bool MaterialDocument::Redo()
	{
		EndPreview("Edit");
		if (RedoStack.empty())
		{
			return false;
		}
		Edit edit = std::move(RedoStack.back());
		RedoStack.pop_back();
		SetText(edit.After);
		UndoStack.push_back(std::move(edit));
		return true;
	}

	const std::string* MaterialDocument::GetUndoLabel() const
	{
		return UndoStack.empty() ? nullptr : &UndoStack.back().Label;
	}

	const std::string* MaterialDocument::GetRedoLabel() const
	{
		return RedoStack.empty() ? nullptr : &RedoStack.back().Label;
	}

	bool MaterialDocument::Save(std::string& outError)
	{
		EndPreview("Edit");
		if (!IsOpen())
		{
			outError = "no material is open";
			return false;
		}
		if (!Asset.Save(File, outError))
		{
			return false;
		}
		SavedText = Asset.ToText();
		return true;
	}

	bool MaterialDocument::FollowFile(const Nyx::Engine::MaterialAsset& file)
	{
		if (!IsOpen() || IsDirty() || IsPreviewing())
		{
			return false;
		}
		SavedText = file.ToText();
		if (Asset.ToText() != SavedText)
		{
			Asset = file;
			UndoStack.clear();
			RedoStack.clear();
			++Revision;
		}
		return true;
	}

	void MaterialDocument::Apply(const std::string& name, const std::optional<std::string>& value)
	{
		if (value)
		{
			Asset.SetValue(name, *value);
		}
		else
		{
			Asset.RemoveValue(name);
		}
		++Revision;
	}

	void MaterialDocument::SetText(const std::string& text)
	{
		// Text this document wrote itself, so it parses
		std::string error;
		Asset.Parse(text, error);
		++Revision;
	}

	void MaterialDocument::Record(Edit edit)
	{
		UndoStack.push_back(std::move(edit));
		RedoStack.clear();
	}
}
