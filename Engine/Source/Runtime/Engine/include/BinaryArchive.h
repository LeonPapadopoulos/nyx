#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	class BinaryWriter
	{
	public:
		// Writes, flushes and closes a temporary file beside the destination before replacing it.
		// Returns false on an I/O failure, leaving an existing destination unchanged.
		bool SaveToFile(const std::filesystem::path& path) const;

		void WriteBytes(const void* data, size_t size);

		void WriteUInt8(uint8_t value);
		void WriteUInt16(uint16_t value);
		void WriteUInt32(uint32_t value);
		void WriteUInt64(uint64_t value);
		void WriteInt32(int32_t value);
		void WriteFloat(float value);
		void WriteBool(bool value);
		void WriteString(const std::string& value);

		// Writes the block's size in bytes (u32, not counting itself), then its bytes. With the
		// size, a reader can skip the whole block, even if it doesn't understand what's inside.
		void WriteBlock(const BinaryWriter& block);

		const std::vector<std::byte>& GetBytes() const
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
		void LoadFromMemory(std::vector<std::byte> bytes);

		bool ReadBytes(void* outData, size_t size);
		bool SkipBytes(size_t size);

		bool ReadUInt8(uint8_t& outValue);
		bool ReadUInt16(uint16_t& outValue);
		bool ReadUInt32(uint32_t& outValue);
		bool ReadUInt64(uint64_t& outValue);
		bool ReadInt32(int32_t& outValue);
		bool ReadFloat(float& outValue);
		bool ReadBool(bool& outValue);
		bool ReadString(std::string& outValue);

		// Reads a block written by BinaryWriter::WriteBlock into its own reader. This reader
		// continues after the block, no matter how much of the block is read.
		bool ReadBlock(BinaryReader& outBlock);
		bool SkipBlock();

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
