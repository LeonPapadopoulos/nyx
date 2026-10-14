#pragma once

#include <vulkan/vulkan_raii.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	// Texture slots every program can count on, made by VulkanBindless
	enum class EBindlessTexture : uint32_t
	{
		White = 0,
		Black = 1,
		// (0.5, 0.5, 1): a normal map that changes nothing
		FlatNormal = 2,
		// A magenta checker, for a texture that is missing
		Missing = 3,
	};

	// FixedSamplers in nyx/bindless.slang, in its order
	enum class EFixedSampler : uint32_t
	{
		LinearWrap = 0,
		LinearClamp = 1,
		PointClamp = 2,
		Anisotropic = 3,
	};

	// The global descriptor set, set 0 of every scene pipeline (Design/shader-materials.md,
	// "Binding"): binding 0 one array of every sampled image, 2D and cube alike, which a shader
	// indexes; binding 1 the fixed samplers; binding 2 the material buffer, every material's
	// values at its own offset. Bound once per pass; a texture is an index, a material an offset.
	class VulkanBindless
	{
	public:
		static constexpr uint32_t MaxTextures = 4096;
		static constexpr uint32_t FixedSamplerCount = 4;
		static constexpr vk::DeviceSize MaterialBufferSize = 64 * 1024;
		// Material offsets are multiples of this
		static constexpr uint32_t MaterialAlignment = 16;

		void Initialize(VulkanContext& context);
		void Shutdown();

		const vk::raii::DescriptorSetLayout& GetSetLayout() const { return SetLayout; }
		vk::DescriptorSet GetSet() const { return *Set; }

		// Puts an image view (ShaderReadOnlyOptimal, 2D or cube) into a free slot of the array and
		// returns its index. Throws when every slot is taken.
		uint32_t AddTexture(vk::ImageView view);
		// Frees the slot once the frames that could still read it are done (EndFrame())
		void RemoveTexture(uint32_t index);

		// Room for a material's values in the material buffer: its offset, or nothing when the
		// buffer is full. Materials aren't freed yet; they are few and made once.
		std::optional<uint32_t> AllocateMaterial(uint32_t size);
		// Writes a material's values at its offset. Written in place: a frame the GPU is still
		// drawing may read them half written, so they are only written while no frame is in
		// flight (at start for now; steering materials while drawing comes later).
		void WriteMaterial(uint32_t offset, std::span<const std::byte> bytes);

		// Once per frame, after it was submitted: slots removed long enough ago are free again
		void EndFrame();

	private:
		void CreateSamplers();
		void CreateDefaultTextures();
		void CreateMaterialBuffer();
		void CreateSet();

		struct OwnTexture
		{
			vk::raii::Image Image{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView View{ nullptr };
		};
		// A small 2D texture of its own, RGBA, 8 bits per channel, not sRGB
		OwnTexture CreateTexture(uint32_t width, uint32_t height, const std::vector<uint8_t>& pixels);

		VulkanContext* Context = nullptr;

		vk::raii::DescriptorSetLayout SetLayout{ nullptr };
		vk::raii::DescriptorPool Pool{ nullptr };
		vk::raii::DescriptorSet Set{ nullptr };

		std::vector<vk::raii::Sampler> Samplers;
		std::vector<OwnTexture> DefaultTextures;

		vk::raii::Buffer MaterialBuffer{ nullptr };
		vk::raii::DeviceMemory MaterialMemory{ nullptr };
		std::byte* MaterialBytes = nullptr;
		uint32_t MaterialEnd = 0;

		// Slots not handed out: the lowest first
		std::vector<uint32_t> FreeSlots;
		uint32_t NextSlot = 0;
		// Removed slots, with how many more frames must end before they are free
		struct PendingSlot
		{
			uint32_t Index = 0;
			uint32_t FramesLeft = 0;
		};
		std::vector<PendingSlot> PendingSlots;
	};
}
