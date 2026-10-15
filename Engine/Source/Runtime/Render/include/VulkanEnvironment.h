#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Nyx
{
	class VulkanContext;
}

namespace Nyx::Engine
{
	class ShaderPrograms;
	class VulkanBindless;

	// The light of the environment for physically based materials, made on the GPU from the sky
	// when it loads (Environment.slang):
	//   - the specular cube: each mip the sky as a surface of rising roughness reflects it, from 0
	//     at mip 0 to 1 at the last, prefiltered with GGX;
	//   - the irradiance: the sky as nine spherical harmonics, already convolved with the cosine,
	//     so that the sum of the coefficients times the basis at a normal is the diffuse light
	//     there (irradiance over pi);
	//   - the BRDF table for the split-sum approximation, made once.
	// All three are in the bindless texture array; the view constants carry them to shaders. Each
	// sky keeps a light of its own, e.g. the scene's and a material preview's studio, which views
	// choose between; the BRDF table is everyone's.
	class VulkanEnvironment
	{
	public:
		static constexpr uint32_t SourceSize = 512;
		static constexpr uint32_t SpecularSize = 256;
		static constexpr uint32_t SpecularLevels = 6;
		static constexpr uint32_t LookupSize = 128;
		// The source's mip the irradiance is summed over: 32 by 32 per face
		static constexpr uint32_t IrradianceLevel = 4;
		static constexpr uint32_t SpecularSamples = 512;
		static constexpr uint32_t LookupSamples = 1024;

		// Makes the BRDF table; waits for the GPU
		void Initialize(Nyx::VulkanContext& context, VulkanBindless& bindless, ShaderPrograms& programs);
		void Shutdown();

		// Makes the light of the sky in `skySlot` (a cube of the bindless array, read in
		// ShaderReadOnlyOptimal layout), again if it has one; waits for the GPU
		void Build(uint32_t skySlot);
		// Lets the light of the sky go, e.g. of a sky replaced
		void Forget(uint32_t skySlot);
		bool HasLight(uint32_t skySlot) const { return Lights.contains(skySlot); }

		// Environment.slang changed: the pipelines, the table and every light are made again
		void Rebuild();

		// The specular cube of the sky's light; 0 without one
		uint32_t GetSpecularSlot(uint32_t skySlot) const;
		uint32_t GetSpecularLevels() const { return SpecularLevels; }
		uint32_t GetLookupSlot() const { return Lookup.Slot; }
		// Nine RGB coefficients in the order of nyx/environment.slang's basis; zero without a light
		const std::array<glm::vec3, 9>& GetIrradiance(uint32_t skySlot) const;

		// For tests: one mip of the sky's specular cube, face after face, row after row
		std::vector<glm::vec4> ReadSpecularLevel(uint32_t skySlot, uint32_t level);
		// For tests: the BRDF table, (scale, bias) row after row; a row is one roughness
		std::vector<glm::vec2> ReadLookup();

	private:
		// An image the compute shaders write: storage views of each mip, and one view of all of
		// it for sampling
		struct Image
		{
			vk::raii::Image Handle{ nullptr };
			vk::raii::DeviceMemory Memory{ nullptr };
			vk::raii::ImageView Sampled{ nullptr };
			std::vector<vk::raii::ImageView> Levels;
			uint32_t Size = 0;
			uint32_t LevelCount = 0;
			uint32_t Layers = 0;
			uint32_t Slot = 0;
			bool bHasSlot = false;
		};

		// What a dispatch writes and reads, by binding of set 1
		struct Bindings
		{
			vk::ImageView Target = nullptr;
			vk::ImageView Previous = nullptr;
			vk::Buffer Partials = nullptr;
			vk::ImageView Lookup = nullptr;
		};

		// A sky's light: the source is only needed while it is made
		struct Light
		{
			Image Specular;
			std::array<glm::vec3, 9> Irradiance{};
		};

		void CreatePipelines();
		void BuildLookup();
		void BuildLight(uint32_t skySlot, Light& light);
		Image MakeImage(uint32_t size, uint32_t levels, uint32_t layers);
		void Release(Image& image);
		void Transition(vk::raii::CommandBuffer& commands, const Image& image, vk::ImageLayout from, vk::ImageLayout to);
		void Dispatch(vk::raii::CommandBuffer& commands, const std::string& entryPoint, const void* constants, const Bindings& bindings, uint32_t groupsX, uint32_t groupsY, uint32_t groupsZ);
		void RunCommands(const std::function<void(vk::raii::CommandBuffer&)>& record);
		std::vector<glm::vec4> ReadImage(Image& image, uint32_t level);

		Nyx::VulkanContext* Context = nullptr;
		VulkanBindless* Bindless = nullptr;
		ShaderPrograms* Programs = nullptr;

		vk::raii::DescriptorSetLayout SetLayout{ nullptr };
		vk::raii::PipelineLayout PipelineLayout{ nullptr };
		vk::raii::DescriptorPool Pool{ nullptr };
		std::map<std::string, vk::raii::Pipeline> Pipelines;

		Image Lookup;
		// By the sky's slot
		std::map<uint32_t, Light> Lights;
	};
}
