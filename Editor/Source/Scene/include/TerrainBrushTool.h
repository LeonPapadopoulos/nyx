#pragma once

#include "Entity.h"
#include "InspectorDrawContext.h"
#include "TerrainBrush.h"
#include "TransactionObjectRef.h"

#include "imgui.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace Nyx
{
	class IEditorRenderer;
}

namespace Nyx::Editor
{
	class SceneDocument;
	class TransactionSystem;

	// The editor's terrain brush. With an SDF terrain selected and the brush on (its section under
	// the terrain's components in the Details panel), a scene view paints the terrain where the mouse
	// meets it instead of picking and showing the gizmo: a ring where it would land, and a stroke
	// while the left button is held, with Shift the other of dig and heap. A stroke is one undo step,
	// which running games get when it ends.
	class TerrainBrushTool
	{
	public:
		// On, and the selection has an SDF terrain
		bool IsActive(const SceneDocument& scene) const;

		// Its section in the Details panel: on or off, and its settings
		void DrawDetails();

		// In a scene view, with its picture at `imageMin`, `imageSize` big. Returns whether it takes
		// the mouse there: while it is active over the picture, and while its stroke goes on.
		bool TickAndDraw(Nyx::IEditorRenderer& renderer, SceneDocument& scene, TransactionSystem& transactions, uint64_t sceneViewId,
			const ImVec2& imageMin, const ImVec2& imageSize, bool bImageHovered);

		// A stroke without a view, as TickAndDraw() makes one: begun on a terrain in a mode, taken to
		// points in the terrain's space, ended as one undo step (false if it made no edit)
		bool BeginStroke(SceneDocument& scene, Nyx::Entity terrain, Nyx::ESdfTerrainEditMode mode);
		void StrokeTo(SceneDocument& scene, const glm::vec3& localPoint);
		bool EndStroke(SceneDocument& scene, TransactionSystem& transactions);
		bool IsStroking() const
		{
			return Stroke.has_value();
		}

		bool bOn = false;
		Nyx::TerrainBrushSettings Settings;

	private:
		std::optional<Nyx::TerrainBrushStroke> Stroke;
		uint64_t StrokeViewId = 0;
		bool bStrokeEdited = false;
		// The terrain's edits before the stroke, recorded with them after as one change of the array
		ObjectRef StrokeTarget{};
		std::vector<std::byte> EditsBefore;
		// Its settings are the editor's, drawn with nothing to undo
		InspectorDrawContext SettingsContext;
	};
}
