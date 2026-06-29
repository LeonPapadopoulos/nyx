#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	class BinaryWriter
	{
	public:
		bool SaveToFile(const std::filesystem::path& path) const;

		void WriteBytes(const void* data, size_t size);

		void WriteUInt32(uint32_t value);
		void WriteInt32(int32_t value);
		void WriteFloat(float value);
		void WriteBool(bool value);
		void WriteString(const std::string& value);

		const std::vector<std::byte>& GetBuffer() const
		{
			return Buffer;
		}

	private:
		std::vector<std::byte> Buffer;
	};

	class BinaryReader
	{
	public:
		bool LoadFromFile(const std::filesystem::path& path);

		bool ReadBytes(void* outData, size_t size);

		bool ReadUInt32(uint32_t& outValue);
		bool ReadInt32(int32_t& outValue);
		bool ReadFloat(float& outValue);
		bool ReadBool(bool& outValue);
		bool ReadString(std::string& outValue);

		bool IsValid() const
		{
			return bValid;
		}

	private:
		std::vector<std::byte> Buffer;
		size_t Offset = 0;
		bool bValid = true;
	};
}