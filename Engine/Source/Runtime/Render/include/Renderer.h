#pragma once
#include "NyxEngineAPI.h"
#include "Entity.h"

#include "SceneViewTypes.h"
#include "SceneViewCameraData.h"
#include "Extent2D.h"
#include "MaterialAsset.h"
#include "ShaderCompileService.h"
#include "glm/glm.hpp"

#include <cstddef>
#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Nyx
{
	class DebugDraw;
	class Mesh;
	struct Material;
	class Registry;
}

namespace Nyx
{
	// The scene part of the renderer: the device and its frames, worlds and the scene views that
	// show them, meshes, materials and shaders. What a game needs; the editor uses the rest of the
	// renderer through IEditorRenderer (EditorRenderer.h), which has the UI's ImGui types.
	class NYXENGINE_API IRenderer
	{
	public:
		virtual ~IRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window) = 0;
		virtual void Shutdown() = 0;

		virtual void OnFramebufferResized() = 0;

		// Vsync presents each frame at the monitor's next refresh (FIFO); without it, the newest
		// frame is shown at each refresh (mailbox, where the GPU has it). Set before Initialize(),
		// or later, which recreates the swapchain after the frame.
		virtual void SetVSync(bool bVSync) = 0;
		virtual bool IsVSync() const = 0;

		// The graphics API it draws with, as shown to the user, e.g. "Vulkan"
		virtual const char* GetApiName() const = 0;

		virtual void DrawFrame(const std::function<void()>& buildUI) = 0;

		virtual void WaitIdle() = 0;

		// Whether the GPU device was lost, e.g. the driver reset the GPU after a fault. Nothing is
		// drawn after that: the renderer can't make a new device, so the window should close.
		virtual bool IsDeviceLost() const = 0;

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode) = 0;

		// How bright the view shows its scene, in stops: +1 twice as bright, -1 half. 0 by default.
		virtual void SetSceneViewExposure(uint64_t id, float stops) = 0;

		// The sky the view shows and is lit by, turned about the up axis by `rotationRadians`; the
		// scene's sky, unturned, by default. The studio's and black's light is made at their first
		// use (a few milliseconds).
		virtual void SetSceneViewSky(uint64_t id, ESceneViewSky sky, float rotationRadians) = 0;

		// What the view shows: the lit scene by default, or a debug view of its materials
		virtual void SetSceneViewDebug(uint64_t id, ESceneViewDebug view) = 0;

		virtual bool GetSceneViewCameraData(uint64_t sceneViewId, Nyx::SceneViewCameraData& outData) const = 0;

		// The renderer's world, which every scene view shows unless it has one of its own
		// (SetSceneViewWorld()); the selection (IEditorRenderer::SetSelectedEntity()) is in it.
		// Starts a new revision of the world and discards the selection and the pending picks of
		// the views showing it, even when the address is unchanged. Call after replacing the
		// contents of a registry too. The registry must outlive being set: it is read while frames
		// are drawn.
		virtual void SetWorld(const Nyx::Registry* world) = 0;

		// A world of the view's own, e.g. a material preview's: the view draws its objects, through
		// its camera and under its sun, and picks from it. Starts a new revision of the world as
		// SetWorld() does, so views showing it drop their pending picks. Views of another world
		// than the renderer's draw no selection outline. Null shows the renderer's world again.
		virtual void SetSceneViewWorld(uint64_t id, const Nyx::Registry* world) = 0;

		// Lines drawn over a world (DebugDraw), e.g. a game's forces and anchors: every view showing
		// the world draws them, unless it hides them (SetSceneViewShowDebugDrawing()). The drawing
		// must outlive being set; null takes it away.
		virtual void SetWorldDebugDraw(const Nyx::Registry* world, const Nyx::DebugDraw* drawing) = 0;
		// Whether the view draws its world's debug drawing; on for a new view
		virtual void SetSceneViewShowDebugDrawing(uint64_t id, bool bShow) = 0;

		virtual uint64_t CreateSceneView() = 0;
		virtual void DestroySceneView(uint64_t id) = 0;

		virtual void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) = 0;

		virtual Extent2D GetSceneViewExtent(uint64_t id) const = 0;
		virtual bool WasSceneViewRecreatedThisFrame(uint64_t id) const = 0;

		// The scene views there are, oldest first, e.g. for debug windows to choose from
		virtual std::vector<uint64_t> GetSceneViewIds() const = 0;

		// temporary demo-asset accessors, until meshes are assets
		virtual Nyx::Mesh* GetCubeMesh() = 0;
		// Of radius 0.5, like the cube's half size
		virtual Nyx::Mesh* GetSphereMesh() = 0;
		// A square of side 1 on the ground (y = 0), facing up
		virtual Nyx::Mesh* GetPlaneMesh() = 0;

		// The material in a .nyxmat file, by its path in the assets folder
		// ("Materials/Stone.nyxmat"), loaded at the first ask. Never null: one that can't be drawn
		// (yet) shows a magenta checker.
		virtual Nyx::Material* GetMaterial(const std::string& path) = 0;

		// Draws the material with these values, as if its file said them, in every view from
		// this frame on when called while the UI is built; the file stays as it is
		// (MaterialAsset::Save() writes it). Reading the file again, once it was saved, takes the
		// file's values.
		virtual void SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset) = 0;

		// Whether shaders are compiling, e.g. for a material's first look; tests wait for it
		virtual bool IsCompilingShaders() const = 0;

		// A game started by the editor: shaders come compiled from the editor
		// (ApplyCompiledShader()), and saving a shader compiles nothing here. A shader nothing
		// compiled yet still compiles here at its first use.
		virtual void FollowEditorShaders() = 0;
		// A compile made by the editor, taken in the next frame as if it had compiled here
		virtual void ApplyCompiledShader(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result) = 0;
	};
}
