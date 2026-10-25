#pragma once

#include "SkyAtmosphere.h"

#include <glm/glm.hpp>
#include <imgui.h>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Nyx
{
	class ShaderPrograms;
	class VulkanBindless;
	class VulkanContext;

	// A view's sky tables and light on the GPU (VulkanSkyAtmosphere), made at the first frame it
	// draws the Atmosphere sky, again in part when what they were made from changes; released when
	// it no longer draws it, and with the view.
	struct SceneViewSkyTables
	{
		// An image the compute passes write: a storage view of each mip (a 2D array, the six faces
		// of a cube), one view of all of it in the bindless array
		struct Image
		{
			vk::raii::Image Handle{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView Sampled{ nullptr };
			std::vector<vk::raii::ImageView> Levels;
			uint32_t Width = 0;
			uint32_t Height = 0;
			uint32_t LevelCount = 0;
			uint32_t Layers = 0;
			uint32_t Slot = 0;
			bool bHasSlot = false;
			// An 8-bit picture of it for the editor (Previews only)
			VkDescriptorSet ImGuiTexture = VK_NULL_HANDLE;
		};

		Image Transmittance;
		Image MultipleScattering;
		Image SkyView;
		// The sky as a cube, with mips; its diffuse light; its specular light, a roughness a mip
		Image Cube;
		Image Irradiance;
		Image Specular;
		// The pictures of the transmittance, the multiple scattering, the sky view and the cube
		std::array<Image, 4> Previews;

		// The air's constants (SkyConstants in SkyAtmosphere.slang), written as the tables are made
		vk::raii::Buffer Constants{ nullptr };
		vk::raii::DeviceMemory ConstantsMemory{ nullptr };
		// A set a dispatch, all freed when the next ones are recorded
		vk::raii::DescriptorPool Pool{ nullptr };

		// What the tables were made from; whether they were made at all
		SceneViewSkyAtmosphere Built;
		bool bBuilt = false;
		// What this frame makes again (VulkanSkyAtmosphere::Prepare()), and whether it draws the
		// pictures, and how bright
		SkyAtmosphereChanges Pending;
		bool bPreviews = false;
		float PreviewExposure = 1.0f;
		// The pictures were asked for since the frame's views were prepared, and how bright
		// (IEditorRenderer::GetSceneViewSkyTablePictures())
		bool bPreviewsAsked = false;
		float AskedPreviewExposure = 1.0f;
		// How many times each was made, for tests
		uint32_t AirBuilds = 0;
		uint32_t SkyViewBuilds = 0;

		bool IsMade() const { return SkyView.bHasSlot; }
		bool HasWork() const { return Pending.bAir || Pending.bSkyView || bPreviews; }
	};

	// Which of a view's sky tables, for tests and the debug window
	enum class ESkyTable : uint8_t
	{
		Transmittance,
		MultipleScattering,
		SkyView,
		Cube,
		Irradiance,
		Specular
	};

	// The sky made by the air's scattering (Hillaire 2020; Docs/Design/atmosphere.md), per view, by
	// the compute shaders of SkyAtmosphere.slang, recorded into the frame before the view's scene
	// pass when something they depend on changed:
	//   1. the transmittance and multiple scattering tables, when the air's settings change;
	//   2. the sky view table round the camera, at its height, and from it the sky as a cube, its
	//      diffuse light and its specular light, when the air, the sun or the camera's height
	//      changes noticeably (GetSkyAtmosphereChanges()).
	// The view's sky pass reads the sky view and the transmittance per pixel (nyx.sky), its materials
	// the cube's light instead of the sky cube map's (nyx.environment), its fog the horizon's colour.
	class VulkanSkyAtmosphere
	{
	public:
		static constexpr uint32_t TransmittanceWidth = 256;
		static constexpr uint32_t TransmittanceHeight = 64;
		static constexpr uint32_t MultipleScatteringSize = 32;
		static constexpr uint32_t SkyViewWidth = 192;
		static constexpr uint32_t SkyViewHeight = 108;
		static constexpr uint32_t CubeSize = 64;
		static constexpr uint32_t CubeLevels = 7;
		static constexpr uint32_t IrradianceSize = 8;
		// The cube's mip the irradiance sums: 4 a side
		static constexpr uint32_t IrradianceSourceLevel = 4;
		static constexpr uint32_t SpecularLevels = 6;

		// Both must outlive it
		VulkanSkyAtmosphere(VulkanContext& context, ShaderPrograms& programs);

		void Initialize(VulkanBindless& bindless);
		void Shutdown();
		// Once, and again when SkyAtmosphere.slang changed; every view's tables are made again
		void CreatePipelines();

		// While no frame is in flight and before this frame's passes are recorded: the view's tables
		// made, or let go of when it draws no such sky (`wanted.bAtmosphere` false), and what of them
		// this frame makes again; with `bPreviews`, the debug window's pictures too, exposed by
		// `previewExposure`
		void Prepare(SceneViewSkyTables& tables, const SceneViewSkyAtmosphere& wanted, bool bPreviews, float previewExposure);
		// What Prepare() found must be made, outside a render pass; the images end in
		// ShaderReadOnlyOptimal layout for the view's passes
		void Record(SceneViewSkyTables& tables, vk::raii::CommandBuffer& cmd);
		void Release(SceneViewSkyTables& tables);
		// Every view's tables must be made again (the shaders changed)
		void Invalidate(SceneViewSkyTables& tables);

		// For tests: a table's mip as it is on the GPU, texel after texel (a cube's faces one after
		// the other); waits for the GPU
		std::vector<glm::vec4> ReadTable(SceneViewSkyTables& tables, ESkyTable table, uint32_t level = 0);
		// The debug window's picture of a table (0 transmittance, 1 multiple scattering, 2 sky view,
		// 3 the cube), and its size; none before it was drawn
		ImTextureID GetPreviewTextureId(SceneViewSkyTables& tables, uint32_t preview);
		glm::uvec2 GetPreviewSize(const SceneViewSkyTables& tables, uint32_t preview) const;

	private:
		// What a dispatch writes and reads, by binding of set 1
		struct Bindings
		{
			vk::ImageView Target = nullptr;
			vk::ImageView Previous = nullptr;
			vk::ImageView Preview = nullptr;
		};

		SceneViewSkyTables::Image MakeImage(uint32_t width, uint32_t height, uint32_t levels, uint32_t layers, vk::Format format);
		void ReleaseImage(SceneViewSkyTables::Image& image);
		void MakeTables(SceneViewSkyTables& tables);
		void WriteConstants(SceneViewSkyTables& tables);
		// The image from what it was (or nothing) to General, to be written whole; and back to
		// ShaderReadOnlyOptimal once written
		void BeginWrite(vk::raii::CommandBuffer& cmd, const SceneViewSkyTables::Image& image);
		void EndWrite(vk::raii::CommandBuffer& cmd, const SceneViewSkyTables::Image& image);
		void Dispatch(SceneViewSkyTables& tables, vk::raii::CommandBuffer& cmd, const std::string& entryPoint, const void* constants, const Bindings& bindings,
			uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);

		VulkanContext& Context;
		ShaderPrograms& Programs;
		VulkanBindless* Bindless = nullptr;

		vk::raii::DescriptorSetLayout SetLayout{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		std::map<std::string, vk::raii::Pipeline> Pipelines;
		// For the pictures in the editor
		vk::raii::Sampler PreviewSampler{ nullptr };
	};
}
