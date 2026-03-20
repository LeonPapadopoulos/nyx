#include "NyxRendererPCH.h"
#include "Texture.h"

namespace Nyx
{
	namespace Renderer
	{
		// Texture::~Texture()
		// {
		// 	Unload();
		// }

		// bool Texture::DoLoad()
		// {
		// 	ASSERT(false && "Not implemented yet.");
		// 	return false;
		// }

		// bool Texture::DoUnload()
		// {
		// 	if (IsLoaded())
		// 	{
		// 		vk::Device device = GetDevice();

		// 		device.destroySampler(Sampler);
		// 		device.destroyImageView(ImageView);
		// 		device.destroyImage(Image);
		// 		device.freeMemory(Memory);

		// 		Resource::Unload();
		// 	}

		// 	return true;
		// }

		// vk::Image Texture::GetImage() const
		// {
		// 	return Image;
		// }

		// vk::ImageView Texture::GetImageView() const
		// {
		// 	return ImageView;
		// }

		// vk::Sampler Texture::GetSampler() const
		// {
		// 	return Sampler;
		// }

		// unsigned char* Texture::LoadImageData(const std::string & filePath, int* width, int* height, int* channels)
		// {
		// 	ASSERT(false && "Not implemented yet.");

		// 	return nullptr;
		// }

		// void Texture::FreeImageData(unsigned char* data)
		// {
		// 	ASSERT(false && "Not implemented yet.");
		// }

		// void Texture::CreateVulkanImage(unsigned char* data, int width, int height, int channels)
		// {
		// 	ASSERT(false && "Not implemented yet.");
		// }

		// vk::Device Texture::GetDevice()
		// {
		// 	ASSERT(false && "Not implemented yet.");
		// 	return vk::Device(); // placeholder!!
		// }

	} // Renderer
} // Nyx

