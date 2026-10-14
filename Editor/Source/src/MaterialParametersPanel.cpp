#include "MaterialParametersPanel.h"

#include "ImGuiSource.h"
#include "Renderer.h"
#include "ShaderLayout.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		std::string_view Trim(std::string_view text)
		{
			while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
			{
				text.remove_prefix(1);
			}
			while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
			{
				text.remove_suffix(1);
			}
			return text;
		}

		// The attribute's text argument, e.g. [Category("Surface")]; null without one
		const std::string* TextArgument(const Nyx::Engine::ShaderField& field, std::string_view attribute)
		{
			const Nyx::Engine::ShaderAttribute* found = field.FindAttribute(attribute);
			return found && !found->Arguments.empty() && found->Arguments[0].bText ? &found->Arguments[0].Text : nullptr;
		}

		std::optional<double> NumberArgument(const Nyx::Engine::ShaderField& field, std::string_view attribute, size_t index)
		{
			const Nyx::Engine::ShaderAttribute* found = field.FindAttribute(attribute);
			if (!found || found->Arguments.size() <= index || found->Arguments[index].bText)
			{
				return std::nullopt;
			}
			return found->Arguments[index].Number;
		}

		std::string Category(const Nyx::Engine::ShaderField& field)
		{
			const std::string* category = TextArgument(field, "Category");
			return category ? *category : std::string();
		}

		// The same value, also when written differently ("0.5" and "0.50")
		bool SameValue(const std::string& a, const std::string& b)
		{
			if (a == b)
			{
				return true;
			}
			const std::optional<std::vector<double>> left = Nyx::Engine::ParseMaterialNumbers(a);
			const std::optional<std::vector<double>> right = Nyx::Engine::ParseMaterialNumbers(b);
			if (!left || !right || left->size() != right->size() || left->empty())
			{
				return false;
			}
			for (size_t index = 0; index < left->size(); ++index)
			{
				if (static_cast<float>((*left)[index]) != static_cast<float>((*right)[index]))
				{
					return false;
				}
			}
			return true;
		}

		// The names of the parameters a material file can set ("Layer.Tint" for a struct's member)
		void CollectNames(const std::vector<Nyx::Engine::ShaderField>& fields, const std::string& prefix, std::set<std::string>& names)
		{
			for (const Nyx::Engine::ShaderField& field : fields)
			{
				if (field.Kind == Nyx::Engine::EShaderFieldKind::Struct)
				{
					CollectNames(field.Fields, prefix + field.Name + ".", names);
				}
				else
				{
					names.insert(prefix + field.Name);
				}
			}
		}

		void TextInColor(const glm::vec4& color, const char* text)
		{
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(color.r, color.g, color.b, color.a));
			NYX_UI(ImGui::TextWrapped("%s", text));
			ImGui::PopStyleColor();
		}
	}

	MaterialParametersPanel::MaterialParametersPanel(std::filesystem::path assetsFolder)
		: AssetsFolder(std::move(assetsFolder))
	{
	}

	void MaterialParametersPanel::OpenMaterial(std::string path)
	{
		bOpen = true;
		if (Document.IsOpen() && path == Document.GetPath())
		{
			ImGui::SetWindowFocus("Parameters");
			return;
		}
		PendingOpen = std::move(path);
		bAskToDiscard = Document.IsDirty();
	}

	void MaterialParametersPanel::Draw(Nyx::IMaterialSource& materials)
	{
		// A drag let go of, or a field left, last frame: one undo step
		if (Document.IsPreviewing() && !ImGui::IsAnyItemActive())
		{
			Document.EndPreview(PreviewLabel);
		}
		// A material asked for, with no unsaved values to ask about
		if (!PendingOpen.empty() && !bAskToDiscard && !Document.IsDirty())
		{
			Open(PendingOpen, materials);
			PendingOpen.clear();
		}
		Sync(materials);

		bFocused = false;
		if (!bOpen)
		{
			return;
		}
		ImGui::SetNextWindowSize(ImVec2(420.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Parameters", &bOpen))
		{
			ImGui::End();
			return;
		}
		bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		DrawDiscardPopup(materials);

		if (!Document.IsOpen())
		{
			NYX_UI(ImGui::TextWrapped("No material is open. Double-click one in the asset browser to edit it here."));
			ImGui::End();
			return;
		}

		const Nyx::UI::Theme& theme = Nyx::UI::GetTheme();
		const Nyx::MaterialView view = materials.ViewMaterial(Document.GetPath());
		const bool bDirty = Document.IsDirty();
		const std::string name = std::filesystem::path(Document.GetPath()).stem().string();
		NYX_UI(Nyx::UI::SectionHeader((name + (bDirty ? " *" : "")).c_str()));
		ImGui::SetItemTooltip("%s", Document.GetFile().string().c_str());
		const std::string shader = Document.GetAsset().GetShader();
		NYX_UI(ImGui::TextDisabled("Shader: %s", shader.empty() ? "none" : shader.c_str()));

		if (NYX_UI(Nyx::UI::Button("Save")))
		{
			Save();
		}
		ImGui::SetItemTooltip("Writes the values to %s (Ctrl+S)", Document.GetPath().c_str());
		ImGui::SameLine();
		ImGui::BeginDisabled(!bDirty);
		if (NYX_UI(Nyx::UI::Button("Revert")))
		{
			Discarded = Document.GetPath();
			Open(Document.GetPath(), materials);
		}
		ImGui::EndDisabled();
		ImGui::SetItemTooltip("Back to what the file holds; undo can't take it back");
		ImGui::SameLine();
		ImGui::AlignTextToFramePadding();
		if (bStatusFailed)
		{
			TextInColor(theme.StatusError, Status.c_str());
		}
		else if (bDirty)
		{
			TextInColor(theme.TextInfo, "Unsaved changes");
		}
		else
		{
			NYX_UI(ImGui::TextDisabled("%s", Status.c_str()));
		}
		NYX_UI(Nyx::UI::Separator());

		DrawMaterial();
		DrawParameters(view);
		ImGui::End();
	}

	void MaterialParametersPanel::DrawMaterial()
	{
		if (!NYX_UI(Nyx::UI::CollapsingHeader("Material")))
		{
			return;
		}
		const std::string shader = Document.GetAsset().GetShader();
		Nyx::UI::BeginPropertyRow("Shader");
		if (Nyx::UI::BeginCombo("##Shader", shader.empty() ? "None" : shader.c_str()))
		{
			for (const std::string& choice : ShaderChoices)
			{
				if (NYX_UI(Nyx::UI::Selectable(choice.c_str(), choice == shader)) && choice != shader)
				{
					// Values the new shader doesn't know stay in the file, under Not in the Shader
					Document.SetValue("Shader", choice, "Change Shader");
				}
			}
			Nyx::UI::EndCombo();
		}
		Nyx::UI::EndPropertyRow();
		Nyx::UI::BeginPropertyRow("Domain");
		ImGui::AlignTextToFramePadding();
		NYX_UI(ImGui::TextUnformatted("Surface"));
		Nyx::UI::EndPropertyRow();
		Nyx::UI::BeginPropertyRow("Used By");
		if (Users.empty())
		{
			ImGui::AlignTextToFramePadding();
			NYX_UI(ImGui::TextDisabled("No entity in the scene"));
		}
		for (size_t index = 0; index < Users.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			if (NYX_UI(Nyx::UI::Selectable(Users[index].Name.c_str())) && Users[index].Select)
			{
				Users[index].Select();
			}
			ImGui::SetItemTooltip("Selects it in the scene");
			ImGui::PopID();
		}
		Nyx::UI::EndPropertyRow();
	}

	bool MaterialParametersPanel::HandleHotkeys()
	{
		// Text fields use these keys for their text
		if (!bFocused || !Document.IsOpen() || ImGui::GetIO().WantTextInput)
		{
			return bFocused;
		}
		if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z))
		{
			Document.Undo();
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z) || ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Y))
		{
			Document.Redo();
		}
		else if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_S))
		{
			Save();
		}
		return true;
	}

	void MaterialParametersPanel::Sync(Nyx::IMaterialSource& materials)
	{
		// The renderer drew values that weren't saved: it draws the file again
		if (!Discarded.empty())
		{
			Nyx::Engine::MaterialAsset file;
			std::string error;
			if (file.Load((AssetsFolder / Discarded).make_preferred(), error))
			{
				materials.SetMaterialValues(Discarded, file);
			}
			Discarded.clear();
		}
		if (!Document.IsOpen())
		{
			return;
		}

		// The file was read again: taken, unless there are unsaved values here, which the
		// renderer then gets again
		const Nyx::MaterialView view = materials.ViewMaterial(Document.GetPath());
		if (view.FileRevision != SeenFileRevision && view.FileRevision != 0)
		{
			SeenFileRevision = view.FileRevision;
			if (view.Asset && Document.FollowFile(*view.Asset))
			{
				TextFields.clear();
			}
			else
			{
				SentRevision = 0;
			}
		}
		if (Document.GetRevision() != SentRevision)
		{
			materials.SetMaterialValues(Document.GetPath(), Document.GetAsset());
			SentRevision = Document.GetRevision();
		}
	}

	void MaterialParametersPanel::Open(const std::string& path, Nyx::IMaterialSource& materials)
	{
		std::string error;
		if (!Document.Open(AssetsFolder, path, error))
		{
			Status = "Couldn't open " + path + ": " + error;
			bStatusFailed = true;
			return;
		}
		// What the file holds now is in the document; a later read of it is followed
		SeenFileRevision = materials.ViewMaterial(path).FileRevision;
		SentRevision = 0;
		TextFields.clear();
		ActiveTextField.clear();
		Status = "Opened " + path;
		bStatusFailed = false;
	}

	void MaterialParametersPanel::Save()
	{
		std::string error;
		if (Document.Save(error))
		{
			Status = "Saved " + Document.GetPath();
			bStatusFailed = false;
		}
		else
		{
			Status = "Couldn't save " + Document.GetPath() + ": " + error;
			bStatusFailed = true;
		}
	}

	void MaterialParametersPanel::DrawDiscardPopup(Nyx::IMaterialSource& materials)
	{
		constexpr const char* Title = "Unsaved Material";
		if (bAskToDiscard)
		{
			ImGui::OpenPopup(Title);
			bAskToDiscard = false;
		}
		if (!Nyx::UI::BeginPopupModal(Title, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		NYX_UI(ImGui::Text("%s has unsaved changes.", Document.GetPath().c_str()));
		NYX_UI(ImGui::Text("Save them before opening %s?", PendingOpen.c_str()));
		ImGui::Spacing();
		// Enter saves, Escape cancels
		if (NYX_UI(Nyx::UI::Button("Save")) || ImGui::IsKeyPressed(ImGuiKey_Enter, false))
		{
			Save();
			if (!bStatusFailed)
			{
				Open(PendingOpen, materials);
			}
			PendingOpen.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Don't Save")))
		{
			Discarded = Document.GetPath();
			Open(PendingOpen, materials);
			PendingOpen.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			PendingOpen.clear();
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	void MaterialParametersPanel::DrawParameters(const Nyx::MaterialView& view)
	{
		if (!view.Layout)
		{
			const std::string shader = Document.GetAsset().GetShader();
			NYX_UI(ImGui::TextWrapped("Its shader, %s, hasn't compiled yet; its parameters show once it has. The file's values:", shader.c_str()));
			for (const auto& [name, value] : Document.GetAsset().GetValues())
			{
				ImGui::PushID(name.c_str());
				Nyx::UI::BeginPropertyRow(name.c_str());
				DrawTextValue(name, value, nullptr, "Edit " + name);
				Nyx::UI::EndPropertyRow();
				ImGui::PopID();
			}
			return;
		}

		// Fields without a category first, then a heading per category, in the order they come
		std::vector<std::string> categories;
		for (const Nyx::Engine::ShaderField& field : view.Layout->Fields)
		{
			const std::string category = Category(field);
			if (!category.empty() && std::find(categories.begin(), categories.end(), category) == categories.end())
			{
				categories.push_back(category);
			}
		}
		const std::string uncategorized;
		DrawFields(view.Layout->Fields, "", 0, view, &uncategorized);
		for (const std::string& category : categories)
		{
			if (NYX_UI(Nyx::UI::CollapsingHeader(category.c_str())))
			{
				DrawFields(view.Layout->Fields, "", 0, view, &category);
			}
		}
		DrawUnknownValues(view);
	}

	void MaterialParametersPanel::DrawFields(const std::vector<Nyx::Engine::ShaderField>& fields, const std::string& prefix, uint32_t base, const Nyx::MaterialView& view,
		const std::string* category)
	{
		for (const Nyx::Engine::ShaderField& field : fields)
		{
			if ((category && Category(field) != *category) || field.FindAttribute("Hidden"))
			{
				continue;
			}
			const std::string name = prefix + field.Name;
			if (field.Kind == Nyx::Engine::EShaderFieldKind::Struct)
			{
				const std::string* displayName = TextArgument(field, "DisplayName");
				ImGui::PushID(name.c_str());
				Nyx::UI::TreeRowOptions options;
				options.bToggleOnClick = true;
				options.bDefaultOpen = true;
				if (NYX_UI(Nyx::UI::TreeRow(displayName ? displayName->c_str() : field.Name.c_str(), options)).bOpen)
				{
					DrawFields(field.Fields, name + ".", base + field.Offset, view, nullptr);
					Nyx::UI::TreeRowEnd();
				}
				ImGui::PopID();
				continue;
			}
			DrawField(field, name, base + field.Offset, view);
		}
	}

	void MaterialParametersPanel::DrawField(const Nyx::Engine::ShaderField& field, const std::string& name, uint32_t offset, const Nyx::MaterialView& view)
	{
		using Nyx::Engine::EShaderFieldKind;
		using Nyx::Engine::EShaderScalarType;

		const std::string* displayName = TextArgument(field, "DisplayName");
		const std::string label = displayName ? *displayName : field.Name;
		const std::string editLabel = "Edit " + label;
		const bool bTexture = field.Kind == EShaderFieldKind::Texture;
		const std::string* set = Document.GetAsset().FindValue(name);
		const std::string defaultText = Nyx::Engine::ReadMaterialValue(field, view.Defaults, offset);
		const std::string current = set ? *set : defaultText;
		const bool bDiffers = set && (bTexture ? !set->empty() : !SameValue(*set, defaultText));

		ImGui::PushID(name.c_str());
		Nyx::UI::BeginPropertyRow(label.c_str());
		if (const std::string* tooltip = TextArgument(field, "Tooltip"); tooltip && ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("%s", tooltip->c_str());
		}

		// Room for the reset button on every row, so that the values line up
		const ImGuiStyle& style = ImGui::GetStyle();
		const float resetWidth = ImGui::CalcTextSize("Reset").x + style.FramePadding.x * 2.0f;
		ImGui::PushItemWidth(std::max(ImGui::CalcItemWidth() - resetWidth - style.ItemSpacing.x, 40.0f));

		const auto preview = [&](std::string text)
		{
			PreviewLabel = editLabel;
			Document.PreviewValue(name, std::move(text));
		};
		const uint32_t count = field.Kind == EShaderFieldKind::Vector ? field.Columns : 1;
		const std::optional<std::vector<double>> numbers = bTexture ? std::nullopt : Nyx::Engine::ParseMaterialNumbers(current);
		const bool bNumbersFit = numbers && numbers->size() == count && count <= 4;
		const bool bSettable = (field.Kind == EShaderFieldKind::Scalar || field.Kind == EShaderFieldKind::Vector) && field.ArrayCount == 0;

		if (bTexture)
		{
			// Unset, it takes the slot its Default(...) names: white without one
			const std::string* fallback = TextArgument(field, "Default");
			const std::string hint = fallback ? *fallback : "White";
			DrawTextValue(name, set ? *set : std::string(), hint.c_str(), editLabel);
		}
		else if (!bSettable)
		{
			NYX_UI(ImGui::TextDisabled("%s (not set from a material file)", field.TypeName.c_str()));
		}
		else if (field.Kind == EShaderFieldKind::Scalar && field.Scalar == EShaderScalarType::Bool)
		{
			bool bValue = current == "true" || current == "1";
			if (NYX_UI(Nyx::UI::Checkbox("##Value", &bValue)))
			{
				Document.SetValue(name, bValue ? "true" : "false", editLabel);
			}
		}
		else if (!bNumbersFit)
		{
			DrawTextValue(name, current, nullptr, editLabel);
		}
		else if (field.Scalar == EShaderScalarType::Float)
		{
			float values[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
			for (uint32_t index = 0; index < count; ++index)
			{
				values[index] = static_cast<float>((*numbers)[index]);
			}
			const float speed = static_cast<float>(NumberArgument(field, "DragSpeed", 0).value_or(0.01));
			const std::optional<double> min = NumberArgument(field, "Range", 0);
			const std::optional<double> max = NumberArgument(field, "Range", 1);
			bool bChanged = false;
			if (count == 1 && min && max)
			{
				bChanged = NYX_UI(Nyx::UI::SliderFloat("##Value", &values[0], static_cast<float>(*min), static_cast<float>(*max)));
			}
			else if (count == 1)
			{
				bChanged = NYX_UI(Nyx::UI::DragFloat("##Value", &values[0], speed));
			}
			else if (count >= 3 && field.FindAttribute("Color") && !field.FindAttribute("HDR"))
			{
				// The swatch edits red, green and blue; a fourth value stays
				bChanged = NYX_UI(Nyx::UI::ColorSwatch("##Value", values)).bChanged;
			}
			else
			{
				bChanged = NYX_UI(Nyx::UI::DragVector("##Value", values, static_cast<int>(count), speed));
			}
			if (bChanged)
			{
				preview(Nyx::Engine::FormatMaterialNumbers(std::span<const float>(values, count)));
			}
		}
		else if (count == 1)
		{
			const auto* enumAttribute = field.FindAttribute("Enum");
			if (const std::string* items = TextArgument(field, "Enum"); items && enumAttribute)
			{
				// [Enum("A, B, C")]: the value is the index of the item chosen
				std::vector<std::string> names;
				size_t start = 0;
				while (start <= items->size())
				{
					const size_t comma = std::min(items->find(',', start), items->size());
					names.emplace_back(Trim(std::string_view(*items).substr(start, comma - start)));
					start = comma + 1;
				}
				std::vector<const char*> pointers;
				for (const std::string& item : names)
				{
					pointers.push_back(item.c_str());
				}
				int index = static_cast<int>((*numbers)[0]);
				if (NYX_UI(Nyx::UI::Combo("##Value", &index, pointers)))
				{
					Document.SetValue(name, std::to_string(index), editLabel);
				}
			}
			else if (field.Scalar == EShaderScalarType::Int)
			{
				int32_t value = static_cast<int32_t>((*numbers)[0]);
				if (NYX_UI(Nyx::UI::DragScalar("##Value", ImGuiDataType_S32, &value, static_cast<float>(NumberArgument(field, "DragSpeed", 0).value_or(0.2)))))
				{
					preview(std::to_string(value));
				}
			}
			else
			{
				uint32_t value = static_cast<uint32_t>(std::max(0.0, (*numbers)[0]));
				if (NYX_UI(Nyx::UI::DragScalar("##Value", ImGuiDataType_U32, &value, static_cast<float>(NumberArgument(field, "DragSpeed", 0).value_or(0.2)))))
				{
					preview(std::to_string(value));
				}
			}
		}
		else
		{
			// Whole-number vectors are typed
			DrawTextValue(name, current, nullptr, editLabel);
		}
		ImGui::PopItemWidth();

		ImGui::SameLine();
		if (bDiffers)
		{
			if (NYX_UI(Nyx::UI::Button("Reset")))
			{
				Document.SetValue(name, std::nullopt, "Reset " + label);
			}
			if (bTexture)
			{
				const std::string* fallback = TextArgument(field, "Default");
				ImGui::SetItemTooltip("Back to the shader's default: %s", fallback ? fallback->c_str() : "White");
			}
			else
			{
				ImGui::SetItemTooltip("Back to the shader's default: %s", defaultText.c_str());
			}
		}
		else
		{
			ImGui::Dummy(ImVec2(resetWidth, ImGui::GetFrameHeight()));
		}
		Nyx::UI::EndPropertyRow();
		ImGui::PopID();
	}

	void MaterialParametersPanel::DrawTextValue(const std::string& name, const std::string& current, const char* hint, const std::string& label)
	{
		// The text typed stays while the field is typed in; otherwise it shows the value
		std::string& text = TextFields[name];
		if (ActiveTextField != name)
		{
			text = current;
		}
		NYX_UI(Nyx::UI::InputText("##Value", &text, 0, hint));
		if (ImGui::IsItemActivated())
		{
			ActiveTextField = name;
		}
		if (ImGui::IsItemDeactivated())
		{
			if (ImGui::IsItemDeactivatedAfterEdit())
			{
				const std::string_view typed = Trim(text);
				Document.SetValue(name, typed.empty() ? std::nullopt : std::optional<std::string>(typed), label);
			}
			ActiveTextField.clear();
		}
	}

	void MaterialParametersPanel::DrawUnknownValues(const Nyx::MaterialView& view)
	{
		std::set<std::string> names;
		CollectNames(view.Layout->Fields, "", names);
		std::vector<std::pair<std::string, std::string>> unknown;
		for (auto& [name, value] : Document.GetAsset().GetValues())
		{
			if (!names.contains(name))
			{
				unknown.emplace_back(name, value);
			}
		}
		if (unknown.empty() || !NYX_UI(Nyx::UI::CollapsingHeader("Not in the Shader")))
		{
			return;
		}
		for (const auto& [name, value] : unknown)
		{
			ImGui::PushID(name.c_str());
			Nyx::UI::BeginPropertyRow(name.c_str());
			ImGui::AlignTextToFramePadding();
			NYX_UI(ImGui::TextDisabled("%s", value.c_str()));
			ImGui::SetItemTooltip("The shader has no parameter of this name; the file keeps the value, for a shader that has it again");
			ImGui::SameLine();
			if (NYX_UI(Nyx::UI::Button("Remove")))
			{
				Document.SetValue(name, std::nullopt, "Remove " + name);
			}
			Nyx::UI::EndPropertyRow();
			ImGui::PopID();
		}
	}
}
