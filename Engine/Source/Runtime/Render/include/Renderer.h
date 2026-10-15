#pragma once
#include "NyxEngineAPI.h"
#include "Entity.h"

#include "SceneViewTypes.h"
#include "SceneViewCameraData.h"
#include "Extent2D.h"
#include "PainterDrawOptions.h"
#include "ImageFile.h"
#include "MaterialAsset.h"
#include "ShaderCompileService.h"
#include "ShaderLog.h"
#include <imgui.h>
#include "glm/glm.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Nyx
{
	class Mesh;
	struct Material;
	class Registry;
}

namespace Nyx::UI
{
	class Painter;
}

namespace Nyx
{
	// A material as an editor shows it (IRenderer::ViewMaterial()), valid until the next frame
	struct MaterialView
	{
		// Its values: as its file has them, or as SetMaterialValues() set them since
		const Nyx::MaterialAsset* Asset = nullptr;
		// An instance's: the values it takes from its parents, merged; empty for a material on a
		// shader of its own
		const Nyx::MaterialAsset* Inherited = nullptr;
		// The shader it is drawn with, its own or its parents' ("Shaders/Stone.slang")
		std::string Shader;
		// Its shader's parameters, and the bytes of their defaults
		// (Nyx::ReadMaterialValue() reads one); null and empty until the shader first
		// compiled
		const Nyx::ShaderLayout* Layout = nullptr;
		std::span<const std::byte> Defaults;
		// Changes each time the file is read, e.g. after it was saved; 0 while it couldn't be
		uint64_t FileRevision = 0;
	};

	// Where an editor reads materials and sends its edits of their values: the renderer, or a
	// test's stand-in
	class NYXENGINE_API IMaterialSource
	{
	public:
		virtual ~IMaterialSource() = default;

		// A material's values and its shader's parameters, for an editor; loads it like
		// IRenderer::GetMaterial()
		virtual MaterialView ViewMaterial(const std::string& path) = 0;
		// Draws the material with these values, as if its file said them, in every view from
		// this frame on when called while the UI is built; the file stays as it is
		// (MaterialAsset::Save() writes it). Reading the file again, once it was saved, takes the
		// file's values.
		virtual void SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset) = 0;
	};

	enum class ESelectionOutlineMode : uint8_t
	{
		VisibleOnly = 0,
		FullSilhouette = 1,
		VisibleOccludeHatched = 2
	};

	class NYXENGINE_API IRenderer : public IMaterialSource
	{
	public:
		struct PickResult
		{
			bool bHasNewResult = false;
			std::optional<Nyx::Entity> HitEntity;
		};

	public:
		virtual ~IRenderer() = default;

		virtual void Initialize(const char* applicationName, GLFWwindow* window) = 0;
		virtual void Shutdown() = 0;

		virtual void BeginFrame() = 0;
		virtual void EndFrame() = 0;

		virtual void OnFramebufferResized() = 0;

		// Vsync presents each frame at the monitor's next refresh (FIFO); without it, the newest
		// frame is shown at each refresh (mailbox, where the GPU has it). Set before Initialize(),
		// or later, which recreates the swapchain after the frame.
		virtual void SetVSync(bool bVSync) = 0;
		virtual bool IsVSync() const = 0;

		// The graphics API it draws with, as shown to the user, e.g. "Vulkan"
		virtual const char* GetApiName() const = 0;

		virtual void DrawFrame(const std::function<void()>& buildUI) = 0;

		virtual void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY) = 0;
		virtual PickResult ConsumeLastPickResult(uint64_t sceneViewId) = 0;

		virtual void SetSelectedEntity(std::optional<Nyx::Entity> entity) = 0;
		virtual void SetSelectionOutlineMode(Nyx::ESelectionOutlineMode mode) = 0;

		virtual void WaitIdle() = 0;

		// Whether the GPU device was lost, e.g. the driver reset the GPU after a fault. Nothing is
		// drawn after that: the renderer can't make a new device, so the window should close.
		virtual bool IsDeviceLost() const = 0;

		virtual void SetSceneViewCameraMode(uint64_t id, EViewportCameraMode mode) = 0;
		// The free camera of a view in EditorFreeCamera mode. The renderer only holds it; the editor
		// moves it (EditorCameraController). Get returns false for an unknown view.
		virtual void SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot) = 0;
		virtual bool GetSceneViewEditorCameraTransform(uint64_t id, glm::vec3& outPos, glm::vec3& outRot) const = 0;

		// Editor overlays are the grid, debug lines, picking and the selection outline.
		// They are shown by default; a view showing the game turns them off.
		virtual void SetSceneViewShowEditorOverlays(uint64_t id, bool bShow) = 0;

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
		// (SetSceneViewWorld()); the selection (SetSelectedEntity()) is in it. Starts a new
		// revision of the world and discards the selection and the pending picks of the views
		// showing it, even when the address is unchanged. Call after replacing the contents of a
		// registry too. The registry must outlive being set: it is read while frames are drawn.
		virtual void SetWorld(const Nyx::Registry* world) = 0;

		// A world of the view's own, e.g. a material preview's: the view draws its objects, through
		// its camera and under its sun, and picks from it. Starts a new revision of the world as
		// SetWorld() does, so views showing it drop their pending picks. Views of another world
		// than the renderer's draw no selection outline. Null shows the renderer's world again.
		virtual void SetSceneViewWorld(uint64_t id, const Nyx::Registry* world) = 0;

		virtual uint64_t CreateSceneView() = 0;
		virtual void DestroySceneView(uint64_t id) = 0;

		virtual void SetSceneViewSize(uint64_t id, uint32_t width, uint32_t height) = 0;

		virtual ImTextureID GetSceneViewTextureId(uint64_t id) const = 0;
		virtual Extent2D GetSceneViewExtent(uint64_t id) const = 0;
		virtual bool WasSceneViewRecreatedThisFrame(uint64_t id) const = 0;

		// The scene view's picture blurred small, about 1/16 of its size (VulkanBlur), for glows
		// and glass. What ImGui::Image() and AddImage() take: it stays the same when the view
		// changes size, and is invalid before the view's first frame.
		virtual ImTextureID GetSceneViewBlurTextureId(uint64_t id) const = 0;
		virtual Extent2D GetSceneViewBlurExtent(uint64_t id) const = 0;
		// How long blurring all scene views took on the GPU, in milliseconds, as of the last frame
		// whose times are back; negative where the GPU can't time it
		virtual float GetBlurMilliseconds() const = 0;
		// The scene views there are, oldest first, e.g. for debug windows to choose from
		virtual std::vector<uint64_t> GetSceneViewIds() const = 0;

		// Painter canvases: images that 2D shapes recorded with a Painter are drawn into, to be
		// shown with ImGui::Image().
		virtual uint64_t CreatePainterCanvas() = 0;
		virtual void DestroyPainterCanvas(uint64_t id) = 0;

		// Hands over the canvas' shapes for this frame; the canvas takes the painter's size. Call
		// it while building the UI: the renderer draws canvases after the UI is built and before
		// ImGui renders, so an ImGui::Image() of the canvas shows this frame's shapes. A canvas
		// without a submission keeps its last image.
		virtual void SubmitPainterCanvas(uint64_t id, const Nyx::UI::Painter& painter, const Nyx::UI::PainterDrawOptions& options) = 0;

		// What ImGui::Image() takes to show the canvas. It stays the same when the canvas changes
		// size, and is invalid until the canvas is first drawn.
		virtual ImTextureID GetPainterCanvasTextureId(uint64_t id) const = 0;

		// The last drawn frame's grid, statistics and problems, as the GPU binned it and read it
		// back; none before the first draw
		virtual std::optional<Nyx::UI::PainterTiles> GetPainterCanvasTiles(uint64_t id) const = 0;

		// Painting inside ImGui windows (Nyx::UI::GetWindowPainter()) in the last drawn frame:
		// while the UI is built, that is the previous one.
		virtual const Nyx::UI::PainterWindowStats& GetWindowPainterStats() const = 0;

		// Images for the UI (ImGui::Image(), WindowPainter::AddImage()), made from RGBA pixels: 8
		// bits per channel, straight alpha, in the same sRGB-encoded values as ImGui's colours.
		// Returns 0 if the image can't be made. Destroying one takes effect after the GPU is done
		// with the frame, so it can be destroyed while the UI is built.
		virtual uint64_t CreateUIImage(const Nyx::RgbaImage& image) = 0;
		virtual void DestroyUIImage(uint64_t id) = 0;
		// What ImGui::Image() and AddImage() take; ImTextureID_Invalid for an unknown id
		virtual ImTextureID GetUIImageTextureId(uint64_t id) const = 0;

		// Saves a rect of the main window as a PNG file, as this frame shows it once rendered. The
		// rect is in ImGui's screen coordinates (e.g. a window's position and size); only the main
		// window can be read back, not ImGui's other OS windows. The file is written after the GPU
		// is done with the frame, and GetScreenshotMessage() says how it went.
		virtual void SaveScreenshot(const std::filesystem::path& path, ImVec2 min, ImVec2 max) = 0;
		// "Saving ...", "Saved <path>" or why it couldn't; empty before the first screenshot
		virtual const std::string& GetScreenshotMessage() const = 0;

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
		// Whether shaders are compiling, e.g. for a material's first look; tests wait for it
		virtual bool IsCompilingShaders() const = 0;

		// The last compile of every shader that compiled since the start, the engine's and the
		// materials', with what the compiler said: for the Shader Log
		virtual const Nyx::ShaderLog& GetShaderLog() const = 0;

		// Called for every shader that compiled here and replaced its code, the engine's and the
		// materials': the editor sends it to the games it started
		using ShaderCompiledListener = std::function<void(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result)>;
		virtual void SetShaderCompiledListener(ShaderCompiledListener listener) = 0;

		// A game started by the editor: shaders come compiled from the editor
		// (ApplyCompiledShader()), and saving a shader compiles nothing here. A shader nothing
		// compiled yet still compiles here at its first use.
		virtual void FollowEditorShaders() = 0;
		// A compile made by the editor, taken in the next frame as if it had compiled here
		virtual void ApplyCompiledShader(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result) = 0;
	};

	NYXENGINE_API std::unique_ptr<IRenderer> CreateRenderer();
}
