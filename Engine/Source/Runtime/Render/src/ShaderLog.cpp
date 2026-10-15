#include "ShaderLog.h"

#include <algorithm>
#include <system_error>

namespace Nyx
{
	const ShaderDiagnostic* ShaderLogEntry::FindFirstError() const
	{
		for (const ShaderDiagnostic& diagnostic : Diagnostics)
		{
			if (diagnostic.Severity == EShaderDiagnosticSeverity::Error)
			{
				return &diagnostic;
			}
		}
		return nullptr;
	}

	void ShaderLog::Add(ShaderLogEntry entry)
	{
		Remove(entry.Name);
		Entries.push_back(std::move(entry));
		++Revision;
	}

	void ShaderLog::Remove(const std::string& name)
	{
		const auto removed = std::remove_if(Entries.begin(), Entries.end(), [&name](const ShaderLogEntry& entry)
			{
				return entry.Name == name;
			});
		if (removed != Entries.end())
		{
			Entries.erase(removed, Entries.end());
			++Revision;
		}
	}

	const ShaderLogEntry* ShaderLog::FindByFile(const std::filesystem::path& file) const
	{
		for (auto entry = Entries.rbegin(); entry != Entries.rend(); ++entry)
		{
			std::error_code error;
			if (entry->File == file || (!entry->File.empty() && std::filesystem::equivalent(entry->File, file, error)))
			{
				return &*entry;
			}
		}
		return nullptr;
	}

	size_t ShaderLog::CountFailures() const
	{
		return static_cast<size_t>(std::count_if(Entries.begin(), Entries.end(), [](const ShaderLogEntry& entry)
			{
				return !entry.bSucceeded;
			}));
	}
}
