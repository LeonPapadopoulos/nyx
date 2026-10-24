#include "ReflectedAssetDocument.h"

#include "Log.h"

namespace Nyx::Editor
{
	ReflectedAssetDocument::ReflectedAssetDocument(const Nyx::ReflectedAssetType& type, std::string path, std::filesystem::path file, uint64_t id)
		: Type(&type)
		, Path(std::move(path))
		, File(std::move(file))
		, Id(id)
		, Data(Nyx::MakeReflectedObject(type.Struct))
	{
	}

	std::unique_ptr<ReflectedAssetDocument> ReflectedAssetDocument::Load(
		const Nyx::ReflectedAssetType& type, std::string path, std::filesystem::path file, uint64_t id, std::string& outError)
	{
		Nyx::ReflectedAssetText text;
		if (!text.Load(file, outError))
		{
			return nullptr;
		}

		std::unique_ptr<ReflectedAssetDocument> document(new ReflectedAssetDocument(type, std::move(path), std::move(file), id));
		document->Text = std::move(text);
		std::vector<Nyx::PropertyProblem> problems;
		document->Text.Read(document->Data.get(), *type.Struct.Metadata, &problems);
		for (const Nyx::PropertyProblem& problem : problems)
		{
			LOG_WARNING("{0} '{1}': {2}: {3}", type.DisplayName, document->Path, problem.Path, problem.Message);
		}
		document->Check();
		return document;
	}

	void ReflectedAssetDocument::OnValuesChanged()
	{
		Text.Write(Data.get(), Type->Struct);
		Check();
		++Revision;
	}

	bool ReflectedAssetDocument::Save(std::string& outError)
	{
		if (!Text.Save(File, outError))
		{
			return false;
		}
		SavedRevision = Revision;
		bChangedOnDisk = false;
		LOG_INFO("Saved {0} '{1}'", Type->DisplayName, Path);
		return true;
	}

	void ReflectedAssetDocument::Check()
	{
		// The lines read again into an object of their own, for those that can't be read
		Problems.clear();
		const Nyx::ReflectedObjectPtr lines = Nyx::MakeReflectedObject(Type->Struct);
		Text.Read(lines.get(), *Type->Struct.Metadata, &Problems);
		std::vector<Nyx::PropertyProblem> problems = Nyx::ValidateReflectedAsset(*Type, Data.get());
		Problems.insert(Problems.end(), problems.begin(), problems.end());
	}
}
