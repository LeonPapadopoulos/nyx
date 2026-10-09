#include "EditorPreferences.h"
#include "ImGuiDebugTools.h"
#include "ReflectionSourceRegistry.h"
#include "ImGuiSource.h"
#include "SourceNavigation.h"
#include "UISourceInspector.h"
#include "TransformComponent.h"

#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
	using namespace Nyx;
	using namespace Nyx::Editor;

	void Require(bool condition, const char* message)
	{
		if (!condition) throw std::runtime_error(message);
	}

	class TestUI
	{
	public:
		TestUI()
		{
			ImGui::CreateContext();
			auto& io = ImGui::GetIO();
			io.IniFilename = nullptr;
			io.LogFilename = nullptr;
			io.DisplaySize = ImVec2(800, 600);
			io.DeltaTime = 1.0f / 60.0f;
			unsigned char* pixels = nullptr;
			int width = 0, height = 0;
			io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
			Inspector.Attach([this](SourceLocation source) { Opened.push_back(source); });
		}

		~TestUI()
		{
			Inspector.Detach();
			ImGui::DestroyContext();
		}

		void Frame(bool disabled = false, bool declaration = false)
		{
			ImGui::NewFrame();
			ImGui::SetNextWindowPos(ImVec2(20, 20));
			ImGui::SetNextWindowSize(ImVec2(450, 250));
			WindowLine = __LINE__ + 1;
			UI::Begin("Source inspection test");
			{
				UI::SourceDeclarationScope scope(declaration ? SourceLocation{ "Declaration.h", 123 } : SourceLocation{});
				ImGui::BeginDisabled(disabled);
				ButtonLine = __LINE__ + 1;
				if (NYX_UI(ImGui::Button("Delete selected entity", ImVec2(240, 30)))) ++Activations;
				ButtonCenter = ItemCenter();
				ImGui::EndDisabled();
			}
			TextLine = __LINE__ + 1;
			NYX_UI(ImGui::TextUnformatted("Static text can be inspected too"));
			TextCenter = ItemCenter();
			ImGui::End();
			ImGui::Render();
		}

		void Hover(ImVec2 position, bool disabled = false, bool declaration = false)
		{
			ImGui::GetIO().AddMousePosEvent(position.x, position.y);
			Frame(disabled, declaration);
		}

		static ImVec2 ItemCenter()
		{
			const ImVec2 min = ImGui::GetItemRectMin();
			const ImVec2 max = ImGui::GetItemRectMax();
			return ImVec2((min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f);
		}

		void Click(bool disabled = false, bool declaration = false)
		{
			ImGui::GetIO().AddMouseButtonEvent(0, true);
			Frame(disabled, declaration);
			ImGui::GetIO().AddMouseButtonEvent(0, false);
			Frame(disabled, declaration);
		}

		UISourceInspector Inspector;
		std::vector<SourceLocation> Opened;
		ImVec2 ButtonCenter, TextCenter;
		uint32_t WindowLine = 0, ButtonLine = 0, TextLine = 0;
		int Activations = 0;
	};

	void TestInspectionAndInput()
	{
		TestUI ui;
		ui.Frame();
		ui.Hover(ui.ButtonCenter);
		ui.Click();
		Require(ui.Activations == 1, "Ordinary widget behavior changed");
		ui.Inspector.RequestToggle();
		ui.Frame();
		Require(ui.Inspector.IsActive(), "Inspection did not start");
		ui.Hover(ui.ButtonCenter);
		ui.Click();
		Require(ui.Activations == 1, "Inspection activated the underlying button");
		Require(ui.Opened.size() == 1 && ui.Opened.back().Line == ui.ButtonLine, "Button did not map to its exact call site");
		Require(std::filesystem::path(ui.Opened.back().File).filename() == "UISourceInspectorTests.cpp", "Wrapper captured its own header");
		Require(!ui.Inspector.IsActive(), "Successful inspection did not exit");
		ui.Click();
		Require(ui.Activations == 2, "Inspection left input stuck afterwards");

		ui.Inspector.RequestToggle();
		ui.Frame(true, true);
		ui.Hover(ui.ButtonCenter, true, true);
		ui.Click(true, true);
		Require(ui.Opened.size() == 2 && ui.Opened.back().Line == 123, "Disabled field did not open its declaration");
		Require(ui.Activations == 2, "Disabled inspection activated the control");

		ui.Inspector.RequestToggle();
		ui.Frame(false, true);
		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, true);
		ui.Hover(ui.ButtonCenter, false, true);
		ui.Click(false, true);
		Require(ui.Opened.size() == 3 && ui.Opened.back().Line == ui.ButtonLine, "Shift-click did not open widget code");
		ImGui::GetIO().AddKeyEvent(ImGuiMod_Shift, false);
		ui.Frame();

		ui.Inspector.RequestToggle();
		ui.Frame();
		ui.Hover(ui.TextCenter);
		ui.Click();
		Require(ui.Opened.back().Line == ui.TextLine, "Static text has no source mapping");

		ui.Inspector.RequestToggle();
		ui.Frame();
		ui.Hover(ImVec2(420, 230));
		ui.Click();
		Require(ui.Opened.back().Line == ui.WindowLine, "Panel fallback points to the wrapper definition");

		const size_t openedBeforeCancel = ui.Opened.size();
		ImGui::GetIO().AddKeyEvent(ImGuiKey_F8, true);
		ui.Frame();
		Require(ui.Inspector.IsActive(), "F8 did not activate inspection");
		ImGui::GetIO().AddKeyEvent(ImGuiKey_F8, false);
		ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
		ui.Frame();
		Require(!ui.Inspector.IsActive() && ui.Opened.size() == openedBeforeCancel, "Escape did not cancel safely");
	}

	void TestActiveDragIsPreserved()
	{
		TestUI ui;
		ui.Frame();
		ui.Hover(ui.ButtonCenter);
		ImGui::GetIO().AddMouseButtonEvent(0, true);
		ui.Frame();
		ui.Inspector.RequestToggle();
		ui.Frame();
		Require(!ui.Inspector.IsActive(), "Inspection interrupted an active interaction");
		ImGui::GetIO().AddMouseButtonEvent(0, false);
		ui.Frame();
		Require(ui.Activations == 1 && ui.Opened.empty(), "An active interaction could not finish normally");
	}

	void TestReflectionSource()
	{
		RegisterRuntimeReflectedSources();
		const auto& sources = ReflectionSourceRegistry::Get();
		const auto& type = Reflection::GetTypeMetadata<Engine::TransformComponent>();
		Require(sources.Find(type).IsValid(), "Component declaration has no source");
		bool foundPosition = false;
		for (size_t i = 0; i < type.PropertyCount; ++i)
		{
			const auto& property = type.Properties[i];
			if (std::string_view(property.Name) != "Position") continue;
			foundPosition = true;
			const SourceLocation source = sources.Find(property);
			Require(source.IsValid(), "Property declaration has no source");
			std::ifstream file(source.File);
			std::string line;
			for (uint32_t row = 0; row < source.Line; ++row) std::getline(file, line);
			Require(line.find("glm::vec3 Position") != std::string::npos, "Property source line does not identify its declaration");
		}
		Require(foundPosition, "Position property missing");
		const Reflection::TypeMetadata unregisteredType{};
		Require(!sources.Find(unregisteredType).IsValid(), "Unregistered types must fall back to widget source");
	}

	void TestDebugPickerInteroperation()
	{
		TestUI ui;
		ui.Frame();
		ui.Hover(ui.ButtonCenter);

		ImGui::DebugStartItemPicker();
		ui.Inspector.RequestToggle();
		ui.Frame();
		Require(ui.Inspector.IsActive(), "Nyx source inspection did not take over from Item Picker");
		Require(!ImGui::GetCurrentContext()->DebugItemPickerActive, "Both UI pickers remained active");
		ui.Hover(ui.ButtonCenter);
		ui.Click();
		Require(ui.Opened.size() == 1 && ui.Activations == 0, "Picking after switching tools did not open source safely");

		ImGuiDebugTools debugTools;
		debugTools.BeforeFrame();
		if (!ImGui::GetIO().ConfigDebugIsDebuggerPresent)
		{
			// ImGui's own Metrics window can arm the picker without a debugger attached.
			ImGui::DebugStartItemPicker();
			ImGui::GetCurrentContext()->DebugBreakInWindow = ImGui::FindWindowByName("Source inspection test")->ID;
			debugTools.BeforeFrame();
			ui.Frame();
			Require(!ImGui::GetCurrentContext()->DebugItemPickerActive, "Item Picker remained armed without a debugger");
		}
	}

	void TestPreferencesAndNavigation()
	{
		const auto path = std::filesystem::temp_directory_path() /
			("NyxSourcePreferences-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini");
		Require(!std::filesystem::exists(path), "Test preference path already exists");
		EditorPreferences preferences;
		Require(preferences.Load(path) && preferences.SourceEditor == ESourceEditor::VisualStudio, "Default preference is not Visual Studio");
		preferences.SourceEditor = ESourceEditor::VisualStudioCode;
		Require(preferences.Save(path), "Preference save failed");
		EditorPreferences loaded;
		Require(loaded.Load(path) && loaded.SourceEditor == ESourceEditor::VisualStudioCode, "Saved IDE preference was not restored");
		std::filesystem::remove(path);
		const auto uri = MakeVSCodeSourceUri(L"C:/Example project/hash#percent%\u00FC.h", 42);
		Require(uri == L"vscode://file/C:/Example%20project/hash%23percent%25%C3%BC.h:42", "Source URI did not encode special characters safely");
		Require(!OpenSourceLocation({}, ESourceEditor::VisualStudio).empty(), "Missing source should report an error without launching an IDE");
	}
}

int main()
{
	try
	{
		TestInspectionAndInput();
		TestActiveDragIsPreserved();
		TestReflectionSource();
		TestDebugPickerInteroperation();
		TestPreferencesAndNavigation();
		std::cout << "All UI source inspector tests passed.\n";
		return 0;
	}
	catch (const std::exception& error)
	{
		std::cerr << "UI source inspector test failed: " << error.what() << '\n';
		return 1;
	}
}
