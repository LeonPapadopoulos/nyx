#include "TerrainBrushTool.h"

#include "CameraProjection.h"
#include "EditorRenderer.h"
#include "ImGuiSource.h"
#include "ReflectedPropertyAccess.h"
#include "ReflectedPropertyDrawer.h"
#include "SceneDocument.h"
#include "SdfTerrain.h"
#include "TransactionObjectRefHelpers.h"
#include "TransactionSystem.h"
#include "TransactionTypes.h"
#include "UIWidgets.h"

#include <vector>

namespace Nyx::Editor
{
	namespace
	{
		// Where the edits are among the terrain's properties
		size_t EditsPropertyIndex()
		{
			const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>();
			for (size_t index = 0; index < type.PropertyCount; ++index)
			{
				if (std::string_view(type.Properties[index].Name) == "Edits")
				{
					return index;
				}
			}
			return type.PropertyCount;
		}

		// The ray through the mouse over a scene view's picture
		Nyx::Ray MouseRay(const Nyx::SceneViewCameraData& view, const ImVec2& imageMin, const ImVec2& imageSize, const ImVec2& mouse)
		{
			const float x = (mouse.x - imageMin.x) / imageSize.x * 2.0f - 1.0f;
			const float y = (mouse.y - imageMin.y) / imageSize.y * 2.0f - 1.0f;
			glm::vec4 nearPoint = view.InverseViewProjection * glm::vec4(x, y, Nyx::NearPlaneDepth, 1.0f);
			glm::vec4 farPoint = view.InverseViewProjection * glm::vec4(x, y, Nyx::FarPlaneDepth, 1.0f);
			nearPoint /= nearPoint.w;
			farPoint /= farPoint.w;
			return { glm::vec3(nearPoint), glm::normalize(glm::vec3(farPoint - nearPoint)) };
		}

		// Where a point of the world shows on the picture; none behind the camera
		std::optional<ImVec2> ToImage(const Nyx::SceneViewCameraData& view, const glm::vec3& point, const ImVec2& imageMin, const ImVec2& imageSize)
		{
			const glm::vec4 clip = view.ViewProjection * glm::vec4(point, 1.0f);
			if (!(clip.w > 1e-6f))
			{
				return std::nullopt;
			}
			const glm::vec3 ndc = glm::vec3(clip) / clip.w;
			return ImVec2(imageMin.x + (ndc.x * 0.5f + 0.5f) * imageSize.x, imageMin.y + (ndc.y * 0.5f + 0.5f) * imageSize.y);
		}
	}

	bool TerrainBrushTool::IsActive(const SceneDocument& scene) const
	{
		const std::optional<Nyx::Entity>& selection = scene.GetSelection();
		return bOn && selection && scene.GetRegistry().IsAlive(*selection) && scene.GetRegistry().Has<Nyx::SdfTerrainComponent>(*selection);
	}

	void TerrainBrushTool::DrawDetails()
	{
		ImGui::PushID("##TerrainBrush");
		if (NYX_UI(Nyx::UI::CollapsingHeader("Terrain Brush")))
		{
			NYX_UI(Nyx::UI::Checkbox("Paint in the scene views", &bOn));
			NYX_UI(ImGui::TextDisabled("Left mouse: a stroke, one undo step; Shift: dig and heap the other way"));
			DrawReflectedTypeTable(&Settings, Nyx::Reflection::GetTypeMetadata<Nyx::TerrainBrushSettings>(), SettingsContext);
		}
		ImGui::PopID();
	}

	bool TerrainBrushTool::TickAndDraw(Nyx::IEditorRenderer& renderer, SceneDocument& scene, TransactionSystem& transactions, uint64_t sceneViewId,
		const ImVec2& imageMin, const ImVec2& imageSize, bool bImageHovered)
	{
		if (!IsActive(scene))
		{
			EndStroke(scene, transactions);
			return false;
		}
		const bool bStrokeHere = Stroke && StrokeViewId == sceneViewId;
		if (Stroke && !ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			EndStroke(scene, transactions);
		}
		Nyx::SceneViewCameraData view{};
		if (!(bImageHovered || bStrokeHere) || !renderer.GetSceneViewCameraData(sceneViewId, view) || !(imageSize.x > 0.0f && imageSize.y > 0.0f))
		{
			return bStrokeHere;
		}

		// Where the mouse meets the selected terrain, and the ring there
		const Nyx::Entity terrain = *scene.GetSelection();
		const std::optional<Nyx::TerrainBrushHit> hit =
			Nyx::CastTerrainBrush(scene.GetRegistry(), MouseRay(view, imageMin, imageSize, ImGui::GetMousePos()), Settings.Reach, terrain);
		const bool bInverted = ImGui::GetIO().KeyShift;
		Nyx::ESdfTerrainEditMode mode = Settings.Mode;
		if (bInverted && mode != Nyx::ESdfTerrainEditMode::Paint)
		{
			mode = mode == Nyx::ESdfTerrainEditMode::Subtract ? Nyx::ESdfTerrainEditMode::Add : Nyx::ESdfTerrainEditMode::Subtract;
		}
		if (hit)
		{
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			drawList->PushClipRect(imageMin, ImVec2(imageMin.x + imageSize.x, imageMin.y + imageSize.y), true);
			const ImU32 color = mode == Nyx::ESdfTerrainEditMode::Add ? IM_COL32(120, 230, 120, 230)
								: mode == Nyx::ESdfTerrainEditMode::Paint ? IM_COL32(120, 180, 255, 230)
																		  : IM_COL32(255, 170, 80, 230);
			std::vector<ImVec2> ring;
			for (const glm::vec3& point : Nyx::GetTerrainBrushOutline(*hit, std::max(Settings.Radius, Nyx::TerrainBrushMinRadius)))
			{
				if (const std::optional<ImVec2> pixel = ToImage(view, point, imageMin, imageSize))
				{
					ring.push_back(*pixel);
				}
			}
			if (ring.size() > 2)
			{
				drawList->AddPolyline(ring.data(), int(ring.size()), color, ImDrawFlags_Closed, 2.0f);
			}
			drawList->PopClipRect();
		}

		if (!Stroke && bImageHovered && hit && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			BeginStroke(scene, terrain, mode);
			StrokeViewId = sceneViewId;
		}
		if (Stroke && StrokeViewId == sceneViewId && hit)
		{
			StrokeTo(scene, hit->LocalPoint);
		}
		return true;
	}

	bool TerrainBrushTool::BeginStroke(SceneDocument& scene, Nyx::Entity terrain, Nyx::ESdfTerrainEditMode mode)
	{
		Nyx::Registry& world = scene.GetRegistry();
		if (Stroke || !world.IsAlive(terrain) || !world.Has<Nyx::SdfTerrainComponent>(terrain))
		{
			return false;
		}
		Nyx::TerrainBrushSettings stroke = Settings;
		stroke.Mode = mode;
		Stroke.emplace(stroke, terrain);
		bStrokeEdited = false;
		StrokeTarget = MakeSceneEntityRef(scene, terrain);
		EditsBefore = SaveReflectedProperty(&world.Get<Nyx::SdfTerrainComponent>(terrain), Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>(),
			EditsPropertyIndex());
		return true;
	}

	void TerrainBrushTool::StrokeTo(SceneDocument& scene, const glm::vec3& localPoint)
	{
		Nyx::Registry& world = scene.GetRegistry();
		if (!Stroke || !world.IsAlive(Stroke->GetTerrain()) || !world.Has<Nyx::SdfTerrainComponent>(Stroke->GetTerrain()))
		{
			return;
		}
		Nyx::SdfTerrainComponent& terrain = world.Get<Nyx::SdfTerrainComponent>(Stroke->GetTerrain());
		for (const Nyx::SdfTerrainEdit& edit : Stroke->MoveTo(localPoint))
		{
			Nyx::AppendTerrainEdit(terrain, edit);
			bStrokeEdited = true;
		}
	}

	bool TerrainBrushTool::EndStroke(SceneDocument& scene, TransactionSystem& transactions)
	{
		if (!Stroke)
		{
			return false;
		}
		const Nyx::Entity terrain = Stroke->GetTerrain();
		Stroke.reset();
		StrokeViewId = 0;
		Nyx::Registry& world = scene.GetRegistry();
		if (!bStrokeEdited || !world.IsAlive(terrain) || !world.Has<Nyx::SdfTerrainComponent>(terrain))
		{
			return false;
		}
		const Nyx::Reflection::TypeMetadata& type = Nyx::Reflection::GetTypeMetadata<Nyx::SdfTerrainComponent>();
		Transaction transaction;
		transaction.Label = "Terrain Brush";
		transaction.Changes.push_back({ EChangeKind::ReplaceArray,
			ReplaceArrayChange{ .Target = StrokeTarget,
				.TypeMetadata = &type,
				.PropertyIndex = EditsPropertyIndex(),
				.Before = std::move(EditsBefore),
				.After = SaveReflectedProperty(&world.Get<Nyx::SdfTerrainComponent>(terrain), type, EditsPropertyIndex()) } });
		transactions.Push(std::move(transaction));
		EditsBefore.clear();
		return true;
	}
}
