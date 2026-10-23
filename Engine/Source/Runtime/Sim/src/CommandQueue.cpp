#include "NyxPCH.h"
#include "CommandQueue.h"

#include "BinaryArchive.h"
#include "Log.h"
#include "ReflectedArchiveSerializer.h"

namespace Nyx
{
	void CommandQueue::Add(const void* command, const Nyx::Reflection::TypeMetadata& type)
	{
		BinaryWriter writer;
		if (!ReflectedArchiveSerializer::SerializeObject(writer, command, type))
		{
			LOG_ERROR("Command {0} has a property that can't be written; it is dropped", type.Name);
			return;
		}

		const std::vector<std::byte>& block = writer.GetBytes();
		Entries.push_back(Entry{ &type, Bytes.size(), block.size() });
		Bytes.insert(Bytes.end(), block.begin(), block.end());
	}

	void CommandQueue::Clear()
	{
		Entries.clear();
		Bytes.clear();
	}

	void CommandQueue::TakeFrom(CommandQueue& other)
	{
		for (const Entry& entry : other.Entries)
		{
			Entries.push_back(Entry{ entry.Type, Bytes.size() + entry.Offset, entry.Size });
		}
		Bytes.insert(Bytes.end(), other.Bytes.begin(), other.Bytes.end());
		other.Clear();
	}

	bool CommandQueue::Read(const Entry& entry, void* outCommand) const
	{
		BinaryReader reader;
		reader.LoadFromMemory(std::vector<std::byte>(Bytes.begin() + static_cast<ptrdiff_t>(entry.Offset),
			Bytes.begin() + static_cast<ptrdiff_t>(entry.Offset + entry.Size)));

		ReadWarnings warnings;
		const bool bRead = ReflectedArchiveSerializer::DeserializeObject(reader, outCommand, *entry.Type, warnings);
		warnings.Log(entry.Type->Name);
		return bRead;
	}
}
