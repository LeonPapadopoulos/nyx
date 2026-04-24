#pragma once
#include "VulkanContext.h"
#include "Texture.h"
#include "ResourceManager.h"

namespace Nyx
{
	class NYXENGINE_API CubemapTexture : public Nyx::Engine::Resource
	{
	public:
		explicit CubemapTexture(const std::string& id)
			: Resource(id)
		{
		}

		~CubemapTexture() override;

		void SetContext(VulkanContext& context);

		bool Upload(VulkanContext& context, const std::array<ImageData, 6>& faces);
		void Release();

		bool DoLoad() override;
		bool DoUnload() override;

		vk::Image GetImage() const;
		vk::ImageView GetImageView() const;
		vk::Sampler GetSampler() const;

		int GetWidth() const;
		int GetHeight() const;

	private:
		bool LoadFaceImageData(const std::filesystem::path& filePath, ImageData& outImageData);
		bool LoadCubemapFaces(std::array<ImageData, 6>& outFaces);

		void CreateCubemapImage(VulkanContext& context, const std::array<ImageData, 6>& faces);
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
	};
}