#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx
{
	// An image in memory: 8 bits per channel, RGBA, row by row from the top left, straight alpha
	struct RgbaImage
	{
		uint32_t Width = 0;
		uint32_t Height = 0;
		std::vector<uint8_t> Pixels;
	};

	// Reads a PNG, JPEG, BMP or TGA file (stb_image) as RGBA. False, with what went wrong in
	// `outError`, if it can't.
	bool LoadRgbaImage(const std::filesystem::path& path, RgbaImage& outImage, std::string& outError);

	// Writes the image as a PNG file (stb_image_write), making its folder if needed. False, with
	// what went wrong in `outError`, if it can't.
	bool SavePng(const std::filesystem::path& path, const RgbaImage& image, std::string& outError);
}
