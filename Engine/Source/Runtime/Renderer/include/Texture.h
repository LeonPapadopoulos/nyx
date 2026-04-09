#pragma once

#include "NyxEngineAPI.h"
#include "ResourceManager.h"
#include "VulkanContext.h"

#include <vulkan/vulkan_raii.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Nyx
{
	struct ImageData
	{
		int Width = 0;
		int Height = 0;
		int Channels = 0; // actual source channels if you care
		std::vector<std::uint8_t> Pixels;
	};

	class NYXENGINE_API Texture : public Nyx::Engine::Resource
	{
	public:
		explicit Texture(const std::string& id)
			: Resource(id)
		{
		}

		~Texture() override;

		// Needed only because Resource::DoLoad() has no context parameter.
		void SetContext(VulkanContext& context);

		// Immediate upload path for already-available CPU image data.
		bool Upload(VulkanContext& context, const ImageData& imageData);
		void Release();

		bool DoLoad() override;
		bool DoUnload() override;

		vk::Image GetImage() const;
		vk::ImageView GetImageView() const;
		vk::Sampler GetSampler() const;

		int GetWidth() const;
		int GetHeight() const;
		int GetChannels() const;

	private:
		bool LoadImageData(const std::string& filePath, ImageData& outImageData);

		void CreateVulkanImage(VulkanContext& context, const ImageData& imageData);
		void CreateImageView(VulkanContext& context);
		void CreateSampler(VulkanContext& context);

	private:
		VulkanContext* BoundContext = nullptr;

		vk::Format Format = vk::Format::eR8G8B8A8Srgb;

		vk::raii::Image Image{ nullptr };
		vk::raii::DeviceMemory Memory{ nullptr };
		vk::raii::ImageView ImageView{ nullptr };
		vk::raii::Sampler Sampler{ nullptr };

		int Width = 0;
		int Height = 0;
		int Channels = 0;
	};
}