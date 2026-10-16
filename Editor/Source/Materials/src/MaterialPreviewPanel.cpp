#include "MaterialPreviewPanel.h"

#include "CameraComponent.h"
#include "DirectionalLightComponent.h"
#include "ImGuiSource.h"
#include "Mesh.h"
#include "MeshRendererComponent.h"
#include "Renderer.h"
#include "ShaderLog.h"
#include "TransformComponent.h"
#include "UITheme.h"
#include "UIWidgets.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace Nyx::Editor
{
	namespace
	{
		constexpr float FieldOfViewDegrees = 35.0f;
		// The camera's pitch stays short of the poles, where looking at the target turns over
		constexpr float MaxPitch = 1.45f;

		glm::vec3 DirectionFrom(float yaw, float pitch)
		{
			return glm::vec3(std::cos(pitch) * std::sin(yaw), std::sin(pitch), std::cos(pitch) * std::cos(yaw));
		}
	}

	Orbit OrbitAround(const glm::vec3& target, float distance, float yaw, float pitch)
	{
		Orbit orbit;
		orbit.Position = target + DirectionFrom(yaw, pitch) * distance;
		orbit.Rotation = glm::quatLookAt(glm::normalize(target - orbit.Position), glm::vec3(0.0f, 1.0f, 0.0f));
		return orbit;
	}

	MaterialPreviewPanel::MaterialPreviewPanel()
	{
		Subject = World.CreateEntity();
		World.Add<TransformComponent>(Subject, TransformComponent{});
		World.Add<MeshRendererComponent>(Subject, MeshRendererComponent{});

		Camera = World.CreateEntity();
		World.Add<TransformComponent>(Camera, TransformComponent{});
		CameraComponent camera;
		camera.FovYDegrees = FieldOfViewDegrees;
		camera.NearPlane = 0.05f;
		camera.FarPlane = 100.0f;
		World.Add<CameraComponent>(Camera, camera);

		Sun = World.CreateEntity();
		World.Add<TransformComponent>(Sun, TransformComponent{});
		World.Add<DirectionalLightComponent>(Sun, DirectionalLightComponent{});
	}

	void MaterialPreviewPanel::ReleaseView()
	{
		if (Renderer && ViewId != 0)
		{
			Renderer->DestroySceneView(ViewId);
		}
		ViewId = 0;
	}

	void MaterialPreviewPanel::Draw(Nyx::IRenderer& renderer, const std::string& materialPath, Nyx::Mesh* selectedMesh, const Nyx::ShaderLogEntry* failedCompile)
	{
		// The view costs a scene's drawing each frame: only while the window is open
		if (!bOpen)
		{
			ReleaseView();
			return;
		}
		if (ViewId == 0)
		{
			Renderer = &renderer;
			ViewId = renderer.CreateSceneView();
			renderer.SetSceneViewWorld(ViewId, &World);
			renderer.SetSceneViewCameraMode(ViewId, EViewportCameraMode::ScenePrimaryCamera);
			renderer.SetSceneViewShowEditorOverlays(ViewId, false);
		}
		UpdateWorld(renderer, materialPath, selectedMesh);
		renderer.SetSceneViewSky(ViewId, Sky, SkyRotation);
		renderer.SetSceneViewDebug(ViewId, DebugView);
		renderer.SetSceneViewExposure(ViewId, ExposureStops);

		ImGui::SetNextWindowSize(ImVec2(480.0f, 420.0f), ImGuiCond_FirstUseEver);
		if (!Nyx::UI::Begin("Preview", &bOpen))
		{
			ImGui::End();
			return;
		}
		DrawBar();

		if (materialPath.empty())
		{
			NYX_UI(ImGui::TextWrapped("No material is open. Double-click one in the asset browser to see it here."));
			ImGui::End();
			return;
		}

		// The shader's first error under the picture, while it doesn't compile
		const Nyx::ShaderDiagnostic* error = failedCompile ? failedCompile->FindFirstError() : nullptr;
		ImVec2 avail = ImGui::GetContentRegionAvail();
		if (error)
		{
			avail.y -= ImGui::GetTextLineHeightWithSpacing();
		}
		renderer.SetSceneViewSize(ViewId, static_cast<uint32_t>(std::max(avail.x, 1.0f)), static_cast<uint32_t>(std::max(avail.y, 1.0f)));
		if (!renderer.WasSceneViewRecreatedThisFrame(ViewId))
		{
			const ImVec2 imageMin = ImGui::GetCursorScreenPos();
			NYX_UI(ImGui::Image(renderer.GetSceneViewTextureId(ViewId), avail));
			// Over the picture, so that drags in it turn the camera, not the window
			ImGui::SetCursorScreenPos(imageMin);
			ImGui::InvisibleButton("##PreviewDrag", ImVec2(std::max(avail.x, 1.0f), std::max(avail.y, 1.0f)));
			ImGui::SetItemTooltip("Drag: turn the camera. Shift: the sun. Ctrl: the sky. Wheel: closer");
			HandleInput(ImGui::IsItemHovered(), ImGui::IsItemActive());
		}
		if (error)
		{
			DiagnosticLine(*error, OpenAtLine);
		}
		ImGui::End();
	}

	void MaterialPreviewPanel::UpdateWorld(Nyx::IRenderer& renderer, const std::string& materialPath, Nyx::Mesh* selectedMesh)
	{
		Nyx::Mesh* mesh = renderer.GetSphereMesh();
		switch (Shape)
		{
		case EShape::Cube:
			mesh = renderer.GetCubeMesh();
			break;
		case EShape::Plane:
			mesh = renderer.GetPlaneMesh();
			break;
		case EShape::SelectedMesh:
			mesh = selectedMesh ? selectedMesh : mesh;
			break;
		default:
			break;
		}

		// The shape fitted into a sphere of radius 0.5 around the origin, whatever its size
		const glm::vec3 boundsMin = mesh->GetLocalBoundsMin();
		const glm::vec3 boundsMax = mesh->GetLocalBoundsMax();
		const glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
		const float radius = std::max(glm::length(boundsMax - boundsMin) * 0.5f, 1e-4f);
		const float scale = 0.5f / radius;
		TransformComponent& subject = World.Get<TransformComponent>(Subject);
		subject.Scale = glm::vec3(scale);
		subject.Position = -center * scale;
		MeshRendererComponent& meshRenderer = World.Get<MeshRendererComponent>(Subject);
		meshRenderer.MeshAsset = mesh;
		meshRenderer.MaterialAsset = materialPath.empty() ? nullptr : renderer.GetMaterial(materialPath);

		// Far enough that the sphere of radius 0.5 fills about three quarters of the height
		const float fitDistance = 0.5f / std::sin(glm::radians(FieldOfViewDegrees) * 0.5f) / 0.75f;
		const Orbit camera = OrbitAround(glm::vec3(0.0f), fitDistance * CameraDistance, CameraYaw, CameraPitch);
		TransformComponent& cameraTransform = World.Get<TransformComponent>(Camera);
		cameraTransform.Position = camera.Position;
		cameraTransform.Rotation = camera.Rotation;

		// The sun shines towards the origin from its direction
		TransformComponent& sun = World.Get<TransformComponent>(Sun);
		sun.Rotation = OrbitAround(glm::vec3(0.0f), 1.0f, SunYaw, SunPitch).Rotation;
	}

	void MaterialPreviewPanel::DrawBar()
	{
		static constexpr std::array<const char*, 4> Shapes = { "Sphere", "Cube", "Plane", "Selected Mesh" };
		static constexpr std::array<const char*, 11> Views = { "Lit", "Base Color", "Normal", "Roughness", "Metallic", "Occlusion", "Emission", "Opacity", "UV Checker", "NaN / Inf", "Out of Range" };
		static constexpr std::array<const char*, 3> Skies = { "Scene Sky", "Studio", "Black" };

		ImGui::SetNextItemWidth(120.0f);
		int shape = static_cast<int>(Shape);
		if (NYX_UI(Nyx::UI::Combo("##Shape", &shape, Shapes)))
		{
			Shape = static_cast<EShape>(shape);
		}
		ImGui::SetItemTooltip("What the material is shown on; Selected Mesh: the mesh of the entity selected in the scene");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		int view = static_cast<int>(DebugView);
		if (NYX_UI(Nyx::UI::Combo("##View", &view, Views)))
		{
			DebugView = static_cast<ESceneViewDebug>(view);
		}
		ImGui::SetItemTooltip("Lit, or one output of the material as it is; NaN / Inf and Out of Range mark where the light before tonemapping is wrong");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(110.0f);
		int sky = static_cast<int>(Sky);
		if (NYX_UI(Nyx::UI::Combo("##Sky", &sky, Skies)))
		{
			Sky = static_cast<ESceneViewSky>(sky);
		}
		ImGui::SetItemTooltip("The sky the shape is lit by; Ctrl+drag in the picture turns it");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(std::max(ImGui::GetContentRegionAvail().x, 60.0f));
		NYX_UI(Nyx::UI::DragFloat("##Exposure", &ExposureStops, 0.02f, "%+.1f EV"));
		ImGui::SetItemTooltip("Exposure in stops: +1 twice as bright. Double-click to type.");
	}

	void MaterialPreviewPanel::HandleInput(bool bHovered, bool bActive)
	{
		const ImGuiIO& io = ImGui::GetIO();
		if (bActive && (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f))
		{
			constexpr float Speed = 0.01f;
			if (io.KeyShift)
			{
				SunYaw -= io.MouseDelta.x * Speed;
				SunPitch = std::clamp(SunPitch + io.MouseDelta.y * Speed, -MaxPitch, MaxPitch);
			}
			else if (io.KeyCtrl)
			{
				SkyRotation += io.MouseDelta.x * Speed;
			}
			else
			{
				CameraYaw -= io.MouseDelta.x * Speed;
				CameraPitch = std::clamp(CameraPitch + io.MouseDelta.y * Speed, -MaxPitch, MaxPitch);
			}
		}
		if (bHovered && io.MouseWheel != 0.0f)
		{
			CameraDistance = std::clamp(CameraDistance * std::pow(0.9f, io.MouseWheel), 0.4f, 4.0f);
		}
	}
}
