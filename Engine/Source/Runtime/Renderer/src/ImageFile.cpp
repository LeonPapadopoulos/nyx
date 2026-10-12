#include "ImageFile.h"

#include <stb_image.h>

// The PNG writer GLFW ships in its deps folder. Static, so that its functions stay inside this
// file and can't clash with another copy (NyxPainterGpuTests has one).
#define STB_IMAGE_WRITE_STATIC
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#include <fstream>
#include <iterator>
#include <system_error>

namespace Nyx::Engine
{
	bool LoadRgbaImage(const std::filesystem::path& path, RgbaImage& outImage, std::string& outError)
	{
		// Read through a stream, so that paths with any characters work; stb_image takes the bytes.
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			outError = "Can't open " + path.string();
			return false;
		}
		const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(bytes.data(), static_cast<int>(bytes.size()), &width, &height, &channels, STBI_rgb_alpha);
		if (pixels == nullptr)
		{
			outError = "Can't read " + path.string() + " as an image: " + stbi_failure_reason();
			return false;
		}

		outImage.Width = static_cast<uint32_t>(width);
		outImage.Height = static_cast<uint32_t>(height);
		outImage.Pixels.assign(pixels, pixels + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
		stbi_image_free(pixels);
		return true;
	}

	bool SavePng(const std::filesystem::path& path, const RgbaImage& image, std::string& outError)
	{
		if (image.Width == 0 || image.Height == 0 || image.Pixels.size() != static_cast<size_t>(image.Width) * image.Height * 4)
		{
			outError = "The image to save has no pixels, or not as many as its size needs";
			return false;
		}

		std::error_code error;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), error);
		}
		std::ofstream file(path, std::ios::binary);
		if (!file)
		{
			outError = "Can't write " + path.string();
			return false;
		}

		// stb_image_write hands the encoded bytes over in pieces.
		const auto write = [](void* context, void* data, int size)
		{
			static_cast<std::ofstream*>(context)->write(static_cast<const char*>(data), size);
		};
		const int stride = static_cast<int>(image.Width) * 4;
		if (stbi_write_png_to_func(write, &file, static_cast<int>(image.Width), static_cast<int>(image.Height), 4, image.Pixels.data(), stride) == 0 || !file)
		{
			outError = "Couldn't write " + path.string();
			return false;
		}
		return true;
	}
}
