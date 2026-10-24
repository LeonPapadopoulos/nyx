// Assets edited by reflection, without a window: a physics material and the test module's creature, a
// game's own type. Their values read from their files, edits written into the files' lines (comments
// and unknown lines kept), undone and redone through the editor's undo, named entries added with a
// name of their own, renamed and taken away, values being dragged counted at once, problems at their
// values, saving, files changed on disk read again into an asset without changes, what reaches the
// running games, New, an asset opened again untouched by the last one's undo; then the windows drawn
// headless, the problems under their values, and Ctrl+S.
#include "FileWatcher.h"
#include "GameModule.h"
#include "HeadlessUI.h"
#include "Log.h"
#include "PhysicsMaterialAsset.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedAssetEditors.h"
#include "ReflectedAssetWindows.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectedPropertyDrawer.h"
#include "ReflectionUtils.h"
#include "SceneEditor.h"
#include "TestCreatureAsset.h"
#include "TestSupport.h"
#include "TransactionDiffUtil.h"

#include <imgui.h>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using namespace NyxTestModule;
	using Nyx::Tests::Require;

	const char* WoodText = "# Planks and crates\n"
						   "Friction = 0.6\n"
						   "Grain = along\n"
						   "Extra.Flammability = 0.8\n";

	const char* HoundText = "# A marsh hound\n"
							"DisplayName = Marsh Hound\n"
							"Speed = 4.5\n"
							"Move.Bite.Damage = 12\n"
							"Move.Bite.Reach = 1.5\n"
							"Range.Hearing = 20 5 40\n";

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	}

	std::string ReadFile(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	bool Contains(const std::string& text, const char* part)
	{
		return text.find(part) != std::string::npos;
	}

	size_t PropertyIndex(const Nyx::Reflection::TypeMetadata& type, const char* name)
	{
		return *Nyx::Reflection::FindPropertyIndexByName(type, name);
	}

	// The editor's undo with assets open in it, from files of its own
	struct Rig
	{
		Nyx::Tests::TestDirectory Assets{ "NyxReflectedAssetEditorTests" };
		SceneEditor Editing;
		ReflectedAssetEditors Editors;

		Rig()
		{
			WriteFile(Assets.Path / "PhysicsMaterials" / "Wood.nyxphysmat", WoodText);
			WriteFile(Assets.Path / "PhysicsMaterials" / "Ice.nyxphysmat", "Friction = 0.05\n");
			WriteFile(Assets.Path / "Creatures" / "Hound.nyxcreature", HoundText);
			Editors.SetAssetFolders({ Assets.Path });
			Editors.Attach(Editing.Transactions);
		}

		ReflectedAssetDocument& Open(const std::string& path)
		{
			std::string error;
			ReflectedAssetDocument* document = Editors.Open(path, error);
			Require(document != nullptr, path + " should open: " + error);
			return *document;
		}

		// Changes the values as the details table does: a snapshot, the change, then one undo step
		template <typename T, typename TChange>
		void Edit(ReflectedAssetDocument& document, const TChange& change, bool bPreviewOnly = false)
		{
			TransactionDiffUtil diff;
			diff.TakeSnapshot(document.GetRef(), document.GetData(), Nyx::Reflection::GetTypeMetadata<T>(), SubobjectPath{});
			change(*static_cast<T*>(document.GetData()));
			if (bPreviewOnly)
			{
				Require(diff.PreviewChanges("Edit Property", Editing.Transactions), "The drag should preview");
				diff.Cancel();
			}
			else
			{
				Require(diff.CommitChanges("Edit Property", Editing.Transactions), "The edit should be recorded");
			}
		}

		// Changes an array as the details table's rows do: the whole array before and after
		template <typename T, typename TChange>
		void EditArray(ReflectedAssetDocument& document, const char* property, const TChange& change)
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<T>();
			const size_t index = PropertyIndex(type, property);
			std::vector<std::byte> before = SaveReflectedProperty(document.GetData(), type, index);
			change(*static_cast<T*>(document.GetData()));
			Transaction transaction;
			transaction.Label = "Add Element";
			transaction.Changes.push_back({ EChangeKind::ReplaceArray,
				ReplaceArrayChange{ .Target = document.GetRef(),
					.TypeMetadata = &type,
					.PropertyIndex = index,
					.Before = std::move(before),
					.After = SaveReflectedProperty(document.GetData(), type, index) } });
			Editing.Transactions.Push(std::move(transaction));
		}
	};

	Nyx::PhysicsMaterialData& Wood(ReflectedAssetDocument& document)
	{
		return *static_cast<Nyx::PhysicsMaterialData*>(document.GetData());
	}

	TestCreatureData& Creature(ReflectedAssetDocument& document)
	{
		return *static_cast<TestCreatureData*>(document.GetData());
	}

	void TestEditsWriteTheLinesAndUndo()
	{
		Rig rig;
		ReflectedAssetDocument& wood = rig.Open("PhysicsMaterials/Wood.nyxphysmat");
		Require(!wood.IsDirty() && wood.GetType().TypeId == "PhysicsMaterial", "Open as a physics material, not changed");
		Require(Wood(wood).Friction == 0.6f && Wood(wood).Extras.size() == 1, "The wood's values should be read");
		Require(wood.GetText() == WoodText && wood.GetProblems().empty(), "Its text should be the file's, without problems");
		Require(&wood == &rig.Open("PhysicsMaterials/Wood.nyxphysmat"), "Opened again, it should be the one open");

		const uint64_t opened = wood.GetRevision();
		rig.Edit<Nyx::PhysicsMaterialData>(wood, [](Nyx::PhysicsMaterialData& data)
			{
				data.Friction = 0.2f;
			});
		Require(wood.IsDirty() && wood.GetRevision() > opened, "An edit should count");
		Require(wood.GetText() == "# Planks and crates\nFriction = 0.2\nGrain = along\nExtra.Flammability = 0.8\n",
			"The edit should change its own line alone:\n" + wood.GetText());

		rig.EditArray<Nyx::PhysicsMaterialData>(wood, "Extras", [](Nyx::PhysicsMaterialData& data)
			{
				data.Extras.push_back(Nyx::PhysicsMaterialExtra{ .Name = "Smoke", .Value = 0.5f });
			});
		Require(wood.GetText().ends_with("Extra.Smoke = 0.5\n"), "A new extra should get a line:\n" + wood.GetText());

		rig.Editing.Undo();
		Require(Wood(wood).Extras.size() == 1 && !Contains(wood.GetText(), "Smoke"), "Undo should take the extra back");
		rig.Editing.Undo();
		Require(Wood(wood).Friction == 0.6f && wood.GetText() == WoodText, "Undone, the text should be the file's again");
		rig.Editing.Redo();
		Require(Wood(wood).Friction == 0.2f && Contains(wood.GetText(), "Friction = 0.2"), "Redo");

		// A value being dragged: counted, and in the text, before the drag ends
		const uint64_t before = wood.GetRevision();
		rig.Edit<Nyx::PhysicsMaterialData>(
			wood,
			[](Nyx::PhysicsMaterialData& data)
			{
				data.Restitution = 0.4f;
			},
			true);
		Require(wood.GetRevision() > before && Contains(wood.GetText(), "Restitution = 0.4"), "A drag should reach the text at once");

		// A value out of its range is a problem at its property, until it isn't
		rig.Edit<Nyx::PhysicsMaterialData>(wood, [](Nyx::PhysicsMaterialData& data)
			{
				data.Density = -5.0f;
			});
		Require(wood.GetProblems().size() == 1 && wood.GetProblems()[0].Path == "Density", "A density below 0 should be a problem");
		rig.Editing.Undo();
		Require(wood.GetProblems().empty(), "Undone, it shouldn't be");
	}

	void TestNamedEntries()
	{
		Rig rig;
		ReflectedAssetDocument& hound = rig.Open("Creatures/Hound.nyxcreature");
		const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<TestCreatureData>();
		Require(hound.GetType().TypeId == "TestCreature" && Creature(hound).Moves.size() == 1 && Creature(hound).Ranges[0].Max == 40.0f,
			"The game's own type should open with its values");

		// Added as the details table adds one: a name of its own
		rig.EditArray<TestCreatureData>(hound, "Moves", [&](TestCreatureData& creature)
			{
				const Nyx::Reflection::ArrayMetadata& array = *Nyx::Reflection::FindPropertyByName(type, "Moves")->Array;
				creature.Moves.push_back(TestCreatureMove{ .Name = Nyx::MakeEntryName(&creature.Moves, array, "Entry") });
			});
		Require(Creature(hound).Moves[1].Name == "Entry" && Contains(hound.GetText(), "Move.Bite.Reach = 1.5\nMove.Entry.Damage = 1\nRange."),
			"An entry added should get its lines after the others':\n" + hound.GetText());

		// Renamed through its Name, as its text field does
		TransactionDiffUtil rename;
		rename.TakeSnapshot(hound.GetRef(), &Creature(hound).Moves[1], Nyx::Reflection::GetTypeMetadata<TestCreatureMove>(),
			SubobjectPath{ .SubobjectType = &type, .PropertyIndices = { PropertyIndex(type, "Moves"), 1 } });
		Creature(hound).Moves[1].Name = "Howl";
		Require(rename.CommitChanges("Edit Property", rig.Editing.Transactions), "The rename should be recorded");
		Require(Contains(hound.GetText(), "Move.Howl.Damage = 1\n") && !Contains(hound.GetText(), "Entry"), "Renamed, its lines should have its name:\n" + hound.GetText());

		// Taken away
		rig.EditArray<TestCreatureData>(hound, "Moves", [](TestCreatureData& creature)
			{
				creature.Moves.erase(creature.Moves.begin());
			});
		Require(!Contains(hound.GetText(), "Bite") && Contains(hound.GetText(), "Move.Howl.Damage"), "Taken away, the bite's lines should go:\n" + hound.GetText());

		// Undone step by step, and done again
		rig.Editing.Undo();
		Require(Creature(hound).Moves.size() == 2 && Contains(hound.GetText(), "Move.Bite.Damage = 12"), "Undo should bring the bite back");
		rig.Editing.Undo();
		Require(Creature(hound).Moves[1].Name == "Entry" && Contains(hound.GetText(), "Move.Entry.Damage"), "Undo should name it Entry again");
		rig.Editing.Undo();
		Require(Creature(hound).Moves.size() == 1 && hound.GetText() == HoundText, "Undone, the text should be the file's:\n" + hound.GetText());
		rig.Editing.Redo();
		rig.Editing.Redo();
		rig.Editing.Redo();
		Require(Creature(hound).Moves.size() == 1 && Creature(hound).Moves[0].Name == "Howl" && !Contains(hound.GetText(), "Bite"), "Redone, only the howl is left");

		// Two entries of a name: a problem at the second
		rig.EditArray<TestCreatureData>(hound, "Moves", [](TestCreatureData& creature)
			{
				creature.Moves.push_back(TestCreatureMove{ .Name = "Howl" });
				creature.Moves.push_back(TestCreatureMove{});
			});
		const std::vector<Nyx::PropertyProblem>& problems = hound.GetProblems();
		Require(problems.size() == 2 && problems[0].Path == "Moves.Howl.Name" && problems[1].Path == "Moves[2].Name", "A name twice and none should be problems");
	}

	void TestSavingFilesAndGames()
	{
		Rig rig;
		ReflectedAssetDocument& wood = rig.Open("PhysicsMaterials/Wood.nyxphysmat");
		ReflectedAssetDocument& hound = rig.Open("Creatures/Hound.nyxcreature");
		Require(rig.Editors.TakeValuesToSend().empty() && rig.Editors.GetUnsavedValues().empty(), "Opened, the games have what the files have");

		rig.Edit<TestCreatureData>(hound, [](TestCreatureData& creature)
			{
				creature.Speed = 6.0f;
			});
		std::vector<Nyx::AssetValuesMessage> sent = rig.Editors.TakeValuesToSend();
		Require(sent.size() == 1 && sent[0].Path == "Creatures/Hound.nyxcreature" && sent[0].Text == hound.GetText() && Contains(sent[0].Text, "Speed = 6"),
			"The edit should go to the games");
		Require(rig.Editors.TakeValuesToSend().empty(), "Once");
		Require(rig.Editors.GetUnsavedValues().size() == 1, "A game starting gets the creature's unsaved values");

		std::string error;
		Require(hound.Save(error) && !hound.IsDirty() && ReadFile(hound.GetFile()) == hound.GetText(), "Saved, the file should have the values: " + error);
		Require(rig.Editors.GetUnsavedValues().empty(), "Saved, nothing is unsaved");

		// Its own save seen on disk: nothing changes, undo goes on
		const ObjectRef ref = hound.GetRef();
		rig.Editors.ApplyFileChanges({ hound.GetFile() });
		Require(rig.Editors.Find("Creatures/Hound.nyxcreature")->GetRef() == ref, "Its own save shouldn't read it again");

		// Changed on disk while it has no changes: read again, its old undo steps do nothing
		WriteFile(rig.Assets.Path / "Creatures" / "Hound.nyxcreature", "# By hand\nSpeed = 9\n");
		rig.Editors.ApplyFileChanges({ rig.Assets.Path / "Creatures" });
		ReflectedAssetDocument& again = *rig.Editors.Find("Creatures/Hound.nyxcreature");
		Require(again.GetRef() != ref && Creature(again).Speed == 9.0f && again.GetText() == "# By hand\nSpeed = 9\n" && !again.IsDirty(),
			"Changed on disk, it should be read again");
		sent = rig.Editors.TakeValuesToSend();
		Require(sent.size() == 1 && sent[0].Text == again.GetText(), "Read again, the games get the file's values");
		rig.Editing.Undo();
		Require(Creature(again).Speed == 9.0f && !again.IsDirty(), "The undo of the old one shouldn't change the new one");

		// Changed on disk while it has changes: they stay, and it says so; Reload takes the file's
		rig.Edit<Nyx::PhysicsMaterialData>(wood, [](Nyx::PhysicsMaterialData& data)
			{
				data.Friction = 0.3f;
			});
		WriteFile(wood.GetFile(), "Friction = 0.9\n");
		rig.Editors.ApplyFileChanges({ wood.GetFile() });
		Require(rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat") == &wood && Wood(wood).Friction == 0.3f && wood.IsChangedOnDisk(),
			"With changes, it should keep them and say the file changed");
		Require(rig.Editors.Reload("PhysicsMaterials/Wood.nyxphysmat", error), "It should read the file again: " + error);
		ReflectedAssetDocument& reloaded = *rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat");
		Require(Wood(reloaded).Friction == 0.9f && !reloaded.IsDirty() && !reloaded.IsChangedOnDisk(), "Reloaded, the file's values");

		// The watcher finds a change by itself
		rig.Editors.Watch();
		WriteFile(rig.Assets.Path / "PhysicsMaterials" / "Wood.nyxphysmat", "Friction = 0.7\n");
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (Wood(*rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat")).Friction != 0.7f && std::chrono::steady_clock::now() < deadline)
		{
			rig.Editors.Update();
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		Require(Wood(*rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat")).Friction == 0.7f, "A file written on disk should be read again by itself");

		// Closed and opened again: another object, its old undo does nothing
		rig.Edit<Nyx::PhysicsMaterialData>(*rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat"), [](Nyx::PhysicsMaterialData& data)
			{
				data.Density = 600.0f;
			});
		rig.Editors.Close("PhysicsMaterials/Wood.nyxphysmat");
		Require(!rig.Editors.Find("PhysicsMaterials/Wood.nyxphysmat"), "Closed");
		ReflectedAssetDocument& woodAgain = rig.Open("PhysicsMaterials/Wood.nyxphysmat");
		rig.Editing.Undo();
		Require(Wood(woodAgain).Density == 1000.0f && !woodAgain.IsDirty(), "The undo of the one closed shouldn't change the one opened again");

		Require(!rig.Editors.Open("PhysicsMaterials/Gone.nyxphysmat", error), "A file that isn't there shouldn't open");
		Require(!rig.Editors.Open("Scenes/Main.nyxscene", error) && Contains(error, "no reflected asset type"), "Nor a file of another type: " + error);
	}

	void TestNewAssets()
	{
		Rig rig;
		std::string error;
		const Nyx::ReflectedAssetType& creature = *Nyx::ReflectedAssetTypes::Get().FindByTypeId("TestCreature");
		const std::string path = rig.Editors.Create(creature, "Creatures", error);
		const std::string second = rig.Editors.Create(creature, "Creatures", error);
		Require(path == "Creatures/NewTestCreature.nyxcreature" && second == "Creatures/NewTestCreature2.nyxcreature", "New creatures, named apart: " + path + ", " + second);
		const std::string text = ReadFile(rig.Assets.Path / path);
		Require(text.starts_with("# A creature's stats, as a game's own asset type\nDisplayName = Creature\nLegs = 4\n") && Contains(text, "Senses.Smell = 0.5\n"),
			"A new creature should have every value written:\n" + text);
		ReflectedAssetDocument& opened = rig.Open(path);
		Require(opened.GetProblems().empty() && Creature(opened).Legs == 4, "It should open without problems");

		const std::string material = rig.Editors.Create(*Nyx::ReflectedAssetTypes::Get().FindByTypeId("PhysicsMaterial"), "", error);
		Require(material == "NewPhysicsMaterial.nyxphysmat" && ReadFile(rig.Assets.Path / material).ends_with("Friction = 0.5\nRestitution = 0\nDensity = 1000\n"),
			"A new physics material should have the engine's values:\n" + ReadFile(rig.Assets.Path / material));
	}

	void TestProblemsAreShownAtTheirValues()
	{
		Rig rig;
		Nyx::Tests::HeadlessUI ui;
		RegisterDefaultPropertyWidgets();
		TestCreatureData creature;
		creature.Moves = { TestCreatureMove{ .Name = "Bite" }, TestCreatureMove{} };
		creature.Ranges = { TestCreatureRange{ .Name = "Sight", .Min = 5.0f, .Max = 1.0f } };
		creature.Habitats = { "Swamp", "Reeds" };
		const std::vector<Nyx::PropertyProblem> problems = {
			{ "Speed", "speed" },
			{ "Senses.Smell", "smell" },
			{ "Moves.Bite.Reach", "reach" },
			{ "Moves[1].Name", "name" },
			{ "Ranges.Sight.Min", "min" },
			{ "Habitats[1]", "habitat" },
			{ "Moves.Gone.Damage", "about no value" },
		};
		InspectorDrawContext context;
		context.Problems = &problems;
		ui.Frame([&]()
			{
				ImGui::Begin("Values");
				DrawReflectedTypeTable(&creature, Nyx::Reflection::GetTypeMetadata<TestCreatureData>(), context);
				ImGui::End();
			});
		std::sort(context.ShownProblems.begin(), context.ShownProblems.end());
		Require(context.ShownProblems == std::vector<size_t>({ 0, 1, 2, 3, 4, 5 }) && context.CurrentPath.empty(),
			"Each problem should be shown at its value, the one about no value not at all (" + std::to_string(context.ShownProblems.size()) + " shown)");
	}

	void TestTheWindowsDrawAndSave()
	{
		Rig rig;
		Nyx::Tests::HeadlessUI ui;
		RegisterDefaultPropertyWidgets();
		ReflectedAssetDocument& hound = rig.Open("Creatures/Hound.nyxcreature");
		ReflectedAssetWindows windows;
		const auto draw = [&]()
		{
			ImGui::SetNextWindowFocus();
			windows.Draw(rig.Editors, rig.Editing.Transactions, nullptr);
			windows.HandleHotkeys(rig.Editors);
		};
		for (int frame = 0; frame < 3; ++frame)
		{
			ui.Frame(draw);
		}
		Require(!hound.IsDirty(), "Drawing should change nothing");

		rig.Edit<TestCreatureData>(hound, [](TestCreatureData& creature)
			{
				creature.Speed = -1.0f;
				creature.Ranges[0].Min = 50.0f;
			});
		Require(hound.GetProblems().size() == 2, "The edit should make two problems");
		ui.Frame(draw);
		ui.Press(ImGuiKey_S, true, false, draw);
		Require(!hound.IsDirty() && Contains(ReadFile(hound.GetFile()), "Speed = -1"), "Ctrl+S in the window should save the creature");

		// A second asset's window beside it
		rig.Open("PhysicsMaterials/Wood.nyxphysmat");
		ui.Frame(draw);
		Require(rig.Editors.GetDocuments().size() == 2, "Both should stay open");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		Nyx::RegisterEngineAssetTypes();
		// The test module registers its creature, as a game's module registers its types
		Nyx::GetLinkedGameModule().RegisterTypes();

		TestEditsWriteTheLinesAndUndo();
		TestNamedEntries();
		TestSavingFilesAndGames();
		TestNewAssets();
		TestProblemsAreShownAtTheirValues();
		TestTheWindowsDrawAndSave();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Reflected asset editor test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All reflected asset editor tests passed.\n";
	return 0;
}
