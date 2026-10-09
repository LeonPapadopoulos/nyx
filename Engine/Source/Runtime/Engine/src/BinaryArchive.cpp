#include "BinaryArchive.h"

#include <cstring>
#include <fstream>
#include <utility>

namespace Nyx::Engine
{
	bool BinaryWriter::SaveToFile(const std::filesystem::path& path) const
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file)
		{
			return false;
		}

		if (!Buffer.empty())
		{
			file.write(reinterpret_cast<const char*>(Buffer.data()), static_cast<std::streamsize>(Buffer.size()));
		}

		return true;
	}

	void BinaryWriter::WriteBytes(const void* data, size_t size)
	{
		if (size == 0)
		{
			return;
		}

		const std::byte* bytes = static_cast<const std::byte*>(data);
		Buffer.insert(Buffer.end(), bytes, bytes + size);
	}

	void BinaryWriter::WriteUInt8(uint8_t value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteUInt16(uint16_t value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteUInt32(uint32_t value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteUInt64(uint64_t value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteInt32(int32_t value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteFloat(float value)
	{
		WriteBytes(&value, sizeof(value));
	}

	void BinaryWriter::WriteBool(bool value)
	{
		const uint8_t asByte = value ? 1u : 0u;
		WriteBytes(&asByte, sizeof(asByte));
	}

	void BinaryWriter::WriteString(const std::string& value)
	{
		WriteUInt32(static_cast<uint32_t>(value.size()));

		if (!value.empty())
		{
			WriteBytes(value.data(), value.size());
		}
	}

	void BinaryWriter::WriteBlock(const BinaryWriter& block)
	{
		WriteUInt32(static_cast<uint32_t>(block.Buffer.size()));
		WriteBytes(block.Buffer.data(), block.Buffer.size());
	}

	bool BinaryReader::LoadFromFile(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		file.seekg(0, std::ios::end);
		const std::streamsize fileSize = file.tellg();
		file.seekg(0, std::ios::beg);

		if (fileSize < 0)
		{
			return false;
		}

		Buffer.resize(static_cast<size_t>(fileSize));

		if (fileSize > 0)
		{
			file.read(reinterpret_cast<char*>(Buffer.data()), fileSize);
			if (!file)
			{
				return false;
			}
		}

		Offset = 0;
		bValid = true;
		return true;
	}

	void BinaryReader::LoadFromMemory(std::vector<std::byte> bytes)
	{
		Buffer = std::move(bytes);
		Offset = 0;
		bValid = true;
	}

	bool BinaryReader::ReadBytes(void* outData, size_t size)
	{
		if (size > Buffer.size() - Offset)
		{
			bValid = false;
			return false;
		}

		if (size > 0)
		{
			std::memcpy(outData, Buffer.data() + Offset, size);
		}

		Offset += size;
		return true;
	}

	bool BinaryReader::SkipBytes(size_t size)
	{
		if (size > Buffer.size() - Offset)
		{
			bValid = false;
			return false;
		}

		Offset += size;
		return true;
	}

	bool BinaryReader::ReadUInt8(uint8_t& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadUInt16(uint16_t& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadUInt32(uint32_t& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadUInt64(uint64_t& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadInt32(int32_t& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadFloat(float& outValue)
	{
		return ReadBytes(&outValue, sizeof(outValue));
	}

	bool BinaryReader::ReadBool(bool& outValue)
	{
		uint8_t asByte = 0;
		if (!ReadBytes(&asByte, sizeof(asByte)))
		{
			return false;
		}

		outValue = (asByte != 0);
		return true;
	}

	bool BinaryReader::ReadString(std::string& outValue)
	{
		uint32_t length = 0;
		if (!ReadUInt32(length))
		{
			return false;
		}

		if (length > Buffer.size() - Offset)
		{
			bValid = false;
			return false;
		}

		outValue.resize(length);

		if (length > 0)
		{
			return ReadBytes(outValue.data(), length);
		}

		return true;
	}

	bool BinaryReader::ReadBlock(BinaryReader& outBlock)
	{
		uint32_t size = 0;
		if (!ReadUInt32(size) || size > Buffer.size() - Offset)
		{
			bValid = false;
			return false;
		}

		const auto blockBegin = Buffer.begin() + static_cast<std::ptrdiff_t>(Offset);
		outBlock.LoadFromMemory(std::vector<std::byte>(blockBegin, blockBegin + size));

		Offset += size;
		return true;
	}

	bool BinaryReader::SkipBlock()
	{
		uint32_t size = 0;
		return ReadUInt32(size) && SkipBytes(size);
	}
}