//#pragma once
//
//namespace Engine
//{
//	enum class ImageFormat
//	{
//		None = 0,
//		RGBA,
//		RGBA32F
//	};
//
//	class Image
//	{
//	public:
//		Image(uint32_t width, uint32_t height, ImageFormat format, const void* data = nullptr);
//		~Image();
//
//		static void* Decode(const void* buffer, uint64_t length, uint64_t& outWidth, uint64_t& outHeight);
//	};
//
//} // Engine