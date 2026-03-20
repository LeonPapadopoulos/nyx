#pragma once
#include "NyxRendererAPI.h"
// #include "ResourceManager.h"
#include <vulkan/vulkan.hpp>

namespace Nyx
{
	namespace Renderer
	{
		NYXRENDERER_API class Texture
		{
			Texture();
		};

		// class Texture : public Resource
		// {
		// public:
		// 	explicit Texture(const std::string& id)
		// 		: Resource(id)
		// 	{
		// 	}

		// 	~Texture() override;

		// 	bool DoLoad() override;
		// 	bool DoUnload() override;

		// 	vk::Image GetImage() const;
		// 	vk::ImageView GetImageView() const;
		// 	vk::Sampler GetSampler() const;

		// private:
		// 	unsigned char* LoadImageData(const std::string& filePath, int* width, int* height, int* channels);
		// 	void FreeImageData(unsigned char* data);
		// 	void CreateVulkanImage(unsigned char* data, int width, int height, int channels);
		// 	vk::Device GetDevice();

		// private:
		// 	vk::Image Image;
		// 	vk::DeviceMemory Memory;
		// 	vk::DeviceSize Offset;
		// 	vk::ImageView ImageView;
		// 	vk::Sampler Sampler;

		// 	int Width = 0;
		// 	int Heigh = 0;
		// 	int channels = 0;
		// };

	} // Renderer
} // Nyx
