#pragma once

#include "Renderer.h"

#include "ImageFile.h"
#include "PainterDrawOptions.h"
#include "ShaderLog.h"

#include <glm/glm.hpp>
#include <imgui.h>

#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace Nyx::UI
{
	class Painter;
}

namespace Nyx
{
	// A material as an editor shows it (IEditorRenderer::ViewMaterial()), valid until the next frame
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
		// As IRenderer::SetMaterialValues()
		virtual void SetMaterialValues(const std::string& path, const Nyx::MaterialAsset& asset) = 0;
	};

	// The renderer as the editor and the UI use it: the scene part (IRenderer), the editor's
	// overlays and interaction with the scene views, editing materials, and what ImGui shows
	class NYXENGINE_API IEditorRenderer : public IRenderer, public IMaterialSource
	{
	public:
		struct PickResult
		{
			bool bHasNewResult = false;
			std::optional<Nyx::Entity> HitEntity;
		};

		// Both bases have it; one renderer does it
		using IRenderer::SetMaterialValues;

		virtual void RequestPick(uint64_t sceneViewId, uint32_t pixelX, uint32_t pixelY) = 0;
		virtual PickResult ConsumeLastPickResult(uint64_t sceneViewId) = 0;

		virtual void SetSelectedEntity(std::optional<Nyx::Entity> entity) = 0;

		// The free camera of a view in EditorFreeCamera mode. The renderer only holds it; the editor
		// moves it (EditorCameraController). Get returns false for an unknown view.
		virtual void SetSceneViewEditorCameraTransform(uint64_t id, const glm::vec3& pos, const glm::vec3& rot) = 0;
		virtual bool GetSceneViewEditorCameraTransform(uint64_t id, glm::vec3& outPos, glm::vec3& outRot) const = 0;

		// Editor overlays are the grid, debug lines, picking and the selection outline. They are
		// off by default; the editor turns them on for its scene views.
		virtual void SetSceneViewShowEditorOverlays(uint64_t id, bool bShow) = 0;

		// The last compile of every shader that compiled since the start, the engine's and the
		// materials', with what the compiler said: for the Shader Log
		virtual const Nyx::ShaderLog& GetShaderLog() const = 0;

		// Called for every shader that compiled here and replaced its code, the engine's and the
		// materials': the editor sends it to the games it started
		using ShaderCompiledListener = std::function<void(const Nyx::ShaderCompileRequest& request, const Nyx::ShaderCompileResult& result)>;
		virtual void SetShaderCompiledListener(ShaderCompiledListener listener) = 0;

		// The scene view's picture, as ImGui::Image() takes it. Asking shows the view, so that a view
		// drawn when shown (IRenderer::SetSceneViewDrawnWhenShown()) is drawn next frame: ask in
		// every frame the picture is shown.
		virtual ImTextureID GetSceneViewTextureId(uint64_t id) const = 0;

		// The scene view's sky tables as pictures (the Atmosphere sky's: Docs/Design/atmosphere.md),
		// for a debug window: the transmittance as it is, the multiple scattering, the sky view and
		// the sky's cube unfolded, exposed by `exposure` (a multiple), 8-bit. Asking draws them in
		// the view's next frame: ask in every frame they are shown. None for a view without that sky,
		// or before they were first drawn.
		struct SkyTablePictures
		{
			std::array<ImTextureID, 4> Textures{};
			std::array<glm::uvec2, 4> Sizes{};
		};
		virtual SkyTablePictures GetSceneViewSkyTablePictures(uint64_t id, float exposure) = 0;

		// The scene view's picture blurred small, about 1/16 of its size (VulkanBlur), for glows
		// and glass. What ImGui::Image() and AddImage() take: it stays the same when the view
		// changes size, and is invalid before the view's first frame. Asking shows the view, as
		// GetSceneViewTextureId() does.
		virtual ImTextureID GetSceneViewBlurTextureId(uint64_t id) const = 0;
		virtual Extent2D GetSceneViewBlurExtent(uint64_t id) const = 0;
		// How long blurring all scene views took on the GPU, in milliseconds, as of the last frame
		// whose times are back; negative where the GPU can't time it
		virtual float GetBlurMilliseconds() const = 0;

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
	};

	NYXENGINE_API std::unique_ptr<IEditorRenderer> CreateRenderer();
}
