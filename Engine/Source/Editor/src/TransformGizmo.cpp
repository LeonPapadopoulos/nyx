#include "NyxPCH.h"
#include "TransformGizmo.h"

#include "TransformComponent.h"

#include <algorithm>
#include <cmath>

namespace Nyx::Editor
{
	bool TransformGizmo::ProjectWorldToSceneImage(
		const Nyx::SceneViewCameraData& viewData,
		const glm::vec3& worldPos,
		const ImVec2& imageScreenMin,
		const ImVec2& imageSize,
		ImVec2& outScreenPos)
	{
		const glm::vec4 clip = viewData.ViewProjection * glm::vec4(worldPos, 1.0f);

		if (std::abs(clip.w) < 1e-6f)
		{
			return false;
		}

		const glm::vec3 ndc = glm::vec3(clip) / clip.w;

		// Vulkan-style projection
		if (ndc.z < 0.0f || ndc.z > 1.0f)
		{
			return false;
		}

		const float x = (ndc.x * 0.5f + 0.5f) * imageSize.x;
		// camera projection is already vulkan-flipped, so let's not do that here
		//const float y = (1.0f - (ndc.y * 0.5f + 0.5f)) * imageSize.y;
		const float y = (ndc.y * 0.5f + 0.5f) * imageSize.y;

		outScreenPos = ImVec2(imageScreenMin.x + x, imageScreenMin.y + y);
		return true;
	}

	float TransformGizmo::DistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b)
	{
		const ImVec2 ab{ b.x - a.x, b.y - a.y };
		const ImVec2 ap{ p.x - a.x, p.y - a.y };

		const float abLenSq = ab.x * ab.x + ab.y * ab.y;
		if (abLenSq <= 1e-6f)
		{
			const float dx = p.x - a.x;
			const float dy = p.y - a.y;
			return dx * dx + dy * dy;
		}

		const float t = std::clamp((ap.x * ab.x + ap.y * ab.y) / abLenSq, 0.0f, 1.0f);
		const ImVec2 closest{ a.x + ab.x * t, a.y + ab.y * t };

		const float dx = p.x - closest.x;
		const float dy = p.y - closest.y;
		return dx * dx + dy * dy;
	}

	glm::vec3 TransformGizmo::GetAxisDirection(
		ETransformGizmoAxis axis,
		EGizmoSpace space,
		const Nyx::Engine::TransformComponent& transform)
	{
		glm::vec3 basis{ 1.0f, 0.0f, 0.0f };

		switch (axis)
		{
		case ETransformGizmoAxis::X: basis = glm::vec3(1.0f, 0.0f, 0.0f); break;
		case ETransformGizmoAxis::Y: basis = glm::vec3(0.0f, 1.0f, 0.0f); break;
		case ETransformGizmoAxis::Z: basis = glm::vec3(0.0f, 0.0f, 1.0f); break;
		default: return glm::vec3(0.0f);
		}

		if (space == EGizmoSpace::World)
		{
			return basis;
		}

		const glm::mat4 localToWorld = transform.ToMatrix();
		return glm::normalize(glm::vec3(localToWorld * glm::vec4(basis, 0.0f)));
	}

	ImU32 TransformGizmo::GetAxisColor(ETransformGizmoAxis axis, bool bHighlighted)
	{
		if (bHighlighted)
		{
			return IM_COL32(255, 255, 0, 255);
		}

		switch (axis)
		{
		case ETransformGizmoAxis::X: return IM_COL32(255, 80, 80, 255);
		case ETransformGizmoAxis::Y: return IM_COL32(80, 255, 80, 255);
		case ETransformGizmoAxis::Z: return IM_COL32(80, 160, 255, 255);
		default: return IM_COL32(255, 255, 255, 255);
		}
	}

	bool TransformGizmo::TickAndDraw(
		Nyx::IRenderer& renderer,
		Nyx::SceneDocument& scene,
		uint64_t sceneViewId,
		const ImVec2& imageScreenMin,
		const ImVec2& imageSize,
		bool bImageHovered)
	{
		auto& selection = scene.GetSelection();
		if (!selection.has_value())
		{
			State.HoveredAxis = ETransformGizmoAxis::None;
			return false;
		}

		auto& world = scene.GetRegistry();
		const Nyx::Engine::Entity entity = selection.value();

		if (!world.IsAlive(entity) || !world.Has<Nyx::Engine::TransformComponent>(entity))
		{
			State.HoveredAxis = ETransformGizmoAxis::None;
			return false;
		}

		Nyx::SceneViewCameraData viewData{};
		if (!renderer.GetSceneViewCameraData(sceneViewId, viewData))
		{
			return false;
		}

		auto& transform = world.Get<Nyx::Engine::TransformComponent>(entity);

		const glm::mat4 localToWorld = transform.ToMatrix();
		const glm::vec3 gizmoOrigin = glm::vec3(localToWorld[3]);

		// Screen-size-ish world scale so the gizmo stays usable at different distances.
		const float distanceToCamera = glm::length(viewData.CameraWorldPos - gizmoOrigin);
		const float gizmoScale = std::max(0.5f, distanceToCamera * 0.15f);

		AxisScreenSegment segments[3]{};

		segments[0].Axis = ETransformGizmoAxis::X;
		segments[1].Axis = ETransformGizmoAxis::Y;
		segments[2].Axis = ETransformGizmoAxis::Z;

		for (AxisScreenSegment& segment : segments)
		{
			const glm::vec3 axisDir = GetAxisDirection(segment.Axis, State.Space, transform);

			segment.WorldStart = gizmoOrigin;
			segment.WorldEnd = gizmoOrigin + axisDir * gizmoScale;

			ImVec2 screenStart{};
			ImVec2 screenEnd{};

			const bool bStartOk = ProjectWorldToSceneImage(viewData, segment.WorldStart, imageScreenMin, imageSize, screenStart);
			const bool bEndOk = ProjectWorldToSceneImage(viewData, segment.WorldEnd, imageScreenMin, imageSize, screenEnd);

			segment.bVisible = bStartOk && bEndOk;
			segment.ScreenStart = screenStart;
			segment.ScreenEnd = screenEnd;
		}

		// Hover test only when not dragging, or when dragging this same view
		if (!State.bDragging || State.ActiveSceneViewId == sceneViewId)
		{
			State.HoveredAxis = ETransformGizmoAxis::None;

			if (bImageHovered && !State.bDragging)
			{
				const ImVec2 mouse = ImGui::GetMousePos();

				const float hoverThresholdPx = 9.0f;
				const float hoverThresholdSq = hoverThresholdPx * hoverThresholdPx;

				float bestDistSq = hoverThresholdSq;

				for (const AxisScreenSegment& segment : segments)
				{
					if (!segment.bVisible)
					{
						continue;
					}

					const float distSq = DistancePointToSegmentSq(mouse, segment.ScreenStart, segment.ScreenEnd);
					if (distSq <= bestDistSq)
					{
						bestDistSq = distSq;
						State.HoveredAxis = segment.Axis;
					}
				}
			}
		}

		bool bConsumed = false;

		// Begin interaction
		if (!State.bDragging &&
			bImageHovered &&
			State.HoveredAxis != ETransformGizmoAxis::None &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			State.bDragging = true;
			State.ActiveAxis = State.HoveredAxis;
			State.ActiveSceneViewId = sceneViewId;

			bConsumed = true;
		}

		// End interaction
		if (State.bDragging &&
			State.ActiveSceneViewId == sceneViewId &&
			ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			State.bDragging = false;
			State.ActiveAxis = ETransformGizmoAxis::None;
			State.ActiveSceneViewId = 0;

			bConsumed = true;
		}

		// Draw overlay
		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 imageScreenMax{ imageScreenMin.x + imageSize.x, imageScreenMin.y + imageSize.y };

		drawList->PushClipRect(imageScreenMin, imageScreenMax, true);

		for (const AxisScreenSegment& segment : segments)
		{
			if (!segment.bVisible)
			{
				continue;
			}

			const bool bHighlighted =
				(State.HoveredAxis == segment.Axis) ||
				(State.ActiveAxis == segment.Axis && State.ActiveSceneViewId == sceneViewId);

			const float thickness = bHighlighted ? 4.0f : 2.5f;
			const ImU32 color = GetAxisColor(segment.Axis, bHighlighted);

			drawList->AddLine(segment.ScreenStart, segment.ScreenEnd, color, thickness);
			drawList->AddCircleFilled(segment.ScreenEnd, bHighlighted ? 6.0f : 4.0f, color);
		}

		ImVec2 screenOrigin{};
		if (ProjectWorldToSceneImage(viewData, gizmoOrigin, imageScreenMin, imageSize, screenOrigin))
		{
			drawList->AddCircleFilled(screenOrigin, 5.0f, IM_COL32(230, 230, 230, 255));
		}

		drawList->PopClipRect();

		return bConsumed || (State.bDragging && State.ActiveSceneViewId == sceneViewId);
	}
}