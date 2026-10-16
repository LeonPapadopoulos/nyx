#include "SceneViewPanel.h"

#include "AssetDragDrop.h"
#include "ComponentEdits.h"
#include "EditorCameraController.h"
#include "EditorPreferences.h"
#include "ImGuiSource.h"
#include "EditorRenderer.h"
#include "SceneEditor.h"
#include "UIChrome.h"
#include "UITheme.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <utility>

namespace Nyx::Editor
{
	void SceneViewPanel::CreateView(Nyx::IEditorRenderer& renderer)
	{
		UseView(renderer, renderer.CreateSceneView());
	}

	void SceneViewPanel::UseView(Nyx::IEditorRenderer& renderer, uint64_t sceneViewId)
	{
		Renderer = &renderer;
		SceneViewId = sceneViewId;
		// The editor's overlays while the view shows the scene, not the game
		Renderer->SetSceneViewShowEditorOverlays(SceneViewId, !bShowingGameView);
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
		bShowingGameView = bShowsGameView;
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

		const Nyx::IEditorRenderer::PickResult result = Renderer->ConsumeLastPickResult(SceneViewId);
		if (!result.bHasNewResult)
		{
			return;
		}

		// A material dropped here goes to the entity under it, which is selected too
		const std::string dropped = std::exchange(DroppedMaterial, std::string());
		if (!dropped.empty() && result.HitEntity && editing.Scene.GetRegistry().IsAlive(*result.HitEntity))
		{
			AssignMaterial(editing.Scene, editing.Transactions, *result.HitEntity, dropped);
		}

		if (!result.HitEntity.has_value() || editing.Scene.GetRegistry().IsAlive(*result.HitEntity))
		{
			editing.Scene.GetSelection() = result.HitEntity;
		}
	}

	void SceneViewPanel::Draw(SceneEditor& editing, const EditorPreferences& preferences, EditorCameraController& cameraController)
	{
		if (!bOpen || !Renderer || SceneViewId == 0)
		{
			return;
		}

		// The picture fills the card, in from its edges only as far as the card's rounded corners
		// reach in along the diagonal, r(1 - 1/sqrt(2)), so that its square corners stay inside them
		const float padding = std::ceil(Nyx::UI::GetTheme().CardRounding * (1.0f - 0.70710678f));
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(padding, padding));
		const bool bVisible = Nyx::UI::Begin(Title.c_str(), &bOpen);
		ImGui::PopStyleVar();
		if (!bVisible)
		{
			ImGui::End();
			return;
		}

		EditorCameraController::ViewState viewState;
		viewState.SceneViewId = SceneViewId;
		viewState.bHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows);
		viewState.bFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
		viewState.bFreeCamera = !bShowingGameView;
		viewState.Window = ImGui::GetWindowViewport()->PlatformHandle;
		cameraController.ReportView(viewState);

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

			// A material dropped on the picture: the entity under the mouse gets it once the pick
			// is back (ApplyPickResult())
			if (!bShowingGameView && AssetTypes && ImGui::BeginDragDropTarget())
			{
				if (std::optional<std::string> material = AcceptAssetDrop(*AssetTypes, "Material"))
				{
					Nyx::SceneViewCameraData viewData{};
					if (Renderer->GetSceneViewCameraData(SceneViewId, viewData) && imageSize.x > 0.0f && imageSize.y > 0.0f)
					{
						uint32_t pickX = 0;
						uint32_t pickY = 0;
						MapImageMouseToPickPixel(imageMin, imageSize, ImGui::GetMousePos(), viewData.Extent, pickX, pickY);
						Renderer->RequestPick(SceneViewId, pickX, pickY);
						DroppedMaterial = std::move(*material);
					}
				}
				ImGui::EndDragDropTarget();
			}

			// Ambilight: the view's picture, blurred by the renderer each frame, glows from the
			// picture's edges onto the editor around it.
			const ImTextureID blurred = Renderer->GetSceneViewBlurTextureId(SceneViewId);
			if (preferences.bAmbilight && blurred != ImTextureID_Invalid)
			{
				const Nyx::UI::PainterRect picture{ { imageMin.x, imageMin.y }, { imageMin.x + imageSize.x, imageMin.y + imageSize.y } };
				Nyx::UI::AddWindowGlow(ImTextureRef(blurred), picture, preferences.AmbilightReach, preferences.AmbilightStrength);
			}

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
