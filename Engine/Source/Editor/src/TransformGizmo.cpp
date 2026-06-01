#include "NyxPCH.h"
#include "TransformGizmo.h"

#include "TransformComponent.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Nyx::Editor
{
	namespace
	{
		constexpr float Pi = 3.14159265359f;
	}

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

		if (ndc.z < 0.0f || ndc.z > 1.0f)
		{
			return false;
		}

		const float x = (ndc.x * 0.5f + 0.5f) * imageSize.x;
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

	float TransformGizmo::DistancePointToPolylineSq(
		const ImVec2& p,
		const ImVec2* points,
		int numPoints,
		bool bClosed)
	{
		float bestDistSq = std::numeric_limits<float>::max();

		const int numSegments = bClosed ? numPoints : (numPoints - 1);
		for (int i = 0; i < numSegments; ++i)
		{
			const ImVec2& a = points[i];
			const ImVec2& b = points[(i + 1) % numPoints];
			bestDistSq = std::min(bestDistSq, DistancePointToSegmentSq(p, a, b));
		}

		return bestDistSq;
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

	void TransformGizmo::BuildRotationRingPoints(
		const glm::vec3& origin,
		const glm::vec3& axisNormal,
		float radius,
		glm::vec3* outPoints,
		int numPoints)
	{
		glm::vec3 tangent = glm::cross(axisNormal, glm::vec3(0.0f, 1.0f, 0.0f));
		if (glm::dot(tangent, tangent) < 1e-6f)
		{
			tangent = glm::cross(axisNormal, glm::vec3(1.0f, 0.0f, 0.0f));
		}
		tangent = glm::normalize(tangent);

		glm::vec3 bitangent = glm::normalize(glm::cross(axisNormal, tangent));

		for (int i = 0; i < numPoints; ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(numPoints);
			const float angle = t * Pi * 2.0f;

			outPoints[i] =
				origin +
				(std::cos(angle) * tangent + std::sin(angle) * bitangent) * radius;
		}
	}

	void TransformGizmo::TickHotkeys()
	{
		if (ImGui::IsKeyPressed(ImGuiKey_W, false))
		{
			State.Operation = EGizmoOperation::Translate;
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_E, false))
		{
			State.Operation = EGizmoOperation::Rotate;
		}
		else if (ImGui::IsKeyPressed(ImGuiKey_R, false))
		{
			State.Operation = EGizmoOperation::Scale;
		}
	}

	bool TransformGizmo::TickTranslate(
		const Nyx::SceneViewCameraData& viewData,
		const Nyx::Engine::TransformComponent& transform,
		const glm::vec3& gizmoOrigin,
		const ImVec2& imageScreenMin,
		const ImVec2& imageSize,
		bool bImageHovered,
		ImDrawList* drawList)
	{
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

		if (!State.bDragging)
		{
			State.HoveredAxis = ETransformGizmoAxis::None;

			if (bImageHovered)
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

		if (!State.bDragging &&
			bImageHovered &&
			State.HoveredAxis != ETransformGizmoAxis::None &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			State.bDragging = true;
			State.ActiveAxis = State.HoveredAxis;
			bConsumed = true;
		}

		if (State.bDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			State.bDragging = false;
			State.ActiveAxis = ETransformGizmoAxis::None;
			bConsumed = true;
		}

		for (const AxisScreenSegment& segment : segments)
		{
			if (!segment.bVisible)
			{
				continue;
			}

			const bool bHighlighted =
				(State.HoveredAxis == segment.Axis) ||
				(State.ActiveAxis == segment.Axis && State.bDragging);

			const float lineThickness = bHighlighted ? 4.0f : 2.5f;
			const ImU32 color = GetAxisColor(segment.Axis, bHighlighted);

			const ImVec2 start = segment.ScreenStart;
			const ImVec2 end = segment.ScreenEnd;

			const ImVec2 delta{ end.x - start.x, end.y - start.y };
			const float lenSq = delta.x * delta.x + delta.y * delta.y;
			if (lenSq <= 1e-6f)
			{
				continue;
			}

			const float len = std::sqrt(lenSq);
			const ImVec2 dir{ delta.x / len, delta.y / len };
			const ImVec2 perp{ -dir.y, dir.x };

			const float arrowLength = bHighlighted ? 18.0f : 14.0f;
			const float baseRadius = bHighlighted ? 5.0f : 4.0f;

			// Arrow geometry:
			// tip = end
			// base center sits a bit back along the axis
			const ImVec2 tip = end;
			const ImVec2 baseCenter{
				end.x - dir.x * arrowLength,
				end.y - dir.y * arrowLength
			};

			const ImVec2 left{
				baseCenter.x + perp.x * baseRadius,
				baseCenter.y + perp.y * baseRadius
			};

			const ImVec2 right{
				baseCenter.x - perp.x * baseRadius,
				baseCenter.y - perp.y * baseRadius
			};

			// Draw the shaft only up to the base circle
			drawList->AddLine(start, baseCenter, color, lineThickness);

			// Circle base of the arrowhead
			drawList->AddCircleFilled(baseCenter, baseRadius, color);

			// Pointed arrow tip
			drawList->AddTriangleFilled(left, right, tip, color);
		}

		return bConsumed || State.bDragging;
	}

	bool TransformGizmo::TickRotate(
		const Nyx::SceneViewCameraData& viewData,
		const Nyx::Engine::TransformComponent& transform,
		const glm::vec3& gizmoOrigin,
		const ImVec2& imageScreenMin,
		const ImVec2& imageSize,
		bool bImageHovered,
		ImDrawList* drawList)
	{
		const float distanceToCamera = glm::length(viewData.CameraWorldPos - gizmoOrigin);
		const float ringRadius = std::max(0.75f, distanceToCamera * 0.2f);

		struct RingData
		{
			ETransformGizmoAxis Axis = ETransformGizmoAxis::None;
			std::array<glm::vec3, 64> WorldPoints{};
			std::array<ImVec2, 64> ScreenPoints{};
			bool bVisible = false;
		};

		RingData rings[3]{};
		rings[0].Axis = ETransformGizmoAxis::X;
		rings[1].Axis = ETransformGizmoAxis::Y;
		rings[2].Axis = ETransformGizmoAxis::Z;

		// Build and project rings first
		for (RingData& ring : rings)
		{
			const glm::vec3 axisNormal = GetAxisDirection(ring.Axis, State.Space, transform);
			BuildRotationRingPoints(
				gizmoOrigin,
				axisNormal,
				ringRadius,
				ring.WorldPoints.data(),
				static_cast<int>(ring.WorldPoints.size())
			);

			ring.bVisible = true;

			for (size_t i = 0; i < ring.WorldPoints.size(); ++i)
			{
				ImVec2 projected{};
				if (!ProjectWorldToSceneImage(viewData, ring.WorldPoints[i], imageScreenMin, imageSize, projected))
				{
					ring.bVisible = false;
					break;
				}

				ring.ScreenPoints[i] = projected;
			}
		}

		// Hover test
		if (!State.bDragging)
		{
			State.HoveredAxis = ETransformGizmoAxis::None;

			if (bImageHovered)
			{
				const ImVec2 mouse = ImGui::GetMousePos();

				const float hoverThresholdPx = 8.0f;
				const float hoverThresholdSq = hoverThresholdPx * hoverThresholdPx;

				float bestDistSq = hoverThresholdSq;

				for (const RingData& ring : rings)
				{
					if (!ring.bVisible)
					{
						continue;
					}

					const float distSq = DistancePointToPolylineSq(
						mouse,
						ring.ScreenPoints.data(),
						static_cast<int>(ring.ScreenPoints.size()),
						true
					);

					if (distSq <= bestDistSq)
					{
						bestDistSq = distSq;
						State.HoveredAxis = ring.Axis;
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
			bConsumed = true;
		}

		// End interaction
		if (State.bDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			State.bDragging = false;
			State.ActiveAxis = ETransformGizmoAxis::None;
			bConsumed = true;
		}

		// Draw rings with front/back fade
		for (const RingData& ring : rings)
		{
			if (!ring.bVisible)
			{
				continue;
			}

			const bool bHighlighted =
				(State.HoveredAxis == ring.Axis) ||
				(State.ActiveAxis == ring.Axis && State.bDragging);

			const ImU32 baseColor = GetAxisColor(ring.Axis, bHighlighted);

			const float frontThickness = bHighlighted ? 4.0f : 2.5f;
			const float backThickness = bHighlighted ? 2.5f : 1.5f;

			// Make front/back fading depend on how edge-on the ring is to the camera.
			const glm::vec3 ringNormal = GetAxisDirection(ring.Axis, State.Space, transform);
			const glm::vec3 viewDirAtOrigin = glm::normalize(viewData.CameraWorldPos - gizmoOrigin);

			// 0 = ring plane faces camera, 1 = ring plane is edge-on
			const float edgeOnFactor = 1.0f - std::abs(glm::dot(ringNormal, viewDirAtOrigin));

			for (size_t i = 0; i < ring.ScreenPoints.size(); ++i)
			{
				const size_t next = (i + 1) % ring.ScreenPoints.size();

				const ImVec2& a = ring.ScreenPoints[i];
				const ImVec2& b = ring.ScreenPoints[next];

				const glm::vec3 worldA = ring.WorldPoints[i];
				const glm::vec3 worldB = ring.WorldPoints[next];
				const glm::vec3 midpoint = (worldA + worldB) * 0.5f;

				const glm::vec3 toMid = glm::normalize(midpoint - gizmoOrigin);
				const glm::vec3 toCamera = glm::normalize(viewData.CameraWorldPos - midpoint);

				// -1 = back side of ring, +1 = front side of ring
				const float facing = glm::dot(toMid, toCamera);

				// Map to 0..1
				const float facing01 = glm::clamp(0.5f + 0.5f * facing, 0.0f, 1.0f);

				// When face-on, keep the whole ring nearly opaque.
				// When edge-on, use stronger front/back distinction.
				const float alphaMin = bHighlighted ? 0.45f : 0.20f;
				const float alphaFromFacing = alphaMin + (1.0f - alphaMin) * facing01;
				const float alpha = 1.0f + (alphaFromFacing - 1.0f) * edgeOnFactor;

				// Same idea for thickness:
				const float thicknessFromFacing =
					backThickness + (frontThickness - backThickness) * facing01;
				const float thickness =
					frontThickness + (thicknessFromFacing - frontThickness) * edgeOnFactor;

				const float r = static_cast<float>((baseColor >> IM_COL32_R_SHIFT) & 0xFF) / 255.0f;
				const float g = static_cast<float>((baseColor >> IM_COL32_G_SHIFT) & 0xFF) / 255.0f;
				const float bl = static_cast<float>((baseColor >> IM_COL32_B_SHIFT) & 0xFF) / 255.0f;

				const ImU32 color = ImGui::GetColorU32(ImVec4(r, g, bl, alpha));

				drawList->AddLine(a, b, color, thickness);
			}
		}

		return bConsumed || State.bDragging;
	}

	bool TransformGizmo::TickScale(
		const Nyx::SceneViewCameraData& viewData,
		const Nyx::Engine::TransformComponent& transform,
		const glm::vec3& gizmoOrigin,
		const ImVec2& imageScreenMin,
		const ImVec2& imageSize,
		bool bImageHovered,
		ImDrawList* drawList)
	{
		const float distanceToCamera = glm::length(viewData.CameraWorldPos - gizmoOrigin);
		const float gizmoScale = std::max(0.5f, distanceToCamera * 0.15f);

		AxisScreenSegment segments[3]{};
		AxisScreenHandle handles[3]{};

		segments[0].Axis = ETransformGizmoAxis::X;
		segments[1].Axis = ETransformGizmoAxis::Y;
		segments[2].Axis = ETransformGizmoAxis::Z;

		handles[0].Axis = ETransformGizmoAxis::X;
		handles[1].Axis = ETransformGizmoAxis::Y;
		handles[2].Axis = ETransformGizmoAxis::Z;

		for (int i = 0; i < 3; ++i)
		{
			const glm::vec3 axisDir = GetAxisDirection(segments[i].Axis, State.Space, transform);

			segments[i].WorldStart = gizmoOrigin;
			segments[i].WorldEnd = gizmoOrigin + axisDir * gizmoScale * 0.85f;

			handles[i].WorldPos = gizmoOrigin + axisDir * gizmoScale;

			ImVec2 screenStart{};
			ImVec2 screenEnd{};
			ImVec2 screenHandle{};

			const bool bStartOk = ProjectWorldToSceneImage(viewData, segments[i].WorldStart, imageScreenMin, imageSize, screenStart);
			const bool bEndOk = ProjectWorldToSceneImage(viewData, segments[i].WorldEnd, imageScreenMin, imageSize, screenEnd);
			const bool bHandleOk = ProjectWorldToSceneImage(viewData, handles[i].WorldPos, imageScreenMin, imageSize, screenHandle);

			segments[i].bVisible = bStartOk && bEndOk;
			handles[i].bVisible = bHandleOk;

			segments[i].ScreenStart = screenStart;
			segments[i].ScreenEnd = screenEnd;
			handles[i].ScreenPos = screenHandle;
		}

		if (!State.bDragging)
		{
			State.HoveredAxis = ETransformGizmoAxis::None;

			if (bImageHovered)
			{
				const ImVec2 mouse = ImGui::GetMousePos();

				const float hoverThresholdPx = 10.0f;
				const float hoverThresholdSq = hoverThresholdPx * hoverThresholdPx;

				float bestDistSq = hoverThresholdSq;

				for (const AxisScreenHandle& handle : handles)
				{
					if (!handle.bVisible)
					{
						continue;
					}

					const float dx = mouse.x - handle.ScreenPos.x;
					const float dy = mouse.y - handle.ScreenPos.y;
					const float distSq = dx * dx + dy * dy;

					if (distSq <= bestDistSq)
					{
						bestDistSq = distSq;
						State.HoveredAxis = handle.Axis;
					}
				}
			}
		}

		bool bConsumed = false;

		if (!State.bDragging &&
			bImageHovered &&
			State.HoveredAxis != ETransformGizmoAxis::None &&
			ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			State.bDragging = true;
			State.ActiveAxis = State.HoveredAxis;
			bConsumed = true;
		}

		if (State.bDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
		{
			State.bDragging = false;
			State.ActiveAxis = ETransformGizmoAxis::None;
			bConsumed = true;
		}

		for (int i = 0; i < 3; ++i)
		{
			if (!segments[i].bVisible || !handles[i].bVisible)
			{
				continue;
			}

			const bool bHighlighted =
				(State.HoveredAxis == handles[i].Axis) ||
				(State.ActiveAxis == handles[i].Axis && State.bDragging);

			const ImU32 color = GetAxisColor(handles[i].Axis, bHighlighted);
			const float thickness = bHighlighted ? 4.0f : 2.0f;
			const float boxHalfSize = bHighlighted ? 6.0f : 5.0f;

			drawList->AddLine(segments[i].ScreenStart, segments[i].ScreenEnd, color, thickness);
			drawList->AddRectFilled(
				ImVec2(handles[i].ScreenPos.x - boxHalfSize, handles[i].ScreenPos.y - boxHalfSize),
				ImVec2(handles[i].ScreenPos.x + boxHalfSize, handles[i].ScreenPos.y + boxHalfSize),
				color
			);
		}

		return bConsumed || State.bDragging;
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

		ImDrawList* drawList = ImGui::GetWindowDrawList();
		const ImVec2 imageScreenMax{ imageScreenMin.x + imageSize.x, imageScreenMin.y + imageSize.y };

		drawList->PushClipRect(imageScreenMin, imageScreenMax, true);

		bool bConsumed = false;

		switch (State.Operation)
		{
		case EGizmoOperation::Translate:
			bConsumed = TickTranslate(viewData, transform, gizmoOrigin, imageScreenMin, imageSize, bImageHovered, drawList);
			break;

		case EGizmoOperation::Rotate:
			bConsumed = TickRotate(viewData, transform, gizmoOrigin, imageScreenMin, imageSize, bImageHovered, drawList);
			break;

		case EGizmoOperation::Scale:
			bConsumed = TickScale(viewData, transform, gizmoOrigin, imageScreenMin, imageSize, bImageHovered, drawList);
			break;

		default:
			break;
		}

		ImVec2 screenOrigin{};
		if (ProjectWorldToSceneImage(viewData, gizmoOrigin, imageScreenMin, imageSize, screenOrigin))
		{
			drawList->AddCircleFilled(screenOrigin, 5.0f, IM_COL32(230, 230, 230, 255));
		}

		drawList->PopClipRect();

		return bConsumed;
	}
}