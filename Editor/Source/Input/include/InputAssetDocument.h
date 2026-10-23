#pragma once

#include "InputAsset.h"
#include "InputChecks.h"
#include "TransactionDomain.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Nyx::Input
{
	class InputSystem;
}

namespace Nyx::Editor
{
	class TransactionSystem;

	// The input asset the editor edits (Assets/Input/Default.nyxinput), as one object of the
	// editor's undo (EObjectDomain::InputAsset): the Input window's Tuning tab edits it through the
	// details table and records the edits like any others, so Ctrl+Z undoes them in turn with the
	// scene's. Every edit, undo, redo and value being dragged puts the asset into the input system
	// again, so the scene views feel the change at once.
	class InputAssetDocument final : public ITransactionDomain, public ITransactionSubscriber
	{
	public:
		static ObjectRef GetRef()
		{
			return ObjectRef{ EObjectDomain::InputAsset, InspectorTargetId{ 1 } };
		}

		// Reads the asset, made from the defaults first if the file isn't there, and puts it into
		// the input system, which must outlive this. Also reads the player's settings, if any, to
		// show their layer. False if there is no asset to read; the input system keeps what it has.
		bool Load(Nyx::Input::InputSystem& input, const std::filesystem::path& path);

		// Edits, undo and redo of the asset reach it from then on
		void Attach(TransactionSystem& transactions);

		bool Save();

		bool IsLoaded() const
		{
			return Input != nullptr;
		}

		Nyx::Input::InputAssetData& GetAsset()
		{
			return Asset;
		}

		const Nyx::Input::InputAssetData& GetAsset() const
		{
			return Asset;
		}

		const std::filesystem::path& GetPath() const
		{
			return Path;
		}

		// Changed since it was read or saved
		bool IsDirty() const
		{
			return Revision != SavedRevision;
		}

		// Counts changes, so views can tell when to look again
		uint64_t GetRevision() const
		{
			return Revision;
		}

		const Nyx::Input::InputPlayerSettingsData* GetPlayerSettings() const
		{
			return Player ? &*Player : nullptr;
		}

		// Whether the input system gets the player's settings too, as the game does
		bool IsApplyingPlayerSettings() const
		{
			return bApplyPlayerSettings;
		}
		void SetApplyPlayerSettings(bool bApply);

		// What ApplyInputAsset() couldn't use, as of the last change
		const std::vector<Nyx::Input::InputAssetProblem>& GetProblems() const
		{
			return Problems;
		}

		// The checks, with what the input system knows now (the stack, the actions the code
		// consumed); run again by RefreshChecks(), with another input system's knowledge if given,
		// e.g. a game's the Input window shows
		const std::vector<Nyx::Input::InputCheckFinding>& GetFindings() const
		{
			return Findings;
		}
		void RefreshChecks(const Nyx::Input::InputSystem* shown = nullptr);

		// Puts the asset into the input system again: A while A is shown, the asset otherwise
		void Apply();

		// A/B: Keep as A remembers the tuning as it is; edits after that are B. Swapping shows one
		// or the other, in the editor and every running game, to feel the difference in a second.
		// An edit shows B again.
		void KeepAsA();
		void ForgetA();
		bool HasA() const
		{
			return KeptA.has_value();
		}

		void SetShowingA(bool bShowA);
		bool IsShowingA() const
		{
			return bShowingA && KeptA;
		}

		const Nyx::Input::InputAssetData* GetA() const
		{
			return KeptA ? &*KeptA : nullptr;
		}

		// What the input system and the games have: A or the asset
		const Nyx::Input::InputAssetData& GetAppliedAsset() const
		{
			return IsShowingA() ? *KeptA : Asset;
		}

		// The swap chord: the pad's Back (View, Share, Create) held and its top face button pressed,
		// read from the input's devices whatever its contexts do, so it works while a game has the
		// focus. Once per frame.
		void HandleSwapChord(const Nyx::Input::InputSystem& input);

		// ITransactionDomain: one root object, the asset, with itself as its only subobject
		void* ResolveMutable(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool CreateRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		bool DeleteRootObject(EditorTransactionContext& context, const ObjectRef& root) override;
		void EnumerateSubobjects(EditorTransactionContext& context, const ObjectRef& root, std::vector<ReflectedObjectView>& outSubobjects) override;
		bool EnsureSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;
		bool RemoveSubobject(EditorTransactionContext& context, const ObjectRef& root, const Nyx::Reflection::TypeMetadata& typeMetadata) override;

		// ITransactionSubscriber
		void OnTransactionCommitted(const Transaction& transaction) override;
		void OnTransactionApplied(const Transaction& transaction, bool bWasUndo) override;
		void OnTransactionPreviewed(const Transaction& transaction) override;

	private:
		void OnChanged(const Transaction& transaction);

		Nyx::Input::InputSystem* Input = nullptr;
		std::filesystem::path Path;
		Nyx::Input::InputAssetData Asset;
		std::optional<Nyx::Input::InputPlayerSettingsData> Player;
		bool bApplyPlayerSettings = false;

		uint64_t Revision = 0;
		uint64_t SavedRevision = 0;

		std::vector<Nyx::Input::InputAssetProblem> Problems;
		std::vector<Nyx::Input::InputCheckFinding> Findings;

		std::optional<Nyx::Input::InputAssetData> KeptA;
		bool bShowingA = false;
	};
}
