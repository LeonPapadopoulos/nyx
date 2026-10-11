#include "SceneViewPanel.h"

#include "ImGuiSource.h"
#include "Renderer.h"
#include "SceneEditor.h"

#include <algorithm>

namespace Nyx::Editor
{
	void SceneViewPanel::CreateView(Nyx::IRenderer& renderer)
	{
		UseView(renderer, renderer.CreateSceneView());
	}

	void SceneViewPanel::UseView(Nyx::IRenderer& renderer, uint64_t sceneViewId)
	{
		Renderer = &renderer;
		SceneViewId = sceneViewId;
	}

	void SceneViewPanel::DestroyView()
	{
		if (Renderer && SceneViewId != 0)
		{
			Renderer->DestroySceneView(SceneViewId);
		}
		SceneViewId = 0;
	}

	void SceneViewPanel::SetShowsGameView(bool bShowsGameView)
	{
		if (!Renderer || SceneViewId == 0)
		{
			return;
		}

		Renderer->SetSceneViewCameraMode(
			SceneViewId,
			bShowsGameView ? EViewportCameraMode::ScenePrimaryCamera : EViewportCameraMode::EditorFreeCamera);
		Renderer->SetSceneViewShowEditorOverlays(SceneViewId, !bShowsGameView);
	}

	void SceneViewPanel::ApplyPickResult(SceneEditor& editing)
	{
		if (!Renderer || SceneViewId == 0)
		{
			return;
		}

		const Nyx::IRenderer::PickResult result = Renderer->ConsumeLastPickResult(SceneViewId);
		if (!result.bHasNewResult)
		{
			return;
		}

		if (!result.HitEntity.has_value() || editing.Scene.GetRegistry().IsAlive(*result.HitEntity))
		{
			editing.Scene.GetSelection() = result.HitEntity;
		}
	}

	void SceneViewPanel::Draw(SceneEditor& editing)
	{
		if (!bOpen || !Renderer || SceneViewId == 0)
		{
			return;
		}

		if (!Nyx::UI::Begin(Title.c_str(), &bOpen))
		{
			ImGui::End();
			return;
		}

		const bool bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		const bool bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);

		Renderer->SetSceneViewHovered(SceneViewId, bHovered);
		Renderer->SetSceneViewFocused(SceneViewId, bFocused);

		const ImVec2 avail = ImGui::GetContentRegionAvail();
		Renderer->SetSceneViewSize(
			SceneViewId,
			static_cast<uint32_t>(avail.x),
			static_cast<uint32_t>(avail.y));

		if (!Renderer->WasSceneViewRecreatedThisFrame(SceneViewId))
		{
			NYX_UI(ImGui::Image(Renderer->GetSceneViewTextureId(SceneViewId), avail));

			const ImVec2 imageMin = ImGui::GetItemRectMin();
			const ImVec2 imageSize = ImGui::GetItemRectSize();
			const bool bImageHovered = ImGui::IsItemHovered();

			const bool bGizmoConsumedInteraction =
				editing.Gizmo.TickAndDraw(
					*Renderer,
					editing.Scene,
					editing.Transactions,
					SceneViewId,
					imageMin,
					imageSize,
					bImageHovered);

			if (!bGizmoConsumedInteraction &&
				bImageHovered &&
				ImGui::IsMouseClicked(ImGuiMouseButton_Left))
			{
				const ImVec2 mousePos = ImGui::GetMousePos();

				Nyx::SceneViewCameraData viewData{};
				if (Renderer->GetSceneViewCameraData(SceneViewId, viewData) &&
					imageSize.x > 0.0f &&
					imageSize.y > 0.0f)
				{
					uint32_t pickX = 0;
					uint32_t pickY = 0;

					MapImageMouseToPickPixel(
						imageMin,
						imageSize,
						mousePos,
						viewData.Extent,
						pickX,
						pickY);

					Renderer->RequestPick(SceneViewId, pickX, pickY);
				}
			}
		}
		else
		{
			NYX_UI(ImGui::Dummy(avail));
		}

		ImGui::End();
	}

	void SceneViewPanel::MapImageMouseToPickPixel(
		const ImVec2& imageMin,
		const ImVec2& imageSize,
		const ImVec2& mousePos,
		const Nyx::Extent2D& extent,
		uint32_t& outPickX,
		uint32_t& outPickY)
	{
		const float localMouseX = mousePos.x - imageMin.x;
		const float localMouseY = mousePos.y - imageMin.y;

		const float normalizedX = std::clamp(localMouseX / imageSize.x, 0.0f, 1.0f);
		const float normalizedY = std::clamp(localMouseY / imageSize.y, 0.0f, 1.0f);

		outPickX = std::min(
			static_cast<uint32_t>(normalizedX * static_cast<float>(extent.Width)),
			extent.Width > 0 ? extent.Width - 1 : 0u);

		outPickY = std::min(
			static_cast<uint32_t>(normalizedY * static_cast<float>(extent.Height)),
			extent.Height > 0 ? extent.Height - 1 : 0u);
	}
}
