#pragma once

#include "Renderer.h"
#include "SceneDocument.h"
#include "SceneViewCameraData.h"
#include "TransformComponent.h"
#include "ReflectedTransactionSystem.h"
#include "ReflectedDiffUtil.h"

#include "imgui.h"
#include <array>
#include <glm/glm.hpp>

namespace Nyx::Editor
{
	enum class ETransformGizmoAxis : uint8_t
	{
		None = 0,
		X,
		Y,
		Z,
		Center,
		PlaneXY,
		PlaneXZ,
		PlaneYZ
	};

	enum class EGizmoSpace : uint8_t
	{
		World = 0,
		Local
	};

	enum class EGizmoOperation : uint8_t
	{
		Translate = 0,
		Rotate,
		Scale
	};

	struct TransformGizmoState
	{
		ETransformGizmoAxis HoveredAxis = ETransformGizmoAxis::None;
		ETransformGizmoAxis ActiveAxis = ETransformGizmoAxis::None;

		bool bDragging = false;
		uint64_t ActiveSceneViewId = 0;

		EGizmoSpace Space = EGizmoSpace::Local;
		EGizmoOperation Operation = EGizmoOperation::Translate;

		Nyx::Engine::Entity DragEntity{};

		glm::vec3 DragStartEntityPosition{ 0.0f };
		glm::vec3 DragStartEntityScale{ 1.0f };
		glm::quat DragStartEntityRotation{ 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec3 DragStartGizmoOrigin{ 0.0f };

		glm::vec3 DragAxisDirectionWS{ 0.0f };
		glm::vec3 DragAxisDirectionLS{ 0.0f };

		glm::vec3 DragPlaneOriginWS{ 0.0f };
		glm::vec3 DragPlaneNormalWS{ 0.0f };
		glm::vec3 DragStartPlaneHitWS{ 0.0f };

		glm::vec3 DragStartRotateVectorWS{ 0.0f };
		glm::vec3 DragCurrentRotateVectorWS{ 0.0f };
		float DragAccumulatedRotationRadians = 0.0f;
		float DragPreviousRawRotationRadians = 0.0f;

		float DragStartAxisCoordinate = 1.0f;
		glm::vec2 DragStartPlaneCoordinates{ 1.0f, 1.0f };
		float DragStartUniformRadius = 1.0f;
	};

	class TransformGizmo
	{
	public:
		void TickHotkeys();

		// Returns true if the gizmo consumed the left-click / interaction.
		bool TickAndDraw(
			Nyx::IRenderer& renderer,
			Nyx::SceneDocument& scene,
			Nyx::Editor::ReflectedTransactionHistory& history,
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

		struct AxisScreenHandle
		{
			ETransformGizmoAxis Axis = ETransformGizmoAxis::None;
			glm::vec3 WorldPos{ 0.0f };
			ImVec2 ScreenPos{ 0.0f, 0.0f };
			bool bVisible = false;
		};

		struct PlaneScreenHandle
		{
			ETransformGizmoAxis Handle = ETransformGizmoAxis::None;
			std::array<glm::vec3, 4> WorldCorners{};
			std::array<ImVec2, 4> ScreenCorners{};
			bool bVisible = false;
		};

	private:
		static bool IsPlaneHandle(ETransformGizmoAxis handle);
		static void GetPlaneAxes(
			ETransformGizmoAxis handle,
			ETransformGizmoAxis& outAxisA,
			ETransformGizmoAxis& outAxisB);

		static bool PointInTriangle2D(
			const ImVec2& p,
			const ImVec2& a,
			const ImVec2& b,
			const ImVec2& c);

		static bool PointInQuad2D(
			const ImVec2& p,
			const ImVec2& a,
			const ImVec2& b,
			const ImVec2& c,
			const ImVec2& d);

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

		static glm::vec3 GetAxisBasisLS(ETransformGizmoAxis axis);

		static ImU32 GetAxisColor(ETransformGizmoAxis axis, bool bHighlighted);

		static void BuildRotationRingPoints(
			const glm::vec3& origin,
			const glm::vec3& axisNormal,
			float radius,
			glm::vec3* outPoints,
			int numPoints);

		static float DistancePointToPolylineSq(
			const ImVec2& p,
			const ImVec2* points,
			int numPoints,
			bool bClosed);

		bool TickTranslate(
			const Nyx::SceneViewCameraData& viewData,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			bool bImageHovered,
			ImDrawList* drawList);

		bool TickRotate(
			const Nyx::SceneViewCameraData& viewData,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			bool bImageHovered,
			ImDrawList* drawList);

		bool TickScale(
			const Nyx::SceneViewCameraData& viewData,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			bool bImageHovered,
			ImDrawList* drawList);

	private:
		struct Ray
		{
			glm::vec3 Origin{ 0.0f };
			glm::vec3 Direction{ 0.0f, 0.0f, -1.0f };
		};

		static Ray BuildMouseRay(
			const Nyx::SceneViewCameraData& viewData,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize,
			const ImVec2& mousePos);

		static bool IntersectRayPlane(
			const Ray& ray,
			const glm::vec3& planeOrigin,
			const glm::vec3& planeNormal,
			glm::vec3& outHitPoint);

		static glm::vec3 BuildAxisDragPlaneNormal(
			const glm::vec3& axisDirectionWS,
			const glm::vec3& cameraWorldPos,
			const glm::vec3& gizmoOrigin);

		static float SignedAngleAroundAxis(
			const glm::vec3& from,
			const glm::vec3& to,
			const glm::vec3& axis);

		void BeginTranslateDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::Entity entity,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

		void UpdateTranslateDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::TransformComponent& transform,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

		void BeginRotateDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::Entity entity,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

		void UpdateRotateDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::TransformComponent& transform,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

		void BeginScaleDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::Entity entity,
			const Nyx::Engine::TransformComponent& transform,
			const glm::vec3& gizmoOrigin,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

		void UpdateScaleDrag(
			const Nyx::SceneViewCameraData& viewData,
			Nyx::Engine::TransformComponent& transform,
			const ImVec2& imageScreenMin,
			const ImVec2& imageSize);

	private:
		TransformGizmoState State;

		std::optional<ReflectedDiffUtil> ActiveTransformDiff;
	};
}