#include "BinaryArchive.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <utility>

#if defined(_WIN32)
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <Windows.h>
#else
	#error BinaryWriter::SaveToFile is not implemented for this platform yet.
#endif

namespace
{
	// Owns only the temporary file. The destination is never opened for writing or deleted.
	// Unless Commit succeeds, leaving this scope closes the file and attempts to remove it.
	class PendingArchiveFile
	{
	public:
		explicit PendingArchiveFile(const std::filesystem::path& directory)
		{
			static std::atomic<uint64_t> nextFileNumber{ 0 };

			// CREATE_NEW reserves the name, even when another thread or process is saving.
			for (int attempt = 0; attempt < 64; ++attempt)
			{
				const std::wstring name = L".nyx-save-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
					std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(nextFileNumber.fetch_add(1)) + L".tmp";
				std::filesystem::path candidate = directory / name;

				File = CreateFileW(candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (File != INVALID_HANDLE_VALUE)
				{
					Path = std::move(candidate);
					return;
				}

				const DWORD error = GetLastError();
				if (error != ERROR_FILE_EXISTS && error != ERROR_ALREADY_EXISTS)
				{
					return;
				}
			}
		}

		~PendingArchiveFile()
		{
			if (File != INVALID_HANDLE_VALUE)
			{
				CloseHandle(File);
			}

			if (!Path.empty())
			{
				DeleteFileW(Path.c_str());
			}
		}

		PendingArchiveFile(const PendingArchiveFile&) = delete;
		PendingArchiveFile& operator=(const PendingArchiveFile&) = delete;

		bool WriteAndClose(const std::vector<std::byte>& bytes)
		{
			if (File == INVALID_HANDLE_VALUE)
			{
				return false;
			}

			// Bounded writes also handle archives larger than WriteFile's DWORD size limit.
			constexpr size_t ChunkSize = 1024 * 1024;
			size_t offset = 0;
			while (offset < bytes.size())
			{
				const DWORD requested = static_cast<DWORD>(std::min(ChunkSize, bytes.size() - offset));
				DWORD written = 0;
				if (!WriteFile(File, bytes.data() + offset, requested, &written, nullptr) || written == 0)
				{
					return false;
				}

				offset += written;
			}

			if (!FlushFileBuffers(File))
			{
				return false;
			}

			return CloseHandle(std::exchange(File, INVALID_HANDLE_VALUE)) != FALSE;
		}

		bool Commit(const std::filesystem::path& destination)
		{
			// Both paths are in the same directory. Do not fall back to deleting the old file
			// or copying over it: a failed replacement must leave its contents intact.
			if (!MoveFileExW(Path.c_str(), destination.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				return false;
			}

			Path.clear();
			return true;
		}

	private:
		HANDLE File = INVALID_HANDLE_VALUE;
		std::filesystem::path Path;
	};
}

namespace Nyx::Engine
{
	bool BinaryWriter::SaveToFile(const std::filesystem::path& path) const
	{
		if (path.empty() || !path.has_filename())
		{
			return false;
		}

		std::error_code error;
		const std::filesystem::path destination = std::filesystem::absolute(path, error);
		if (error)
		{
			return false;
		}

		PendingArchiveFile file(destination.parent_path());
		return file.WriteAndClose(Buffer) && file.Commit(destination);
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
