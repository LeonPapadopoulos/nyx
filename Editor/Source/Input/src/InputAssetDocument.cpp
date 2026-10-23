#include "InputAssetDocument.h"

#include "InputSystem.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"
#include "TransactionSystem.h"

#include <algorithm>
#include <variant>

namespace Nyx::Editor
{
	namespace
	{
		bool TouchesAsset(const Transaction& transaction)
		{
			return std::any_of(transaction.Changes.begin(), transaction.Changes.end(),
				[](const Change& change)
				{
					const ObjectRef target = std::visit(
						[](const auto& payload)
						{
							return payload.Target;
						},
						change.Payload);
					return target == InputAssetDocument::GetRef();
				});
		}
	}

	bool InputAssetDocument::Load(Nyx::Input::InputSystem& input, const std::filesystem::path& path)
	{
		std::error_code error;
		if (!std::filesystem::exists(path, error))
		{
			if (Nyx::Input::SaveInputAsset(Nyx::Input::MakeDefaultInputAsset(), path))
			{
				LOG_INFO("Input: made {0} from the defaults", path.string());
			}
		}

		Nyx::ReadWarnings warnings;
		std::optional<Nyx::Input::InputAssetData> asset = Nyx::Input::LoadInputAsset(path, warnings);
		warnings.Log(path.string());
		if (!asset)
		{
			LOG_WARNING("Input: could not read {0}; using the built-in bindings", path.string());
			return false;
		}

		const std::filesystem::path playerPath = Nyx::Input::GetPlayerInputSettingsPath();
		if (!playerPath.empty() && std::filesystem::exists(playerPath, error))
		{
			Nyx::ReadWarnings playerWarnings;
			Player = Nyx::Input::LoadPlayerSettings(playerPath, playerWarnings);
		}

		Input = &input;
		Path = path;
		Asset = std::move(*asset);
		Revision = SavedRevision = 0;
		Apply();
		RefreshChecks();

		for (const Nyx::Input::InputAssetProblem& problem : Problems)
		{
			LOG_WARNING("Input: {0}: {1}: {2}", Path.filename().string(), problem.Where, problem.What);
		}
		for (const Nyx::Input::InputCheckFinding& finding : Nyx::Input::RunInputChecks(Asset))
		{
			LOG_WARNING("Input: {0}: {1}: {2}: {3}", Path.filename().string(), Nyx::Input::GetInputCheckName(finding.Check),
				Nyx::Input::DescribeWhere(Asset, finding), finding.Message);
		}
		return true;
	}

	void InputAssetDocument::Attach(TransactionSystem& transactions)
	{
		transactions.RegisterDomain(EObjectDomain::InputAsset, this);
		transactions.Subscribe(this);
	}

	bool InputAssetDocument::Save()
	{
		if (!IsLoaded() || !Nyx::Input::SaveInputAsset(Asset, Path))
		{
			LOG_ERROR("Input: could not save {0}", Path.string());
			return false;
		}
		SavedRevision = Revision;
		LOG_INFO("Input: saved {0}", Path.string());
		return true;
	}

	void InputAssetDocument::SetApplyPlayerSettings(bool bApply)
	{
		bApplyPlayerSettings = bApply;
		Apply();
	}

	void InputAssetDocument::RefreshChecks()
	{
		if (Input)
		{
			const Nyx::Input::InputCheckRuntime runtime = Nyx::Input::MakeCheckRuntime(*Input);
			Findings = Nyx::Input::RunInputChecks(Asset, &runtime);
		}
	}

	void InputAssetDocument::Apply()
	{
		if (Input)
		{
			Problems = Nyx::Input::ApplyInputAsset(*Input, Asset, bApplyPlayerSettings ? GetPlayerSettings() : nullptr);
		}
	}

	void* InputAssetDocument::ResolveMutable(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return root == GetRef() && &typeMetadata == &Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>() ? &Asset : nullptr;
	}

	bool InputAssetDocument::CreateRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	bool InputAssetDocument::DeleteRootObject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/)
	{
		return false;
	}

	void InputAssetDocument::EnumerateSubobjects(
		EditorTransactionContext& /*context*/, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects)
	{
		if (root == GetRef())
		{
			outSubobjects.push_back({ &Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>(), &Asset });
		}
	}

	bool InputAssetDocument::EnsureSubobject(EditorTransactionContext& /*context*/, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata)
	{
		return root == GetRef() && &typeMetadata == &Nyx::Reflection::GetTypeMetadata<Nyx::Input::InputAssetData>();
	}

	bool InputAssetDocument::RemoveSubobject(EditorTransactionContext& /*context*/, const ObjectRef& /*root*/, const Nyx::Reflection::TypeMetadata& /*typeMetadata*/)
	{
		return false;
	}

	void InputAssetDocument::OnTransactionCommitted(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void InputAssetDocument::OnTransactionApplied(const Transaction& transaction, bool /*bWasUndo*/)
	{
		OnChanged(transaction);
	}

	void InputAssetDocument::OnTransactionPreviewed(const Transaction& transaction)
	{
		OnChanged(transaction);
	}

	void InputAssetDocument::OnChanged(const Transaction& transaction)
	{
		if (TouchesAsset(transaction))
		{
			++Revision;
			Apply();
			RefreshChecks();
		}
	}
}
