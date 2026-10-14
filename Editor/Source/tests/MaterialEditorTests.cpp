// Editing a material without a window or GPU: the material document's edits, undo and redo, a
// drag as one undo step, unsaved values against the file, saving with the file's other lines
// kept, and following the file when it is saved elsewhere. The Parameters window drawn headless
// over a stand-in for the renderer: every kind of parameter drawn, edits sent to the renderer,
// undo, redo and save keys while it has focus, the file followed while nothing is unsaved, and
// the question before another material opens over unsaved values.
#include "MaterialDocument.h"
#include "MaterialParametersPanel.h"
#include "Renderer.h"
#include "ShaderLayout.h"

#include <imgui.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Editor;
	using Nyx::Engine::EShaderFieldKind;
	using Nyx::Engine::EShaderScalarType;
	using Nyx::Engine::MaterialAsset;
	using Nyx::Engine::ShaderAttribute;
	using Nyx::Engine::ShaderAttributeArgument;
	using Nyx::Engine::ShaderField;
	using Nyx::Engine::ShaderLayout;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	struct TempAssets
	{
		std::filesystem::path Path = std::filesystem::temp_directory_path() / ("NyxMaterialEditorTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		TempAssets()
		{
			std::filesystem::create_directories(Path / "Materials");
		}

		~TempAssets()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		void Write(const std::string& name, const std::string& text) const
		{
			std::ofstream stream(Path / name, std::ios::binary | std::ios::trunc);
			stream << text;
		}

		std::string Read(const std::string& name) const
		{
			std::ifstream stream(Path / name, std::ios::binary);
			std::stringstream text;
			text << stream.rdbuf();
			return text.str();
		}
	};

	const std::string TestMaterial = "# A material of the tests\n"
									 "Shader = Shaders/Test.slang\n"
									 "Roughness = 0.25\n"
									 "\n"
									 "BaseColor = 1 0 0\n"
									 "Gone = 3\n";

	MaterialAsset Parsed(const std::string& text)
	{
		MaterialAsset asset;
		std::string error;
		Require(asset.Parse(text, error), "the material doesn't parse: " + error);
		return asset;
	}

	ShaderAttribute Attribute(std::string name, std::vector<ShaderAttributeArgument> arguments = {})
	{
		return ShaderAttribute{ std::move(name), std::move(arguments) };
	}

	ShaderAttributeArgument Text(std::string text)
	{
		return ShaderAttributeArgument{ .bText = true, .Text = std::move(text) };
	}

	ShaderAttributeArgument Number(double number)
	{
		return ShaderAttributeArgument{ .Number = number };
	}

	ShaderField Field(std::string name, EShaderFieldKind kind, EShaderScalarType scalar, uint32_t columns, uint32_t offset, std::vector<ShaderAttribute> attributes = {})
	{
		ShaderField field;
		field.Name = std::move(name);
		field.Kind = kind;
		field.Scalar = scalar;
		field.Columns = columns;
		field.Offset = offset;
		field.Size = kind == EShaderFieldKind::Texture ? 4 : columns * 4;
		field.Attributes = std::move(attributes);
		return field;
	}

	// A parameter of every kind the window draws, and the bytes of their defaults
	ShaderLayout TestLayout()
	{
		ShaderLayout layout;
		layout.TypeName = "Test";
		layout.Fields.push_back(Field("BaseColor", EShaderFieldKind::Vector, EShaderScalarType::Float, 3, 0, { Attribute("Category", { Text("Surface") }), Attribute("Color") }));
		layout.Fields.push_back(Field("Roughness", EShaderFieldKind::Scalar, EShaderScalarType::Float, 1, 12, { Attribute("Category", { Text("Surface") }), Attribute("Range", { Number(0), Number(1) }) }));
		layout.Fields.push_back(Field("Shiny", EShaderFieldKind::Scalar, EShaderScalarType::Bool, 1, 16, { Attribute("Switch") }));
		layout.Fields.push_back(Field("Mode", EShaderFieldKind::Scalar, EShaderScalarType::Int, 1, 20, { Attribute("Enum", { Text("Off, On, Debug") }) }));
		layout.Fields.push_back(Field("Albedo", EShaderFieldKind::Texture, EShaderScalarType::UInt, 1, 24, { Attribute("Category", { Text("Textures") }), Attribute("Default", { Text("White") }) }));
		ShaderField layer = Field("Layer", EShaderFieldKind::Struct, EShaderScalarType::None, 1, 28);
		layer.Fields.push_back(Field("Tint", EShaderFieldKind::Vector, EShaderScalarType::Float, 3, 0));
		layer.Fields.push_back(Field("Amount", EShaderFieldKind::Scalar, EShaderScalarType::Float, 1, 12, { Attribute("Tooltip", { Text("How much of it") }) }));
		layer.Size = 16;
		layout.Fields.push_back(layer);
		layout.Fields.push_back(Field("Tiling", EShaderFieldKind::Vector, EShaderScalarType::Float, 2, 44, { Attribute("DragSpeed", { Number(0.01) }) }));
		layout.Fields.push_back(Field("Steps", EShaderFieldKind::Scalar, EShaderScalarType::UInt, 1, 52));
		layout.Fields.push_back(Field("Secret", EShaderFieldKind::Scalar, EShaderScalarType::Float, 1, 56, { Attribute("Hidden") }));
		layout.Size = 60;
		return layout;
	}

	std::vector<std::byte> TestDefaults(const ShaderLayout& layout)
	{
		std::vector<std::byte> bytes(layout.Size, std::byte{ 0 });
		const auto put = [&bytes](uint32_t offset, float value)
		{
			std::memcpy(bytes.data() + offset, &value, sizeof(value));
		};
		put(0, 0.62f);
		put(4, 0.6f);
		put(8, 0.56f);
		put(12, 0.78f);
		put(44, 1.0f);
		put(48, 1.0f);
		return bytes;
	}

	// The renderer as the window sees it: a material's values, its file read again when a test
	// says, and what the window sent
	struct FakeMaterials : Nyx::IMaterialSource
	{
		ShaderLayout Layout = TestLayout();
		std::vector<std::byte> Defaults = TestDefaults(Layout);
		bool bCompiled = true;
		std::map<std::string, MaterialAsset> Assets;
		std::map<std::string, uint64_t> FileRevisions;
		std::vector<std::pair<std::string, std::string>> Sent;
		std::filesystem::path Folder;

		Nyx::MaterialView ViewMaterial(const std::string& path) override
		{
			if (!Assets.contains(path))
			{
				ReadFile(path);
			}
			return Nyx::MaterialView{ .Asset = &Assets[path], .Layout = bCompiled ? &Layout : nullptr, .Defaults = Defaults, .FileRevision = FileRevisions[path] };
		}

		void SetMaterialValues(const std::string& path, const MaterialAsset& asset) override
		{
			Assets[path] = asset;
			Sent.emplace_back(path, asset.ToText());
		}

		// As the renderer does once a saved file settled
		void ReadFile(const std::string& path)
		{
			std::string error;
			Require(Assets[path].Load(Folder / path, error), "the fake can't read " + path + ": " + error);
			++FileRevisions[path];
		}

		std::string LastSent(const std::string& path) const
		{
			for (auto sent = Sent.rbegin(); sent != Sent.rend(); ++sent)
			{
				if (sent->first == path)
				{
					return sent->second;
				}
			}
			return {};
		}
	};

	// A headless ImGui context for the duration of a test
	class HeadlessUI
	{
	public:
		HeadlessUI()
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
		}

		~HeadlessUI()
		{
			ImGui::DestroyContext();
		}

		void Frame(const std::function<void()>& draw)
		{
			ImGui::NewFrame();
			draw();
			ImGui::Render();
		}

		// One frame in which the key (with Ctrl, and Shift when asked) is pressed, then one in
		// which it is released
		void Press(ImGuiKey key, bool bCtrl, bool bShift, const std::function<void()>& draw)
		{
			ImGuiIO& io = ImGui::GetIO();
			io.AddKeyEvent(ImGuiMod_Ctrl, bCtrl);
			io.AddKeyEvent(ImGuiMod_Shift, bShift);
			io.AddKeyEvent(key, true);
			Frame(draw);
			io.AddKeyEvent(key, false);
			io.AddKeyEvent(ImGuiMod_Shift, false);
			io.AddKeyEvent(ImGuiMod_Ctrl, false);
			Frame(draw);
		}
	};

	void TestEditsUndoAndSave()
	{
		TempAssets assets;
		assets.Write("Materials/Test.nyxmat", TestMaterial);
		MaterialDocument document;
		std::string error;
		Require(document.Open(assets.Path, "Materials/Test.nyxmat", error), "the material doesn't open: " + error);
		Require(!document.IsDirty() && !document.GetUndoLabel(), "a material just opened has unsaved values or something to undo");

		document.SetValue("Roughness", "0.5", "Edit Roughness");
		Require(document.IsDirty() && *document.GetAsset().FindValue("Roughness") == "0.5", "setting a value didn't change it");
		Require(document.GetUndoLabel() && *document.GetUndoLabel() == "Edit Roughness", "the edit isn't the step to undo");
		document.SetValue("Roughness", "0.5", "Edit Roughness again");
		Require(*document.GetUndoLabel() == "Edit Roughness", "an edit that changes nothing became a step");

		// A value removed takes the default; undo puts its line back where it was
		document.SetValue("BaseColor", std::nullopt, "Reset BaseColor");
		Require(!document.GetAsset().FindValue("BaseColor"), "the reset value is still set");
		Require(document.Undo() && document.Undo(), "two steps don't undo");
		Require(!document.IsDirty() && document.GetAsset().ToText() == TestMaterial, "undoing everything doesn't give the file's text back:\n" + document.GetAsset().ToText());
		Require(document.Redo() && *document.GetAsset().FindValue("Roughness") == "0.5" && document.GetRedoLabel(), "redo doesn't set the value again");
		document.SetValue("Mode", "1", "Edit Mode");
		Require(!document.GetRedoLabel(), "a new edit kept the steps to redo");

		// A drag: many values, one step back to where it started
		const uint64_t revision = document.GetRevision();
		document.PreviewValue("Roughness", "0.6");
		document.PreviewValue("Roughness", "0.7");
		document.PreviewValue("Tiling", "2 2");
		Require(document.IsPreviewing() && document.GetRevision() > revision && *document.GetAsset().FindValue("Roughness") == "0.7", "a drag doesn't change the value at once");
		document.EndPreview("Drag");
		Require(!document.IsPreviewing() && *document.GetUndoLabel() == "Drag", "the drag isn't one step");
		Require(document.Undo() && *document.GetAsset().FindValue("Roughness") == "0.5" && !document.GetAsset().FindValue("Tiling"), "undoing the drag doesn't go back to where it started");
		document.PreviewValue("Roughness", "0.9");
		document.PreviewValue("Roughness", "0.5");
		document.EndPreview("Drag back");
		Require(*document.GetUndoLabel() == "Edit Mode", "a drag that ended where it started became a step");

		// Saved: the file's comment, blank line and unknown value stay, in their places
		Require(document.Save(error), "the material doesn't save: " + error);
		Require(!document.IsDirty(), "a saved material has unsaved values");
		const std::string saved = assets.Read("Materials/Test.nyxmat");
		Require(saved == "# A material of the tests\nShader = Shaders/Test.slang\nRoughness = 0.5\n\nBaseColor = 1 0 0\nGone = 3\nMode = 1\n", "the saved file is:\n" + saved);
		Require(document.Undo() && document.IsDirty(), "undo after saving isn't unsaved");
	}

	void TestFollowingTheFile()
	{
		TempAssets assets;
		assets.Write("Materials/Test.nyxmat", TestMaterial);
		MaterialDocument document;
		std::string error;
		Require(document.Open(assets.Path, "Materials/Test.nyxmat", error), "the material doesn't open: " + error);
		document.SetValue("Roughness", "0.5", "Edit Roughness");
		Require(document.Save(error), "the material doesn't save: " + error);

		// Saved elsewhere while nothing is unsaved here: taken, and what undo had is gone
		Require(document.FollowFile(Parsed("Shader = Shaders/Test.slang\nRoughness = 0.9\n")), "the file saved elsewhere isn't taken");
		Require(*document.GetAsset().FindValue("Roughness") == "0.9" && !document.IsDirty() && !document.GetUndoLabel(), "the document doesn't hold the file as it is now");

		// While there are unsaved values, they stay
		document.SetValue("Roughness", "0.1", "Edit Roughness");
		Require(!document.FollowFile(Parsed("Shader = Shaders/Test.slang\nRoughness = 0.3\n")), "unsaved values were thrown away for the file");
		Require(*document.GetAsset().FindValue("Roughness") == "0.1" && document.IsDirty(), "the unsaved value didn't stay");
	}

	void TestParametersWindow()
	{
		TempAssets assets;
		assets.Write("Materials/Test.nyxmat", TestMaterial);
		assets.Write("Materials/Other.nyxmat", "Shader = Shaders/Test.slang\nRoughness = 1\n");
		FakeMaterials materials;
		materials.Folder = assets.Path;
		HeadlessUI ui;
		MaterialParametersPanel panel(assets.Path);
		const auto draw = [&]()
		{
			panel.Draw(materials);
			panel.HandleHotkeys();
		};

		// Nothing open: the window says how to open one
		panel.bOpen = true;
		ui.Frame(draw);
		Require(!panel.GetDocument().IsOpen(), "a material is open before one was asked for");

		// Opened, drawn with every kind of parameter, and its values sent
		panel.OpenMaterial("Materials/Test.nyxmat");
		ui.Frame(draw);
		ui.Frame(draw);
		MaterialDocument& document = panel.GetDocument();
		Require(document.IsOpen() && document.GetPath() == "Materials/Test.nyxmat", "the material didn't open: " + panel.GetStatus());
		Require(materials.LastSent("Materials/Test.nyxmat") == TestMaterial, "the window didn't send the material's values");

		// An edit reaches the renderer in the next frame; a drag ends when nothing is dragged
		document.PreviewValue("Roughness", "0.4");
		ui.Frame(draw);
		Require(materials.LastSent("Materials/Test.nyxmat").find("Roughness = 0.4") != std::string::npos, "the edit didn't reach the renderer");
		Require(!document.IsPreviewing() && document.GetUndoLabel(), "the drag didn't end once nothing was dragged");

		// Its keys while it has focus: undo, redo, save
		ui.Frame([&]()
			{
				draw();
				ImGui::SetWindowFocus("Parameters");
			});
		ui.Frame(draw);
		ui.Press(ImGuiKey_Z, true, false, draw);
		Require(*document.GetAsset().FindValue("Roughness") == "0.25", "Ctrl+Z didn't undo in the material");
		Require(materials.LastSent("Materials/Test.nyxmat") == TestMaterial, "the undone value didn't reach the renderer");
		ui.Press(ImGuiKey_Y, true, false, draw);
		Require(*document.GetAsset().FindValue("Roughness") == "0.4", "Ctrl+Y didn't redo in the material");
		ui.Press(ImGuiKey_S, true, false, draw);
		Require(!document.IsDirty() && assets.Read("Materials/Test.nyxmat").find("Roughness = 0.4") != std::string::npos, "Ctrl+S didn't save the material");

		// The file saved elsewhere and read again: followed
		assets.Write("Materials/Test.nyxmat", "Shader = Shaders/Test.slang\nRoughness = 0.8\n");
		materials.ReadFile("Materials/Test.nyxmat");
		ui.Frame(draw);
		Require(*document.GetAsset().FindValue("Roughness") == "0.8" && !document.IsDirty(), "the file read again wasn't followed");

		// ...but not over unsaved values, which the renderer gets back
		document.SetValue("Roughness", "0.2", "Edit Roughness");
		ui.Frame(draw);
		assets.Write("Materials/Test.nyxmat", "Shader = Shaders/Test.slang\nRoughness = 0.7\n");
		materials.ReadFile("Materials/Test.nyxmat");
		ui.Frame(draw);
		Require(*document.GetAsset().FindValue("Roughness") == "0.2", "the file read again threw the unsaved value away");
		Require(materials.LastSent("Materials/Test.nyxmat").find("Roughness = 0.2") != std::string::npos, "the renderer didn't get the unsaved value back");

		// Another material over unsaved values: asked first. Escape keeps this one...
		panel.OpenMaterial("Materials/Other.nyxmat");
		ui.Frame(draw);
		Require(ImGui::IsPopupOpen("Unsaved Material", ImGuiPopupFlags_AnyPopupId), "opening another material over unsaved values didn't ask");
		ui.Press(ImGuiKey_Escape, false, false, draw);
		Require(document.GetPath() == "Materials/Test.nyxmat" && document.IsDirty(), "cancelling didn't keep the material and its values");

		// ...Enter saves it and opens the other
		panel.OpenMaterial("Materials/Other.nyxmat");
		ui.Frame(draw);
		ui.Press(ImGuiKey_Enter, false, false, draw);
		Require(document.GetPath() == "Materials/Other.nyxmat" && !document.IsDirty(), "saving didn't open the other material");
		Require(assets.Read("Materials/Test.nyxmat").find("Roughness = 0.2") != std::string::npos, "the material wasn't saved before the other opened");

		// A shader that hasn't compiled: the file's values, as text
		materials.bCompiled = false;
		ui.Frame(draw);
		ui.Frame(draw);
	}
}

int main()
{
	try
	{
		TestEditsUndoAndSave();
		TestFollowingTheFile();
		TestParametersWindow();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}
	std::cout << "All material editor tests passed\n";
	return 0;
}
