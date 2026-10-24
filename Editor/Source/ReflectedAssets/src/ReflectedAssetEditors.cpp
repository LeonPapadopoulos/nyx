#include "ReflectedAssetEditors.h"

#include "FileWatcher.h"
#include "Log.h"
#include "Paths.h"
#include "StringUtil.h"
#include "TransactionSystem.h"

#include <algorithm>
#include <variant>

namespace Nyx::Editor
{
	namespace
	{
		bool Touches(const Transaction& transaction, const ObjectRef& ref)
		{
			return std::any_of(transaction.Changes.begin(), transaction.Changes.end(),
				[&ref](const Change& change)
				{
					const ObjectRef target = std::visit(
						[](const auto& payload)
						{
							return payload.Target;
						},
						change.Payload);
					return target == ref;
				});
		}

		// Windows' file names don't tell case apart
		std::string ComparablePath(const std::filesystem::path& path)
		{
			std::string text = Nyx::ToLower(path.lexically_normal().generic_string());
			while (!text.empty() && text.back() == '/')
			{
				text.pop_back();
			}
			return text;
		}
	}

	ReflectedAssetEditors::ReflectedAssetEditors() = default;
	ReflectedAssetEditors::~ReflectedAssetEditors() = default;

	void ReflectedAssetEditors::Attach(TransactionSystem& transactions)
	{
		transactions.RegisterDomain(EObjectDomain::ReflectedAsset, this);
		transactions.Subscribe(this);
	}

	void ReflectedAssetEditors::SetAssetFolders(std::vector<std::filesystem::path> folders)
	{
		AssetFolders = std::move(folders);
	}

	void ReflectedAssetEditors::Watch()
	{
		Watcher = std::make_unique<Nyx::FileWatcher>();
		for (const std::filesystem::path& folder : AssetFolders)
		{
			if (!Watcher->Watch(folder))
			{
				LOG_WARNING("Reflected assets: can't watch '{0}': {1}", folder.string(), Watcher->GetProblem());
			}
		}
	}

	void ReflectedAssetEditors::Update()
	{
		if (Watcher)
		{
			const std::vector<std::filesystem::path> changes = Watcher->TakeChanges();
			if (!changes.empty())
			{
				ApplyFileChanges(changes);
			}
		}
	}

	ReflectedAssetDocument* ReflectedAssetEditors::Open(const std::string& path, std::string& outError)
	{
		if (ReflectedAssetDocument* open = Find(path))
		{
			return open;
		}
		const Nyx::ReflectedAssetType* type = Nyx::ReflectedAssetTypes::Get().FindForPath(path);
		if (!type)
		{
			outError = "no reflected asset type has the extension of '" + path + "'";
			return nullptr;
		}
		std::unique_ptr<ReflectedAssetDocument> document = ReflectedAssetDocument::Load(*type, path, FindFile(path), ++OpenCount, outError);
		if (!document)
		{
			return nullptr;
		}
		Documents.push_back(std::move(document));
		return Documents.back().get();
	}

	void ReflectedAssetEditors::Close(const std::string& path)
	{
		std::erase_if(Documents, [&path](const std::unique_ptr<ReflectedAssetDocument>& document)
			{
				return document->GetPath() == path;
			});
	}

	ReflectedAssetDocument* ReflectedAssetEditors::Find(const std::string& path)
	{
		const auto found = std::find_if(Documents.begin(), Documents.end(), [&path](const std::unique_ptr<ReflectedAssetDocument>& document)
			{
				return document->GetPath() == path;
			});
		return found != Documents.end() ? found->get() : nullptr;
	}

	std::string ReflectedAssetEditors::Create(const Nyx::ReflectedAssetType& type, const std::filesystem::path& folder, std::string& outError)
	{
		if (AssetFolders.empty())
		{
			outError = "there is no asset folder";
			return {};
		}

		// Named apart from what is there
		std::filesystem::path relative;
		for (int number = 1; relative.empty() || std::filesystem::exists(AssetFolders.front() / relative); ++number)
		{
			const std::string name = "New" + type.TypeId + (number == 1 ? std::string() : std::to_string(number));
			relative = folder / (name + type.Extension);
		}

		// Every value at its default, written out to be changed
		Nyx::ReflectedAssetText text;
		if (!type.Description.empty())
		{
			text.AddComment(type.Description);
		}
		const Nyx::ReflectedObjectPtr values = Nyx::MakeReflectedObject(type.Struct);
		text.WriteAll(values.get(), type.Struct);
		const std::filesystem::path file = AssetFolders.front() / relative;
		std::error_code error;
		std::filesystem::create_directories(file.parent_path(), error);
		if (!text.Save(file, outError))
		{
			return {};
		}
		LOG_INFO("Made {0} '{1}'", type.DisplayName, relative.generic_string());
		return relative.generic_string();
	}

	void ReflectedAssetEditors::ApplyFileChanges(const std::vector<std::filesystem::path>& changes)
	{
		std::vector<std::string> changed;
		for (const std::unique_ptr<ReflectedAssetDocument>& document : Documents)
		{
			const std::string file = ComparablePath(document->GetFile());
			// The file itself, or a folder it is in: the watcher names its folder when it lost count
			const bool bChanged = std::any_of(changes.begin(), changes.end(), [&file](const std::filesystem::path& change)
				{
					const std::string path = ComparablePath(change);
					return file.starts_with(path) && (file.size() == path.size() || file[path.size()] == '/');
				});
			if (bChanged)
			{
				changed.push_back(document->GetPath());
			}
		}

		for (const std::string& path : changed)
		{
			ReflectedAssetDocument& document = *Find(path);
			Nyx::ReflectedAssetText text;
			std::string error;
			// Gone, or being written: it says so if it doesn't come back
			if (!text.Load(document.GetFile(), error))
			{
				document.SetChangedOnDisk(true);
				continue;
			}
			// What it has, e.g. once saved
			if (text.ToText() == document.GetText())
			{
				document.SetChangedOnDisk(false);
				continue;
			}
			if (document.IsDirty())
			{
				LOG_WARNING("{0} '{1}' changed on disk; it keeps the changes not saved", document.GetType().DisplayName, path);
				document.SetChangedOnDisk(true);
				continue;
			}
			if (Reload(path, error))
			{
				LOG_INFO("{0} '{1}' read again", Find(path)->GetType().DisplayName, path);
			}
		}
	}

	bool ReflectedAssetEditors::Reload(const std::string& path, std::string& outError)
	{
		const auto found = std::find_if(Documents.begin(), Documents.end(), [&path](const std::unique_ptr<ReflectedAssetDocument>& document)
			{
				return document->GetPath() == path;
			});
		if (found == Documents.end())
		{
			outError = "'" + path + "' isn't open";
			return false;
		}
		std::unique_ptr<ReflectedAssetDocument> document =
			ReflectedAssetDocument::Load((*found)->GetType(), path, (*found)->GetFile(), ++OpenCount, outError);
		if (!document)
		{
			(*found)->SetChangedOnDisk(true);
			return false;
		}
		// The games get the file's values too, which they may have had unsaved ones over, or not read
		document->SetSentRevision(document->GetRevision() + 1);
		*found = std::move(document);
		return true;
	}

	std::vector<Nyx::AssetValuesMessage> ReflectedAssetEditors::TakeValuesToSend()
	{
		std::vector<Nyx::AssetValuesMessage> messages;
		for (const std::unique_ptr<ReflectedAssetDocument>& document : Documents)
		{
			if (document->GetRevision() != document->GetSentRevision())
			{
				document->SetSentRevision(document->GetRevision());
				messages.push_back(Nyx::AssetValuesMessage{ document->GetPath(), document->GetText() });
			}
		}
		return messages;
	}

	std::vector<Nyx::AssetValuesMessage> ReflectedAssetEditors::GetUnsavedValues() const
	{
		std::vector<Nyx::AssetValuesMessage> messages;
		for (const std::unique_ptr<ReflectedAssetDocument>& document : Documents)
		{
			if (document->IsDirty())
			{
				messages.push_back(Nyx::AssetValuesMessage{ document->GetPath(), document->GetText() });
			}
		}
		return messages;
	}

	ReflectedAssetDocument* ReflectedAssetEditors::FindByRef(const ObjectRef& root)
	{
		const auto found = std::find_if(Documents.begin(), Documents.end(), [&root](const std::unique_ptr<ReflectedAssetDocument>& document)
			{
				return document->GetRef() == root;
			});
		return found != Documents.end() ? found->get() : nullptr;
	}

	std::filesystem::path ReflectedAssetEditors::FindFile(const std::string& path) const
	{
		return Nyx::Paths::FindAssetDirIn(AssetFolders, path) / path;
	}

	void* ReflectedAssetEditors::ResolveMutable(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		ReflectedAssetDocument* document = FindByRef(root);
		return document && &typeMetadata == document->GetType().Struct.Metadata ? document->GetData() : nullptr;
	}

	bool ReflectedAssetEditors::CreateRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	bool ReflectedAssetEditors::DeleteRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	void ReflectedAssetEditors::EnumerateSubobjects(EditorTransactionContext& /*context*/, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects)
	{
		if (ReflectedAssetDocument* document = FindByRef(root))
		{
			outSubobjects.push_back({ document->GetType().Struct.Metadata, document->GetData() });
		}
	}

	bool ReflectedAssetEditors::EnsureSubobject(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		ReflectedAssetDocument* document = FindByRef(root);
		return document && &typeMetadata == document->GetType().Struct.Metadata;
	}

	bool ReflectedAssetEditors::RemoveSubobject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/, const Nyx::Reflection::TypeMetadata& /*typeMetadata*/)
	{
		return false;
	}

	void ReflectedAssetEditors::OnTransactionCommitted(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void ReflectedAssetEditors::OnTransactionApplied(const Transaction& transaction, bool /*bWasUndo*/)
	{
		OnChanged(transaction);
	}

	void ReflectedAssetEditors::OnTransactionPreviewed(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void ReflectedAssetEditors::OnChanged(const Transaction& transaction)
	{
		for (const std::unique_ptr<ReflectedAssetDocument>& document : Documents)
		{
			if (Touches(transaction, document->GetRef()))
			{
				document->OnValuesChanged();
			}
		}
	}
}
