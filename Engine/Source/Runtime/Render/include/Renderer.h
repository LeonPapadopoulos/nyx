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
	class FixedStepClock;
	class IParticleVelocityField;
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

		// Multisample anti-aliasing of the scene views (Docs/Design/anti-aliasing.md): how many
		// samples each pixel of their scene has as it is drawn, which smooths the edges of what is
		// in it. 1 (none), 2, 4 or 8, the same for every view; DefaultMsaaSamples (4) unless set.
		// More than the device can do is the most it can. Set before Initialize(), or later, which
		// makes the views' targets and the scene's pipelines again before the next frame is drawn.
		virtual void SetMsaaSamples(uint32_t samples) = 0;
		// The samples a pixel the views draw with: as set, as far as the device can; from the frame
		// after it was set
		virtual uint32_t GetMsaaSamples() const = 0;

		// The graphics API it draws with, as shown to the user, e.g. "Vulkan"
		virtual const char* GetApiName() const = 0;

		virtual void DrawFrame(const std::function<void()>& buildUI) = 0;

		virtual void WaitIdle() = 0;

		// Whether the GPU device was lost, e.g. the driver reset the GPU after a fault. Nothing is
		// drawn after that: the renderer can't make a new device, so the window should close.
		virtual bool IsDeviceLost() const = 0;

		// What the last frame whose GPU work is done took and drew: its CPU times, its GPU times by
		// timestamps, and every scene view's draws and passes summed (RenderFrameStats). A frame's
		// stats are complete at the start of the next one, after its fence. Kept in place: reading
		// them allocates nothing.
		virtual const RenderFrameStats& GetFrameStats() const = 0;
		// The same frame's of one scene view; null for an unknown view
		virtual const SceneViewFrameStats* GetSceneViewFrameStats(uint64_t id) const = 0;

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode) = 0;

		// How bright the view shows its scene, in stops: +1 twice as bright, -1 half. 0 by default.
		virtual void SetSceneViewExposure(uint64_t id, float stops) = 0;

		// The sky the view shows and is lit by, turned about the up axis by `rotationRadians`; the
		// scene's sky, unturned, by default. The studio's and black's light is made at their first
		// use (a few milliseconds).
		virtual void SetSceneViewSky(uint64_t id, ESceneViewSky sky, float rotationRadians) = 0;

		// What the view shows: the lit scene by default, or a debug view of its materials
		virtual void SetSceneViewDebug(uint64_t id, ESceneViewDebug view) = 0;

		// Every view culls what its camera can't see: entities by their bounds, those drawn
		// instanced cell by cell. This shows the bounds it culls by, or freezes its culling where
		// its camera is now (ESceneViewCullingDebug); off by default.
		virtual void SetSceneViewCullingDebug(uint64_t id, ESceneViewCullingDebug mode) = 0;

		// The primary directional light casts shadows in every view, from the cascades of its
		// shadow maps (DirectionalLightComponent). This shows which cascade a surface's shadow
		// comes from, and the cascades' shadow maps (ESceneViewShadowDebug); off by default.
		virtual void SetSceneViewShadowDebug(uint64_t id, ESceneViewShadowDebug mode) = 0;

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

		// Draws the world's entities that have a PreviousTransformComponent between their last two
		// steps, by how far the clock is into the next one (FixedStepClock::GetInterpolationFactor()),
		// read as each frame is drawn; while the clock is paused, where the last step left them.
		// Picking and the editor's overlays use the transforms as they are. Off for a world until
		// set, so the editor's own world is drawn as it is. The clock must outlive being set; null
		// turns it off.
		virtual void SetWorldInterpolation(const Nyx::Registry* world, const Nyx::FixedStepClock* clock) = 0;

		// The air that carries the world's particles (ParticleEmitterComponent), as the game knows
		// it, asked for each particle as they are moved, each frame; null, or until set: the world's
		// wind (EnvironmentComponent), the same everywhere. The field must outlive being set.
		virtual void SetWorldParticleVelocityField(const Nyx::Registry* world, const Nyx::IParticleVelocityField* field) = 0;

		virtual uint64_t CreateSceneView() = 0;
		virtual void DestroySceneView(uint64_t id) = 0;

		// A view drawn when shown culls, casts shadows and draws only in the frames it is shown in:
		// those after a frame that asked for its picture to show it (IEditorRenderer::
		// GetSceneViewTextureId() or GetSceneViewBlurTextureId(), or WasSceneViewRecreatedThisFrame()).
		// So a view nobody shows, e.g. in a window behind another's tab, costs nothing; shown again,
		// it shows the picture it drew last for a frame, then this frame's. Off for a new view, which
		// is drawn every frame, e.g. one read back offscreen.
		virtual void SetSceneViewDrawnWhenShown(uint64_t id, bool bWhenShown) = 0;

		virtual void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) = 0;

		virtual Extent2D GetSceneViewExtent(uint64_t id) const = 0;
		// Whether the view has no picture to show this frame: it was made or made again (a new
		// size, a new sample count) and not drawn since. Asking shows the view (see
		// SetSceneViewDrawnWhenShown()), so that it is drawn next frame.
		virtual bool WasSceneViewRecreatedThisFrame(uint64_t id) const = 0;

		// The scene views there are, oldest first, e.g. for debug windows to choose from; not the
		// renderer's own, which its pipelines' render passes are made from and which is never drawn
		virtual std::vector<uint64_t> GetSceneViewIds() const = 0;

		// temporary demo-asset accessors, until meshes are assets
		virtual Nyx::Mesh* GetCubeMesh() = 0;
		// Of radius 0.5, like the cube's half size
		virtual Nyx::Mesh* GetSphereMesh() = 0;
		// Of radius 0.3 and 1.8 tall, standing on its origin: the default character's
		// (CharacterComponent)
		virtual Nyx::Mesh* GetCapsuleMesh() = 0;
		// A square of side 1 on the ground (y = 0), facing up
		virtual Nyx::Mesh* GetPlaneMesh() = 0;
		// Two upright quads crossed, 0.6 wide and 1 tall, standing on the origin, with weights to sway
		// by (MakeCrossedQuads()): for small plants drawn with a swaying, two-sided material
		virtual Nyx::Mesh* GetCrossedQuadsMesh() = 0;

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
