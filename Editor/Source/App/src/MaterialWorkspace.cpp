#include "MaterialWorkspace.h"

#include "AssetDatabase.h"
#include "ImGuiSource.h"
#include "Log.h"
#include "MaterialAsset.h"
#include "MeshRendererComponent.h"
#include "NameComponent.h"
#include "NewMaterial.h"
#include "EditorRenderer.h"
#include "SceneEditor.h"
#include "UITheme.h"
#include "UIWidgets.h"
#include "Workspaces.h"

#include "imgui.h"

namespace Nyx::Editor
{
	MaterialWorkspace::MaterialWorkspace(AssetDatabase& assets, Workspaces& layouts)
		: Assets(assets)
		, Layouts(layouts)
	{
		Parameters.OpenOther = [this](const std::string& path)
		{
			OpenMaterial(path);
		};
	}

	void MaterialWorkspace::OpenMaterial(const std::string& path)
	{
		Parameters.OpenMaterial(path);
		Preview.bOpen = true;
		Layouts.Request(EWorkspace::Materials);
	}

	void MaterialWorkspace::AskNewMaterial()
	{
		bAskNewMaterialName = true;
		NewMaterialName = "NewMaterial";
		NewMaterialError.clear();
	}

	void MaterialWorkspace::SetOpenFileAtLine(OpenFileAtLine open)
	{
		ShaderLog.SetOpenFileAtLine(open);
		Preview.SetOpenFileAtLine(open);
		OpenAtLine = std::move(open);
	}

	void MaterialWorkspace::Release()
	{
		Preview.ReleaseView();
	}

	bool MaterialWorkspace::HandleHotkeys()
	{
		return Parameters.HandleHotkeys();
	}

	void MaterialWorkspace::Draw(Nyx::IEditorRenderer& renderer, SceneEditor& editing)
	{
		UpdateChoices();

		// The entities using the open material
		Users.clear();
		const std::string& openMaterial = Parameters.GetDocument().GetPath();
		Nyx::Registry& world = editing.Scene.GetRegistry();
		world.Each<Nyx::MeshRendererComponent>([&](Nyx::Entity entity, const Nyx::MeshRendererComponent& meshRenderer)
			{
				if (openMaterial.empty() || meshRenderer.Material.Path != openMaterial)
				{
					return;
				}
				const std::string name = world.Has<Nyx::NameComponent>(entity) ? world.Get<Nyx::NameComponent>(entity).Name : std::string("Entity");
				Users.push_back({ name, [&editing, entity]()
					{
						editing.Scene.GetSelection() = entity;
					} });
			});
		Parameters.Draw(renderer, MaterialParametersPanel::Choices{
			.Shaders = &Shaders,
			.Materials = &Materials,
			.Instances = openMaterial.empty() ? nullptr : &FindInstancesOf(openMaterial),
			.Users = &Users,
		});

		{
			// The selected entity's mesh, for the preview's Selected Mesh
			Nyx::Mesh* selectedMesh = nullptr;
			const auto& selection = editing.Scene.GetSelection();
			const auto& scene = editing.Scene.GetRegistry();
			if (selection && scene.IsAlive(*selection) && scene.Has<Nyx::MeshRendererComponent>(*selection))
			{
				selectedMesh = scene.Get<Nyx::MeshRendererComponent>(*selection).MeshAsset;
			}
			// The material's shader's last compile, when it failed
			const Nyx::ShaderLogEntry* failedCompile = nullptr;
			if (Parameters.GetDocument().IsOpen())
			{
				const std::string shader = Parameters.GetDocument().GetAsset().GetShader();
				const Nyx::ShaderLogEntry* compile = shader.empty() ? nullptr : renderer.GetShaderLog().FindByFile((Nyx::Paths::GetAssetsDir() / shader).make_preferred());
				failedCompile = compile && !compile->bSucceeded ? compile : nullptr;
			}
			Preview.Draw(renderer, Parameters.GetDocument().GetPath(), selectedMesh, failedCompile);
		}
		ShaderLog.Draw(renderer.GetShaderLog());
	}

	void MaterialWorkspace::UpdateChoices()
	{
		if (ChoicesRevision == Assets.GetRevision())
		{
			return;
		}
		ChoicesRevision = Assets.GetRevision();

		Shaders.clear();
		for (const AssetEntry& shader : Assets.GetAllByType("Shader"))
		{
			Shaders.push_back(shader.RelativePath.generic_string());
		}
		Materials.clear();
		for (const AssetEntry& material : Assets.GetAllByType("Material"))
		{
			Materials.push_back(material.RelativePath.generic_string());
		}
	}

	void MaterialWorkspace::DrawNewMaterialPopup()
	{
		if (bAskNewMaterialName)
		{
			ImGui::OpenPopup("New Material");
			bAskNewMaterialName = false;
		}
		if (!Nyx::UI::BeginPopupModal("New Material", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			return;
		}
		NYX_UI(ImGui::TextUnformatted("A surface shader and a material on it, named"));
		if (ImGui::IsWindowAppearing())
		{
			ImGui::SetKeyboardFocusHere();
		}
		ImGui::SetNextItemWidth(280.0f);
		const bool bEntered = NYX_UI(Nyx::UI::InputText("##Name", &NewMaterialName, ImGuiInputTextFlags_EnterReturnsTrue));
		const std::string name = MakeMaterialName(NewMaterialName);
		if (!name.empty())
		{
			NYX_UI(ImGui::TextDisabled("Shaders/%s.slang and Materials/%s.nyxmat", name.c_str(), name.c_str()));
		}
		if (!NewMaterialError.empty())
		{
			const glm::vec4& error = Nyx::UI::GetTheme().StatusError;
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(error.r, error.g, error.b, error.a));
			NYX_UI(ImGui::TextUnformatted(NewMaterialError.c_str()));
			ImGui::PopStyleColor();
		}
		ImGui::Spacing();
		if ((NYX_UI(Nyx::UI::Button("Create")) || bEntered) && CreateNewMaterial())
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (NYX_UI(Nyx::UI::Button("Cancel")) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
		{
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	bool MaterialWorkspace::CreateNewMaterial()
	{
		const std::optional<NewMaterialFiles> files = CreateSurfaceMaterial(Nyx::Paths::GetAssetsDir(), GetSurfaceTemplateFile(), NewMaterialName, NewMaterialError);
		if (!files)
		{
			return false;
		}
		Assets.Rescan();
		OpenMaterial(files->MaterialPath);
		// The shader at the line to fill in
		OpenAtLine((Nyx::Paths::GetAssetsDir() / files->ShaderPath).make_preferred(), files->ShadeLine);
		LOG_INFO("Made {} and {}", files->ShaderPath, files->MaterialPath);
		return true;
	}

	void MaterialWorkspace::CreateMaterialInstance(const std::string& parentPath)
	{
		std::string error;
		const std::optional<NewMaterialFiles> files = Nyx::Editor::CreateMaterialInstance(Nyx::Paths::GetAssetsDir(), parentPath, error);
		if (!files)
		{
			LOG_ERROR("Couldn't make an instance of {}: {}", parentPath, error);
			return;
		}
		Assets.Rescan();
		InstancesFoundAt = -1.0;
		OpenMaterial(files->MaterialPath);
		LOG_INFO("Made {}", files->MaterialPath);
	}

	const std::vector<std::string>& MaterialWorkspace::FindInstancesOf(const std::string& path)
	{
		const double now = ImGui::GetTime();
		if (path == InstancesOf && InstancesFoundAt >= 0.0 && now - InstancesFoundAt < 1.0)
		{
			return FoundInstances;
		}
		InstancesOf = path;
		InstancesFoundAt = now;
		FoundInstances.clear();
		for (const AssetEntry& material : Assets.GetAllByType("Material"))
		{
			Nyx::MaterialAsset asset;
			std::string error;
			if (asset.Load(material.AbsolutePath, error) && asset.GetParent() == path)
			{
				FoundInstances.push_back(material.RelativePath.generic_string());
			}
		}
		return FoundInstances;
	}

	void MaterialWorkspace::CreateMaterialForShader(const std::string& shaderPath)
	{
		std::string error;
		const std::optional<NewMaterialFiles> files = Nyx::Editor::CreateMaterialForShader(Nyx::Paths::GetAssetsDir(), shaderPath, error);
		if (!files)
		{
			LOG_ERROR("Couldn't make a material for {}: {}", shaderPath, error);
			return;
		}
		Assets.Rescan();
		OpenMaterial(files->MaterialPath);
		LOG_INFO("Made {}", files->MaterialPath);
	}
}
