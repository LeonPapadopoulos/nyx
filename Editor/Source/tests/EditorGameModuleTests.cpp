// The editor with a game module (NyxTestModule, linked as the editor links a game's module): the
// module gives the editor where its types are declared, so the source inspector can open them, and
// the Details panel reads a game's run-time components of the module's types.
#include "BinaryArchive.h"
#include "ComponentEdits.h"
#include "ComponentRegistration.h"
#include "ComponentTypeRegistry.h"
#include "DetailsPanel.h"
#include "GameModule.h"
#include "HeadlessUI.h"
#include "Log.h"
#include "PropertyWidgetRegistry.h"
#include "ReflectedArchiveSerializer.h"
#include "ReflectionSourceRegistry.h"
#include "SceneEditor.h"
#include "TestLanternComponent.h"
#include "TestLanternGlowComponent.h"
#include "TestSupport.h"

#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

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

int main()
{
	Nyx::Logger::Get().Init();

	try
	{
		TestModuleRegistersItsSources();
		TestModuleRuntimeComponentsInTheDetails();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Editor game module test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All editor game module tests passed.\n";
	return 0;
}
