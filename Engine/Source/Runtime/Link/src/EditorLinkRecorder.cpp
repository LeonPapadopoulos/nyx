#include "NyxPCH.h"
#include "EditorLinkRecorder.h"

#include "BinaryArchive.h"

namespace Nyx
{
	bool EditorLinkRecorder::Open(const std::filesystem::path& path, const std::string& programName)
	{
		Close();

		std::error_code ignored;
		if (path.has_parent_path())
		{
			std::filesystem::create_directories(path.parent_path(), ignored);
		}

		File.open(path, std::ios::binary | std::ios::trunc);
		if (!File.is_open())
		{
			return false;
		}

		BinaryWriter header;
		header.WriteUInt32(FileMagic);
		header.WriteUInt32(FileVersion);
		header.WriteString(programName);
		header.WriteUInt64(GetClockTimeMs());
		File.write(reinterpret_cast<const char*>(header.GetBytes().data()), static_cast<std::streamsize>(header.GetBytes().size()));
		File.flush();

		Path = path;
		Start = std::chrono::steady_clock::now();
		return File.good();
	}

	void EditorLinkRecorder::Record(ELinkDirection direction, const Net::Message& message)
	{
		if (!File.is_open())
		{
			return;
		}

		const auto sinceStart = std::chrono::steady_clock::now() - Start;

		BinaryWriter record;
		record.WriteUInt64(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(sinceStart).count()));
		record.WriteUInt8(direction == ELinkDirection::Sent ? 0 : 1);
		record.WriteUInt16(message.Type);
		record.WriteUInt32(static_cast<uint32_t>(message.Payload.size()));
		record.WriteBytes(message.Payload.data(), message.Payload.size());

		File.write(reinterpret_cast<const char*>(record.GetBytes().data()), static_cast<std::streamsize>(record.GetBytes().size()));
		File.flush();
	}

	void EditorLinkRecorder::Close()
	{
		if (File.is_open())
		{
			File.close();
		}
		Path.clear();
	}
}
