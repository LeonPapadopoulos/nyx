#include "FileWatcher.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <cstdint>

namespace Nyx
{
	// One watched folder: a handle opened for overlapped reads, and one read always pending
	struct FileWatcher::Folder
	{
		std::filesystem::path Path;
		HANDLE Handle = INVALID_HANDLE_VALUE;
		OVERLAPPED Overlapped{};
		// DWORD-aligned, as ReadDirectoryChangesW requires
		std::vector<uint32_t> Buffer = std::vector<uint32_t>(16 * 1024);

		~Folder()
		{
			if (Handle != INVALID_HANDLE_VALUE)
			{
				// The pending read must end before its buffer and OVERLAPPED go away
				CancelIoEx(Handle, &Overlapped);
				DWORD transferred = 0;
				GetOverlappedResult(Handle, &Overlapped, &transferred, TRUE);
				CloseHandle(Handle);
			}
			if (Overlapped.hEvent)
			{
				CloseHandle(Overlapped.hEvent);
			}
		}

		bool StartRead()
		{
			ResetEvent(Overlapped.hEvent);
			constexpr DWORD filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE |
				FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_CREATION;
			return ReadDirectoryChangesW(Handle, Buffer.data(), static_cast<DWORD>(Buffer.size() * sizeof(uint32_t)), TRUE, filter, nullptr, &Overlapped, nullptr) != 0;
		}
	};

	FileWatcher::FileWatcher() = default;
	FileWatcher::~FileWatcher() = default;

	bool FileWatcher::Watch(const std::filesystem::path& folder)
	{
		std::error_code error;
		const std::filesystem::path absolute = std::filesystem::absolute(folder, error).lexically_normal();
		if (error || !std::filesystem::is_directory(absolute, error))
		{
			Problem = "not a folder: " + folder.string();
			return false;
		}

		auto watched = std::make_unique<Folder>();
		watched->Path = absolute;
		watched->Handle = CreateFileW(absolute.wstring().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
			OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
		if (watched->Handle == INVALID_HANDLE_VALUE)
		{
			Problem = "can't open " + absolute.string() + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		watched->Overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
		if (!watched->Overlapped.hEvent || !watched->StartRead())
		{
			Problem = "can't watch " + absolute.string() + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		Folders.push_back(std::move(watched));
		return true;
	}

	std::vector<std::filesystem::path> FileWatcher::TakeChanges()
	{
		std::vector<std::filesystem::path> changes;
		const auto add = [&changes](const std::filesystem::path& path)
		{
			if (std::find(changes.begin(), changes.end(), path) == changes.end())
			{
				changes.push_back(path);
			}
		};

		for (const std::unique_ptr<Folder>& folder : Folders)
		{
			// Each finished read is taken and a new one started at once, until none has finished
			while (true)
			{
				DWORD transferred = 0;
				if (!GetOverlappedResult(folder->Handle, &folder->Overlapped, &transferred, FALSE))
				{
					const DWORD error = GetLastError();
					if (error != ERROR_IO_INCOMPLETE)
					{
						// The read failed (e.g. too many changes for the buffer): anything may have changed
						add(folder->Path);
						folder->StartRead();
					}
					break;
				}

				if (transferred == 0)
				{
					// The system had more changes than the buffer holds and kept none of them
					add(folder->Path);
				}
				else
				{
					const auto* bytes = reinterpret_cast<const uint8_t*>(folder->Buffer.data());
					size_t offset = 0;
					while (true)
					{
						const auto* info = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(bytes + offset);
						const std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
						add((folder->Path / name).lexically_normal());
						if (info->NextEntryOffset == 0)
						{
							break;
						}
						offset += info->NextEntryOffset;
					}
				}

				if (!folder->StartRead())
				{
					add(folder->Path);
					break;
				}
			}
		}
		return changes;
	}
}
