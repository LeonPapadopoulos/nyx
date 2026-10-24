// A physics material as the editor edits it, without a window: its values read from the file, edits
// written into the file's lines (its comments and the lines Nyx doesn't know kept), undone and redone
// through the editor's undo, values being dragged counted at once for the games, saving, another
// material opened untouched by the last one's undo, and the Physics Material window drawn headless,
// saving with Ctrl+S.
#include "HeadlessUI.h"
#include "Log.h"
#include "PhysicsMaterialDocument.h"
#include "PhysicsMaterialPanel.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
#include "SceneEditor.h"
#include "TestSupport.h"
#include "TransactionDiffUtil.h"

#include <imgui.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using Nyx::Tests::Require;

	const char* WoodText = "# Planks and crates\n"
						   "Friction = 0.6\n"
						   "Grain = along\n"
						   "Extra.Flammability = 0.8\n";

	void WriteFile(const std::filesystem::path& file, const std::string& text)
	{
		std::filesystem::create_directories(file.parent_path());
		std::ofstream stream(file, std::ios::binary | std::ios::trunc);
		stream << text;
	}

	bool Contains(const std::string& text, const char* part)
	{
		return text.find(part) != std::string::npos;
	}

	std::string ReadFile(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
	}

	// The editor's undo with a physics material open in it, from files of its own
	struct Rig
	{
		Nyx::Tests::TestDirectory Assets{ "NyxPhysicsMaterialEditorTests" };
		SceneEditor Editing;
		PhysicsMaterialDocument Document;

		Rig()
		{
			WriteFile(Assets.Path / "PhysicsMaterials" / "Wood.nyxphysmat", WoodText);
			WriteFile(Assets.Path / "PhysicsMaterials" / "Ice.nyxphysmat", "Friction = 0.05\n");
			Document.Attach(Editing.Transactions);
			std::string error;
			Require(Document.Open({ Assets.Path }, "PhysicsMaterials/Wood.nyxphysmat", error), "The wood should open: " + error);
		}

		// Changes the values as the details table does: a snapshot, the change, then one undo step
		template <typename TChange>
		void Edit(const TChange& change, bool bPreviewOnly = false)
		{
			TransactionDiffUtil diff;
			diff.TakeSnapshot(Document.GetRef(), &Document.GetData(), Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>(), SubobjectPath{});
			change(Document.GetData());
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

		// Adds an extra as the details table's array row does: the whole array before and after
		void AddExtra(const Nyx::PhysicsMaterialExtra& extra)
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::PhysicsMaterialData>();
			const size_t extras = *Nyx::Reflection::FindPropertyIndexByName(type, "Extras");
			std::vector<std::byte> before = SaveReflectedProperty(&Document.GetData(), type, extras);
			Document.GetData().Extras.push_back(extra);
			Transaction transaction;
			transaction.Label = "Add Element";
			transaction.Changes.push_back({ EChangeKind::ReplaceArray,
				ReplaceArrayChange{ .Target = Document.GetRef(),
					.TypeMetadata = &type,
					.PropertyIndex = extras,
					.Before = std::move(before),
					.After = SaveReflectedProperty(&Document.GetData(), type, extras) } });
			Editing.Transactions.Push(std::move(transaction));
		}
	};

	void TestEditsWriteTheLinesAndUndo()
	{
		Rig rig;
		Require(rig.Document.IsOpen() && !rig.Document.IsDirty(), "Open, not changed");
		Require(rig.Document.GetData().Friction == 0.6f && rig.Document.GetData().Extras.size() == 1, "The wood's values should be read");
		Require(rig.Document.GetText() == WoodText, "Its text should be the file's");

		const uint64_t opened = rig.Document.GetRevision();
		rig.Edit([](Nyx::PhysicsMaterialData& data)
			{
				data.Friction = 0.2f;
			});
		Require(rig.Document.IsDirty() && rig.Document.GetRevision() > opened, "An edit should count");
		Require(rig.Document.GetText() == "# Planks and crates\nFriction = 0.2\nGrain = along\nExtra.Flammability = 0.8\n",
			"The edit should change its own line alone:\n" + rig.Document.GetText());

		rig.AddExtra(Nyx::PhysicsMaterialExtra{ .Name = "Smoke", .Value = 0.5f });
		Require(rig.Document.GetText().ends_with("Extra.Smoke = 0.5\n"), "A new extra should get a line:\n" + rig.Document.GetText());

		rig.Editing.Undo();
		Require(rig.Document.GetData().Extras.size() == 1 && !Contains(rig.Document.GetText(), "Smoke"), "Undo should take the extra back");
		rig.Editing.Undo();
		Require(rig.Document.GetData().Friction == 0.6f && rig.Document.GetText() == WoodText, "Undone, the text should be the file's again");
		rig.Editing.Redo();
		Require(rig.Document.GetData().Friction == 0.2f && Contains(rig.Document.GetText(), "Friction = 0.2"), "Redo");

		// A value being dragged: counted, and in the text, before the drag ends
		const uint64_t before = rig.Document.GetRevision();
		rig.Edit(
			[](Nyx::PhysicsMaterialData& data)
			{
				data.Restitution = 0.4f;
			},
			true);
		Require(rig.Document.GetRevision() > before && Contains(rig.Document.GetText(), "Restitution = 0.4"), "A drag should reach the text at once");
	}

	void TestSavingAndAnotherMaterial()
	{
		Rig rig;
		rig.Edit([](Nyx::PhysicsMaterialData& data)
			{
				data.Density = 600.0f;
			});
		std::string error;
		Require(rig.Document.Save(error) && !rig.Document.IsDirty(), "Saved, not changed: " + error);
		Require(ReadFile(rig.Document.GetFile()) == rig.Document.GetText() && Contains(rig.Document.GetText(), "Density = 600"), "The file should have the values");

		// Another material: the wood's undo steps find nothing to change in it
		Require(rig.Document.Open({ rig.Assets.Path }, "PhysicsMaterials/Ice.nyxphysmat", error), "The ice should open: " + error);
		rig.Editing.Undo();
		Require(rig.Document.GetData().Friction == 0.05f && rig.Document.GetData().Density == 1000.0f && !rig.Document.IsDirty(),
			"The wood's undo shouldn't change the ice");
		Require(!rig.Document.Open({ rig.Assets.Path }, "PhysicsMaterials/Gone.nyxphysmat", error) && rig.Document.GetPath() == "PhysicsMaterials/Ice.nyxphysmat",
			"A material that isn't there shouldn't open, leaving the ice open");

		rig.Document.Close();
		Require(!rig.Document.IsOpen(), "Closed");
	}

	void TestTheWindowDrawsAndSaves()
	{
		Rig rig;
		Nyx::Tests::HeadlessUI ui;
		RegisterDefaultPropertyWidgets();
		PhysicsMaterialPanel panel;
		const auto draw = [&]()
		{
			ImGui::SetNextWindowFocus();
			panel.Draw(rig.Document, rig.Editing.Transactions);
			panel.HandleHotkeys(rig.Document);
		};
		for (int frame = 0; frame < 3; ++frame)
		{
			ui.Frame(draw);
		}
		Require(!rig.Document.IsDirty(), "Drawing should change nothing");

		rig.Edit([](Nyx::PhysicsMaterialData& data)
			{
				data.Friction = 0.9f;
			});
		ui.Press(ImGuiKey_S, true, false, draw);
		Require(!rig.Document.IsDirty() && Contains(ReadFile(rig.Document.GetFile()), "Friction = 0.9"), "Ctrl+S in the window should save the material");
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();
		TestEditsWriteTheLinesAndUndo();
		TestSavingAndAnotherMaterial();
		TestTheWindowDrawsAndSaves();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Physics material editor test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All physics material editor tests passed.\n";
	return 0;
}
