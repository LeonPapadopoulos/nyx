#pragma once

#include "Entity.h"
#include "SceneViewTypes.h"
#include "ShaderLogPanel.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace Nyx
{
	class IEditorRenderer;
	class Mesh;
}

namespace Nyx
{
	struct ShaderLogEntry;
}

namespace Nyx::Editor
{
	// Where something orbiting a point is: `distance` from `target`, turned `yaw` (radians) about
	// the up axis from +Z and `pitch` above the horizon, and turned to look at the target (its
	// -Z towards it)
	struct Orbit
	{
		glm::vec3 Position{ 0.0f };
		glm::quat Rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
	};
	Orbit OrbitAround(const glm::vec3& target, float distance, float yaw, float pitch);

	// The "Preview" window: the material open in the Parameters window on a shape in a world of its
	// own (IEditorRenderer::SetSceneViewWorld()), with a camera, a sun and a sky. Its bar chooses the
	// shape (sphere, cube, plane, or the mesh of the entity selected in the scene), what it shows
	// (lit, or one output of the material: base colour, normals, roughness, ... and where light is
	// NaN or out of range), the sky (studio, the scene's, black) and the exposure.
	//
	// Dragging in the picture turns the camera around the shape, the wheel brings it closer;
	// with Shift the drag turns the sun, with Ctrl the sky. Its scene view exists while the window
	// is open. While the material's shader doesn't compile, the picture keeps the last good look
	// over a red line with the first error, which opens the file at the line when clicked.
	class MaterialPreviewPanel
	{
	public:
		enum class EShape : uint8_t
		{
			Sphere,
			Cube,
			Plane,
			// The mesh of the entity selected in the scene; the sphere without one
			SelectedMesh,
		};

		MaterialPreviewPanel();

		// Every frame: the window while open, showing `materialPath` (empty: no material open).
		// `selectedMesh`: the selected entity's mesh, null without one. `failedCompile`: the last
		// compile of the material's shader when it failed, null otherwise.
		void Draw(Nyx::IEditorRenderer& renderer, const std::string& materialPath, Nyx::Mesh* selectedMesh, const Nyx::ShaderLogEntry* failedCompile = nullptr);

		void SetOpenFileAtLine(OpenFileAtLine open) { OpenAtLine = std::move(open); }

		// Lets the scene view go, e.g. before the renderer does
		void ReleaseView();

		bool bOpen = false;

		EShape Shape = EShape::Sphere;
		ESceneViewDebug DebugView = ESceneViewDebug::Lit;
		ESceneViewSky Sky = ESceneViewSky::Studio;
		float ExposureStops = 0.0f;
		float SkyRotation = 0.0f;
		// The camera's and the sun's turns, in radians; the camera's distance in shape sizes
		float CameraYaw = 0.6f;
		float CameraPitch = 0.3f;
		float CameraDistance = 1.0f;
		float SunYaw = 0.9f;
		float SunPitch = 0.8f;

	private:
		// The preview world's entities as the settings say: the shape fitted to a sphere of
		// radius 0.5 at the origin, the camera around it, the sun
		void UpdateWorld(Nyx::IEditorRenderer& renderer, const std::string& materialPath, Nyx::Mesh* selectedMesh);
		void DrawBar();
		// Drags and the wheel over the picture
		void HandleInput(bool bHovered, bool bActive);

		Nyx::Registry World;
		Nyx::Entity Subject;
		Nyx::Entity Camera;
		Nyx::Entity Sun;

		Nyx::IEditorRenderer* Renderer = nullptr;
		uint64_t ViewId = 0;
		OpenFileAtLine OpenAtLine;
	};
}
