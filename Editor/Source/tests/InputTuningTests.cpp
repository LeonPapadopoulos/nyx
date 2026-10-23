// The input asset as the editor edits it, without a window: edits inside its arrays recorded,
// undone and redone through the editor's undo, values being dragged felt at once, elements added
// and removed as one step, saving, and the Tuning and Checks tabs drawn headless.
#include "InputAssetDocument.h"
#include "InputPanel.h"
#include "InputSystem.h"
#include "Log.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectionUtils.h"
#include "SceneEditor.h"
#include "TransactionDiffUtil.h"
#include "VirtualInputBackend.h"

#include <imgui.h>

#include <cmath>
#include <filesystem>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using namespace Nyx::Input;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	bool Near(float a, float b)
	{
		return std::abs(a - b) <= 1e-4f;
	}

	size_t Index(const Nyx::Reflection::TypeMetadata& type, const char* name)
	{
		return *Nyx::Reflection::FindPropertyIndexByName(type, name);
	}

	// The editor's undo with the asset in it, read from a file of its own
	struct Rig
	{
		std::filesystem::path Path = std::filesystem::temp_directory_path() / "NyxInputTuningTests.nyxinput";
		InputSystem Input;
		SceneEditor Editing;
		InputAssetDocument Document;

		Rig()
		{
			std::filesystem::remove(Path);
			Require(Document.Load(Input, Path), "The document makes the file from the defaults and reads it");
			Document.Attach(Editing.Transactions);
		}

		~Rig()
		{
			std::filesystem::remove(Path);
		}

		InputAssetData& Asset()
		{
			return Document.GetAsset();
		}

		// The input system's copy of a context's binding
		const InputBinding& Applied(const char* context, size_t binding)
		{
			return Input.GetContext(Input.FindContext(context)).Bindings[binding];
		}

		// Where a binding's dead zone is, as the details table records it
		SubobjectPath DeadZonePath(size_t context, size_t binding)
		{
			const Nyx::Reflection::TypeMetadata& assetType = Nyx::Reflection::GetTypeMetadata<InputAssetData>();
			return SubobjectPath{ .SubobjectType = &assetType,
				.PropertyIndices = { Index(assetType, "Contexts"), context, Index(Nyx::Reflection::GetTypeMetadata<InputContextData>(), "Bindings"), binding,
					Index(Nyx::Reflection::GetTypeMetadata<InputBindingData>(), "Pipeline"),
					Index(Nyx::Reflection::GetTypeMetadata<ValuePipeline>(), "DeadZone") } };
		}
	};

	void TestEditInsideArraysUndoes()
	{
		Rig rig;
		Require(!rig.Document.IsDirty(), "Not changed after reading");
		Require(rig.Asset().Contexts[0].Name == "EditorFlyCamera" && rig.Asset().Contexts[0].Bindings[1].Control == "Gamepad/LeftStick",
			"Binding 1 of the editor's fly camera is the left stick");

		// The left stick's inner dead zone, dragged: felt before the drag ends
		DeadZoneSettings& deadZone = rig.Asset().Contexts[0].Bindings[1].Pipeline.DeadZone;
		TransactionDiffUtil diff;
		diff.TakeSnapshot(InputAssetDocument::GetRef(), &deadZone, Nyx::Reflection::GetTypeMetadata<DeadZoneSettings>(), rig.DeadZonePath(0, 1));
		deadZone.Inner = 0.3f;
		Require(diff.PreviewChanges("Edit Property", rig.Editing.Transactions), "The drag previews");
		Require(Near(rig.Applied("EditorFlyCamera", 1).Pipeline.DeadZone.Inner, 0.3f), "The input system has the value being dragged");

		deadZone.Inner = 0.25f;
		Require(diff.CommitChanges("Edit Property", rig.Editing.Transactions), "The drag's end records it");
		Require(Near(rig.Applied("EditorFlyCamera", 1).Pipeline.DeadZone.Inner, 0.25f), "And the input system has it");
		Require(rig.Document.IsDirty(), "Changed");

		rig.Editing.Undo();
		Require(Near(rig.Asset().Contexts[0].Bindings[1].Pipeline.DeadZone.Inner, 0.15f), "Undo finds the struct inside the arrays");
		Require(Near(rig.Applied("EditorFlyCamera", 1).Pipeline.DeadZone.Inner, 0.15f), "And the input system has it back");

		rig.Editing.Redo();
		Require(Near(rig.Asset().Contexts[0].Bindings[1].Pipeline.DeadZone.Inner, 0.25f), "Redo");

		// An element that isn't there any more: the change finds nothing and does nothing
		rig.Asset().Contexts[0].Bindings.resize(1);
		rig.Editing.Undo();
		Require(rig.Asset().Contexts[0].Bindings.size() == 1, "A path past the end changes nothing");
	}

	void TestWholeArrayChanges()
	{
		Rig rig;
		InputContextData& fly = rig.Asset().Contexts[0];
		const size_t bindings = fly.Bindings.size();
		const Nyx::Reflection::TypeMetadata& contextType = Nyx::Reflection::GetTypeMetadata<InputContextData>();
		const Nyx::Reflection::TypeMetadata& assetType = Nyx::Reflection::GetTypeMetadata<InputAssetData>();
		const size_t bindingsIndex = Index(contextType, "Bindings");

		ReplaceArrayChange change{ .Target = InputAssetDocument::GetRef(),
			.TypeMetadata = &contextType,
			.PropertyIndex = bindingsIndex,
			.Location = SubobjectPath{ .SubobjectType = &assetType, .PropertyIndices = { Index(assetType, "Contexts"), 0 } },
			.Before = SaveReflectedProperty(&fly, contextType, bindingsIndex) };
		fly.Bindings.erase(fly.Bindings.begin());
		change.After = SaveReflectedProperty(&fly, contextType, bindingsIndex);

		Transaction transaction;
		transaction.Label = "Remove Element";
		transaction.Changes.push_back({ EChangeKind::ReplaceArray, change });
		rig.Editing.Transactions.Push(std::move(transaction));
		Require(rig.Input.GetContext(rig.Input.FindContext("EditorFlyCamera")).Bindings.size() == bindings - 1, "The input system lost the binding too");

		rig.Editing.Undo();
		Require(rig.Asset().Contexts[0].Bindings.size() == bindings && rig.Asset().Contexts[0].Bindings[0].Composite.Forward == "Keyboard/W",
			"Undo brings the binding back, as it was");
		Require(rig.Input.GetContext(rig.Input.FindContext("EditorFlyCamera")).Bindings.size() == bindings, "In the input system too");

		rig.Editing.Redo();
		Require(rig.Asset().Contexts[0].Bindings.size() == bindings - 1, "Redo removes it again");
	}

	void TestSaveAndChecks()
	{
		Rig rig;
		rig.Asset().Contexts[0].Bindings[0].Composite.bNormalize = false;
		rig.Document.Apply();
		rig.Document.RefreshChecks();
		Require(rig.Document.GetFindings().size() == 1 && rig.Document.GetFindings()[0].Check == EInputCheck::CompositeNotNormalized,
			"The document's checks see the change");

		Require(rig.Document.Save(), "Saves");
		Nyx::ReadWarnings warnings;
		const std::optional<InputAssetData> saved = LoadInputAsset(rig.Path, warnings);
		Require(saved && !saved->Contexts[0].Bindings[0].Composite.bNormalize, "What was saved is what was edited");

		// What the running editor knows: the code took LookButton once
		rig.Input.GetUser().ConsumeTriggered(rig.Input.FindAction("LookButton"));
		rig.Document.RefreshChecks();
		bool bFound = false;
		for (const InputCheckFinding& finding : rig.Document.GetFindings())
		{
			bFound |= finding.Check == EInputCheck::DownOnOneShot && finding.Action == "LookButton";
		}
		Require(bFound, "The checks know which actions the code takes once");
	}

	// The Tuning and Checks tabs, headless, on each kind of selection
	void TestTabsDraw()
	{
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
		io.DisplaySize = ImVec2(1280, 800);
		io.DeltaTime = 1.0f / 60.0f;
		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
		RegisterDefaultPropertyWidgets();

		{
			Rig rig;
			VirtualInputBackend backend;
			rig.Input.AddBackend(backend);
			const InputDeviceId pad = backend.AddDevice(EDeviceKind::Gamepad, "Pad", 1000);
			AddRecipe(rig.Asset(), 0, EInputRecipe::TapAndHold, "Use");
			rig.Document.Apply();

			InputPanel panel;
			panel.bOpen = true;
			panel.Attach(rig.Document, rig.Editing.Transactions);

			uint64_t time = 1000;
			const auto frame = [&]()
			{
				time += 16000;
				backend.SetValue(pad, Gamepad(EGamepadControl::RightStick), glm::vec3(0.5f, 0.0f, 0.0f), time - 1);
				rig.Input.BeginFrame(time);
				panel.Record(rig.Input);
				ImGui::NewFrame();
				panel.Draw(rig.Input);
				ImGui::Render();
				rig.Input.EndFrame();
			};

			const size_t tapBinding = rig.Asset().Contexts[0].Bindings.size() - 4;
			for (const auto& [tab, select] : std::vector<std::pair<std::string, std::function<void()>>>{
					 { "Tuning", [&]()
						 {
							 panel.GetTuning().SelectBinding(0, 4);
						 } },
					 { "Tuning", [&]()
						 {
							 panel.GetTuning().SelectBinding(0, tapBinding);
						 } },
					 { "Checks", [&]()
						 {
						 } },
				 })
			{
				panel.SelectExtraTab(tab);
				select();
				for (int i = 0; i < 3; ++i)
				{
					frame();
				}
			}
			Require(rig.Asset().Contexts[0].Bindings[4].Control == "Gamepad/RightStick", "Binding 4 is the right stick, with its curve");
			Require(!rig.Document.IsDirty(), "Drawing changes nothing");
		}

		ImGui::DestroyContext();
	}
}

int main()
{
	try
	{
		Nyx::Logger::Get().Init();

		TestEditInsideArraysUndoes();
		TestWholeArrayChanges();
		TestSaveAndChecks();
		TestTabsDraw();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Input tuning test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All input tuning tests passed.\n";
	return 0;
}
