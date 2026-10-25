// The editor with a game module (NyxTestModule, linked as the editor links a game's module): the
// module gives the editor where its types are declared, so the source inspector can open them, and
// the Details panel reads a game's run-time components of the module's types, and shows the values a
// module component inherits next to those it overrides.
#include "BinaryArchive.h"
#include "ComponentEdits.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "DetailsPanel.h"
#include "GameModule.h"
#include "HeadlessUI.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "ValueOverride.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionSourceRegistry.h"
#include "ReflectionUtils.h"
#include "SceneEditor.h"
#include "TestLanternComponent.h"
#include "TestLanternGlowComponent.h"
#include "TestMarkerComponent.h"
#include "TestSupport.h"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	using Nyx::Tests::Require;

	// The line `line` (from 1) of the file
	std::string ReadLine(const char* file, uint32_t line)
	{
		std::ifstream stream(file);
		std::string text;
		for (uint32_t row = 0; row < line; ++row)
		{
			std::getline(stream, text);
		}
		return text;
	}

	void TestModuleRegistersItsSources()
	{
		const GameModule& module = GetLinkedGameModule();
		Require(std::string_view(module.Name) == "NyxTestModule", "The linked module should be the test module");
		Require(module.RegisterSources != nullptr, "The editor's module should register source locations");
		Require(module.CreateSystems == nullptr, "The editor doesn't simulate, so its module has no systems");

		module.RegisterSources();

		const ReflectionSourceRegistry& sources = ReflectionSourceRegistry::Get();
		const Reflection::TypeMetadata& type = Reflection::GetTypeMetadata<NyxTestModule::TestLanternComponent>();
		const SourceLocation typeSource = sources.Find(type);
		Require(typeSource.IsValid(), "The module's type has no source location");
		Require(std::string_view(typeSource.File).ends_with("TestLanternComponent.h"),
			"The module's type should be declared in its own header");

		bool foundBrightness = false;
		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const Reflection::PropertyMetadata& property = type.Properties[i];
			if (std::string_view(property.Name) != "Brightness")
			{
				continue;
			}
			foundBrightness = true;
			const SourceLocation source = sources.Find(property);
			Require(source.IsValid(), "The module's property has no source location");
			Require(ReadLine(source.File, source.Line).find("float Brightness") != std::string::npos,
				"The module property's source line should be its declaration");
		}
		Require(foundBrightness, "The module's type should have its Brightness property");
	}
}

namespace
{
	// A game's run-time component of the module's type: the editor knows the type from the module,
	// doesn't offer it to add, and shows a game's answer with it in the Runtime section
	void TestModuleRuntimeComponentsInTheDetails()
	{
		RegisterComponentTypes();
		GetLinkedGameModule().RegisterTypes();
		const ComponentTypeOps* glowType = ComponentTypeRegistry::Get().FindByName("NyxTestModule::TestLanternGlowComponent");
		Require(glowType && glowType->IsRuntime(), "The module's glow should be a run-time type in the editor too");
		Require(!CanAddOrRemoveComponent(*glowType), "A run-time type should not be offered to add");

		const NyxTestModule::TestLanternGlowComponent glow{ .Glow = 2.5f, .Steps = 9 };
		BinaryWriter block;
		Require(ReflectedArchiveSerializer::SerializeObject(block, &glow, *glowType->TypeMetadata), "The glow should be written");
		InspectedEntityMessage answer;
		answer.bFound = true;
		answer.Step = 9;
		answer.Components.push_back(InspectedComponent{ .Type = glowType->TypeMetadata->Name, .DisplayName = "Test Lantern Glow", .Properties = block.GetBytes() });

		Tests::HeadlessUI ui;
		RegisterDefaultPropertyWidgets();
		SceneEditor editing;
		const Entity lantern = editing.Scene.CreateEntity("Lantern");
		editing.Scene.GetSelection() = lantern;
		answer.Entity = editing.Scene.GetGuid(lantern);

		const std::optional<RuntimeSection> section = GetRuntimeSection(editing.Scene.GetRegistry(), lantern, GameInspection{ "Game 1", &answer });
		Require(section && section->Source == "Game 1, step 9" && section->Components.size() == 1, "The game's answer should be the Runtime section");

		DetailsPanel details;
		details.SetGameInspection(GameInspection{ "Game 1", &answer });
		const auto draw = [&]()
		{
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(400.0f, 780.0f));
			details.Draw(editing);
		};
		ui.Frame(draw);
		ui.Frame(draw);
		Require(details.GetInspectedEntity() == answer.Entity, "The details should show the lantern's Runtime section");
	}
}

namespace
{
	using NyxTestModule::TestLanternComponent;
	using NyxTestModule::TestMarkerComponent;

	// A lantern and a marker, the lantern selected in the details. Their items are found through the
	// UI source inspector, which reports each item with the property it belongs to: the declarations
	// of both components' overrides, and of an override's Name, are marked for that.
	class OverridesFixture
	{
	public:
		OverridesFixture()
		{
			RegisterComponentTypes();
			GetLinkedGameModule().RegisterTypes();
			RegisterDefaultPropertyWidgets();
			Lantern = Editing.Scene.CreateEntity("Lantern");
			Editing.Scene.GetRegistry().Add<TestLanternComponent>(Lantern, TestLanternComponent{});
			Marker = Editing.Scene.CreateEntity("Marker");
			Editing.Scene.GetRegistry().Add<TestMarkerComponent>(Marker, TestMarkerComponent{ .Overrides = { { "Reach", 3.0f } } });
			Editing.Scene.GetSelection() = Lantern;

			Mark(SourceLocation(OverridesMarker, 1), SourceLocation(NameMarker, 1));
			Nyx::UI::SetSourceItemCallback(&Collect, this);
			Nyx::UI::SetInspectingSources(true);
		}

		~OverridesFixture()
		{
			Nyx::UI::SetInspectingSources(false);
			Nyx::UI::SetSourceItemCallback(nullptr, nullptr);
			Mark(SourceLocation{}, SourceLocation{});
		}

		OverridesFixture(const OverridesFixture&) = delete;
		OverridesFixture& operator=(const OverridesFixture&) = delete;

		void Draw()
		{
			Items.clear();
			ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
			ImGui::SetNextWindowSize(ImVec2(500.0f, 780.0f));
			Details.Draw(Editing);
			Editing.DetailsContext.PreviewPendingEdits();
		}

		void Frame()
		{
			Headless.Frame([&]()
				{
					Draw();
				});
		}

		void Press(ImVec2 at)
		{
			ImGui::GetIO().AddMousePosEvent(at.x, at.y);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, true);
			Frame();
			ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
			Frame();
		}

		// Ctrl+click on a number field to type into it, each part typed in a frame of its own, then Enter
		void Type(const ImRect& field, const std::vector<const char*>& parts)
		{
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, true);
			Press(field.GetCenter());
			ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame();
			for (const char* part : parts)
			{
				ImGui::GetIO().AddInputCharactersUTF8(part);
				Frame();
			}
			Headless.Press(ImGuiKey_Enter, false, false, [&]()
				{
					Draw();
				});
			Frame();
		}

		// The rows under the overrides' tree row, each its items from left to right: the value, then its
		// reset arrow or Remove button, if it has one
		std::vector<std::vector<ImRect>> GetRows()
		{
			Frame();
			// Top to bottom; an item within a few pixels of the row's middle is on it (text sits a
			// little lower than a button beside it)
			std::vector<ImRect> items = Find(OverridesMarker);
			Require(!items.empty(), "The overrides should be drawn");
			std::sort(items.begin(), items.end(), [](const ImRect& a, const ImRect& b)
				{
					return a.GetCenter().y < b.GetCenter().y;
				});
			std::vector<std::vector<ImRect>> rows;
			for (const ImRect& item : items)
			{
				if (rows.empty() || item.GetCenter().y - rows.back().front().GetCenter().y > 6.0f)
				{
					rows.emplace_back();
				}
				rows.back().push_back(item);
			}
			for (std::vector<ImRect>& row : rows)
			{
				std::sort(row.begin(), row.end(), [](const ImRect& a, const ImRect& b)
					{
						return a.Min.x < b.Min.x;
					});
			}
			// The tree row first
			rows.erase(rows.begin());
			return rows;
		}

		// The fields of overrides' names: only an array of them draws those
		size_t CountNameFields()
		{
			Frame();
			return Find(NameMarker).size();
		}

		std::vector<ValueOverride>& Overrides()
		{
			return Editing.Scene.GetRegistry().Get<TestLanternComponent>(Lantern).Overrides;
		}

		Tests::HeadlessUI Headless;
		SceneEditor Editing;
		DetailsPanel Details;
		Entity Lantern;
		Entity Marker;

	private:
		static constexpr const char* OverridesMarker = "OverridesFixture: overrides";
		static constexpr const char* NameMarker = "OverridesFixture: ValueOverride::Name";

		static void Mark(SourceLocation overrides, SourceLocation name)
		{
			ReflectionSourceRegistry& sources = ReflectionSourceRegistry::Get();
			sources.Register(*Reflection::FindPropertyByName<TestLanternComponent>("Overrides"), overrides);
			sources.Register(*Reflection::FindPropertyByName<TestMarkerComponent>("Overrides"), overrides);
			sources.Register(*Reflection::FindPropertyByName<ValueOverride>("Name"), name);
		}

		struct Item
		{
			std::string WindowName;
			std::string File;
			bool bWindow = false;
			ImRect Rect;
		};

		static void Collect(const Nyx::UI::SourceItem& item, void* userData)
		{
			OverridesFixture& fixture = *static_cast<OverridesFixture*>(userData);
			fixture.Items.push_back(Item{ item.Window ? std::string(item.Window->Name) : std::string(), item.Declaration.File ? item.Declaration.File : "",
				item.bWindow, ImRect(item.Min, item.Max) });
		}

		std::vector<ImRect> Find(std::string_view marker) const
		{
			std::vector<ImRect> found;
			for (const Item& item : Items)
			{
				if (!item.bWindow && item.WindowName == "Details" && item.File == marker)
				{
					found.push_back(item.Rect);
				}
			}
			return found;
		}

		std::vector<Item> Items;
	};

	// The lantern's values from the module's provider, a row each: typing into an inherited one
	// overrides it, the arrow resets it, each one undo step, and kept in its range
	void TestOverridesInTheDetails()
	{
		OverridesFixture fixture;
		fixture.Frame();
		std::vector<std::vector<ImRect>> rows = fixture.GetRows();
		Require(rows.size() == 3, "The provider's three values should be a row each (" + std::to_string(rows.size()) + ")");
		for (const std::vector<ImRect>& row : rows)
		{
			Require(row.size() == 1, "A value not overridden should have no reset arrow");
		}
		Require(fixture.CountNameFields() == 0, "The overrides shouldn't be drawn as their array");

		// Typed over two frames: one undo step that makes the override
		fixture.Type(rows[0][0], { "2", "6" });
		Require(fixture.Overrides().size() == 1 && fixture.Overrides()[0].Name == "Reach" && fixture.Overrides()[0].Value == 26.0f,
			"Typing into the inherited reach should override it");
		Require(fixture.GetRows()[0].size() == 2, "The overridden reach should have its reset arrow");
		fixture.Editing.Undo();
		Require(fixture.Overrides().empty(), "One undo should take the override away");
		fixture.Editing.Redo();
		Require(fixture.Overrides().size() == 1 && fixture.Overrides()[0].Value == 26.0f, "Redo should bring it back");

		// Kept in its range: the flicker's is 0 to 1
		rows = fixture.GetRows();
		fixture.Type(rows[1][0], { "9" });
		Require(fixture.Overrides().size() == 2 && fixture.Overrides()[1].Name == "Flicker" && fixture.Overrides()[1].Value == 1.0f,
			"A value typed above its range should be kept at its top");

		// An override changed: one undo step
		rows = fixture.GetRows();
		fixture.Type(rows[0][0], { "3", "5" });
		Require(fixture.Overrides().size() == 2 && fixture.Overrides()[0].Value == 35.0f, "Typing into the overridden reach should change it");
		fixture.Editing.Undo();
		Require(fixture.Overrides().size() == 2 && fixture.Overrides()[0].Value == 26.0f, "Undo should give the reach its value back");

		// Reset
		rows = fixture.GetRows();
		fixture.Press(rows[0].back().GetCenter());
		Require(fixture.Overrides().size() == 1 && fixture.Overrides()[0].Name == "Flicker", "The arrow should take the reach's override away");
		Require(fixture.GetRows()[0].size() == 1, "The reach should be inherited again");
		fixture.Editing.Undo();
		Require(fixture.Overrides().size() == 2 && fixture.Overrides()[0].Name == "Reach" && fixture.Overrides()[0].Value == 26.0f,
			"Undo should bring the reset override back");
		fixture.Editing.Undo();
		Require(fixture.Overrides().size() == 1, "Undo should take the flicker's override away");
	}

	// A cold lantern lists no flicker: its override stays, shown as not listed, until removed
	void TestOverrideNotListed()
	{
		OverridesFixture fixture;
		TestLanternComponent& lantern = fixture.Editing.Scene.GetRegistry().Get<TestLanternComponent>(fixture.Lantern);
		lantern.Overrides = { { "Flicker", 0.5f }, { "Reach", 30.0f } };
		lantern.Shade = "Cold";
		std::vector<std::vector<ImRect>> rows = fixture.GetRows();
		Require(rows.size() == 3 && rows[0].size() == 2 && rows[1].size() == 1 && rows[2].size() == 2,
			"A cold lantern should show its reach, overridden, its glow, and the flicker's override as not listed");
		Require(lantern.Overrides.size() == 2, "Drawing should drop nothing");

		fixture.Press(rows[2].back().GetCenter());
		Require(lantern.Overrides.size() == 1 && lantern.Overrides[0].Name == "Reach", "Remove should take the flicker's override away");
		Require(fixture.GetRows().size() == 2, "Its row should be gone");
		fixture.Editing.Undo();
		Require(lantern.Overrides.size() == 2 && lantern.Overrides[0].Name == "Flicker", "Undo should bring it back where it was");

		// Warm again, it overrides the flicker again
		lantern.Shade = "Warm";
		rows = fixture.GetRows();
		Require(rows.size() == 3 && rows[1].size() == 2, "A warm lantern should list the flicker, overridden");
	}

	// The marker's overrides have no provider: they are drawn as their array
	void TestOverridesWithoutProvider()
	{
		OverridesFixture fixture;
		fixture.Editing.Scene.GetSelection() = fixture.Marker;
		fixture.Frame();
		Require(fixture.CountNameFields() == 1, "Without a provider the overrides should be an array, with its element's name");
	}
}

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestModuleRegistersItsSources();
		TestModuleRuntimeComponentsInTheDetails();
		TestOverridesInTheDetails();
		TestOverrideNotListed();
		TestOverridesWithoutProvider();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor game module tests passed.\n";
	return 0;
}
