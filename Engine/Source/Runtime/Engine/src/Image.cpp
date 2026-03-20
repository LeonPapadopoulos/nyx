//#include "Enginepch.h"
//#include "Image.h"
//#include "stb_image.h"
//
//namespace Engine
//{
//	void* Image::Decode(const void* buffer, uint64_t length, uint64_t& outWidth, uint64_t& outHeight)
//	{
//		int width = 0;
//		int height = 0;
//		int channels = 0;
//		uint8_t* data = nullptr;
//		uint64_t size = 0;
//
//		data = stbi_load_from_memory((const stbi_uc*)buffer, length, &width, &height, &channels, 4);
//		size = width * height * 4;
//
//		outWidth = width;
//		outHeight = height;
//
//		return data;
//	}
//
//} // Engine