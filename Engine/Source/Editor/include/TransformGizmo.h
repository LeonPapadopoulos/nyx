#pragma once

#include "Renderer.h"
#include "SceneDocument.h"
#include "SceneViewCameraData.h"
#include "TransformComponent.h"

#include "imgui.h"

#include <glm/glm.hpp>

namespace Nyx::Editor
{
	enum class ETransformGizmoAxis : uint8_t
	{
		None = 0,
		X,
		Y,
		Z
	};

	enum class EGizmoSpace : uint8_t
	{
		World = 0,
		Local
	};

	struct TransformGizmoState
	{
		ETransformGizmoAxis HoveredAxis = ETransformGizmoAxis::None;
		ETransformGizmoAxis ActiveAxis = ETransformGizmoAxis::None;

		bool bDragging = false;
		uint64_t ActiveSceneViewId = 0;

		EGizmoSpace Space = EGizmoSpace::Local;
	};

	class TransformGizmo
	{
	public:
		// Returns true if the gizmo consumed the left-click / interaction.
		bool TickAndDraw(
			Nyx::IRenderer& renderer,
			Nyx::SceneDocument& scene,
			uint64_t sceneViewId,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			bool bImageHovered);

	private:
		struct AxisScreenSegment
		{
			ETransformGizmoAxis Axis = ETransformGizmoAxis::None;
			glm::vec3 WorldStart{ 0.0f };
			glm::vec3 WorldEnd{ 0.0f };
			ImVec2 ScreenStart{ 0.0f, 0.0f };
			ImVec2 ScreenEnd{ 0.0f, 0.0f };
			bool bVisible = false;
		};

	private:
		static bool ProjectWorldToSceneImage(
			const Nyx::SceneViewCameraData& viewData,
			const glm::vec3& worldPos,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			ImVec2& outScreenPos);

		static float DistancePointToSegmentSq(const ImVec2& p, const ImVec2& a, const ImVec2& b);

		static glm::vec3 GetAxisDirection(
			ETransformGizmoAxis axis,
			EGizmoSpace space,
			const Nyx::Engine::TransformComponent& transform);

		static ImU32 GetAxisColor(ETransformGizmoAxis axis, bool bHighlighted);

	private:
		TransformGizmoState State;
	};
}