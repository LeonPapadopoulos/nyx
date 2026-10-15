#include "ShaderCache.h"

#include "BinaryArchive.h"
#include "StringUtil.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <vector>

namespace Nyx
{
	namespace
	{
		constexpr char Magic[8] = { 'N', 'Y', 'X', 'S', 'H', 'A', 'D', 'R' };

		constexpr uint64_t FnvOffset = 14695981039346656037ull;
		constexpr uint64_t FnvPrime = 1099511628211ull;

		uint64_t HashBytes(const void* data, size_t size, uint64_t hash = FnvOffset)
		{
			const auto* bytes = static_cast<const unsigned char*>(data);
			for (size_t index = 0; index < size; ++index)
			{
				hash = (hash ^ bytes[index]) * FnvPrime;
			}
			return hash;
		}

		// PathText() with forward slashes, so a path gives the same text however it was written
		std::string GenericPathText(const std::filesystem::path& path)
		{
			const std::u8string text = path.generic_u8string();
			return std::string(text.begin(), text.end());
		}

		std::filesystem::path Normal(const std::filesystem::path& path)
		{
			std::error_code error;
			const std::filesystem::path absolute = std::filesystem::absolute(path, error);
			return (error ? path : absolute).lexically_normal();
		}
	}

	std::optional<uint64_t> HashFileContent(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		if (!stream)
		{
			return std::nullopt;
		}
		uint64_t hash = FnvOffset;
		std::vector<char> buffer(64 * 1024);
		while (stream)
		{
			stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
			hash = HashBytes(buffer.data(), static_cast<size_t>(stream.gcount()), hash);
		}
		return hash;
	}

	ShaderCache::ShaderCache(std::filesystem::path folder, std::filesystem::path root, std::string compilerVersion)
		: Folder(std::move(folder))
		, Root(Normal(root))
		, CompilerVersion(std::move(compilerVersion))
	{
	}

	std::string ShaderCache::Relative(const std::filesystem::path& path) const
	{
		const std::filesystem::path normal = Normal(path);
		const std::filesystem::path relative = normal.lexically_relative(Root);
		const bool bInside = !relative.empty() && *relative.begin() != "..";
		return GenericPathText(bInside ? relative : normal);
	}

	std::filesystem::path ShaderCache::Absolute(const std::string& relative) const
	{
		const std::filesystem::path path = TextPath(relative);
		return path.is_absolute() ? path : (Root / path).lexically_normal();
	}

	std::string ShaderCache::KeyOf(const ShaderCompileRequest& request) const
	{
		std::string key = "kind=" + std::to_string(static_cast<int>(request.Kind)) + "\nfile=" + ToLower(Relative(request.File));
		if (request.Kind == EShaderCompileKind::Material)
		{
			key += "\ndomain=" + request.Domain.Module;
			for (const std::string& entryPoint : request.Domain.EntryPoints)
			{
				key += "\nentry=" + entryPoint;
			}
		}
		return key;
	}

	std::filesystem::path ShaderCache::GetEntryPath(const ShaderCompileRequest& request) const
	{
		const std::string key = KeyOf(request);
		char hash[17] = {};
		std::snprintf(hash, sizeof(hash), "%016llx", static_cast<unsigned long long>(HashBytes(key.data(), key.size())));
		return Folder / (request.File.filename().string() + "." + std::string(hash, 8) + ".nyxshader");
	}

	bool ShaderCache::Store(const ShaderCompileRequest& request, const ShaderCompileResult& result) const
	{
		if (!result.bSucceeded)
		{
			return false;
		}

		BinaryWriter writer;
		writer.WriteBytes(Magic, sizeof(Magic));
		writer.WriteUInt32(FormatVersion);
		writer.WriteString(CompilerVersion);
		writer.WriteString(KeyOf(request));

		// Every file the compile read, with what it held then; the request's own file always
		std::vector<std::filesystem::path> files = result.Dependencies;
		const bool bHasOwnFile = std::any_of(files.begin(), files.end(), [&](const std::filesystem::path& file)
			{
				return ToLower(Relative(file)) == ToLower(Relative(request.File));
			});
		if (!bHasOwnFile)
		{
			files.push_back(request.File);
		}
		writer.WriteUInt32(static_cast<uint32_t>(files.size()));
		for (const std::filesystem::path& file : files)
		{
			const std::optional<uint64_t> hash = HashFileContent(file);
			if (!hash)
			{
				return false; // A file the compile read is gone already: nothing worth keeping
			}
			writer.WriteString(Relative(file));
			writer.WriteUInt64(*hash);
		}

		// The result, its dependencies relative to the root
		ShaderCompileResult stored = result;
		stored.Dependencies.clear();
		BinaryWriter resultWriter;
		ShaderCompileProtocol::WriteResult(resultWriter, 0, stored);
		writer.WriteBlock(resultWriter);

		std::error_code error;
		std::filesystem::create_directories(Folder, error);
		return writer.SaveToFile(GetEntryPath(request));
	}

	std::optional<ShaderCache::Entry> ShaderCache::Find(const ShaderCompileRequest& request) const
	{
		BinaryReader reader;
		if (!reader.LoadFromFile(GetEntryPath(request)))
		{
			return std::nullopt;
		}

		char magic[sizeof(Magic)] = {};
		uint32_t format = 0;
		std::string compilerVersion;
		std::string key;
		uint32_t fileCount = 0;
		if (!reader.ReadBytes(magic, sizeof(magic)) || !std::equal(std::begin(magic), std::end(magic), std::begin(Magic)) || !reader.ReadUInt32(format) ||
			format != FormatVersion || !reader.ReadString(compilerVersion) || compilerVersion != CompilerVersion || !reader.ReadString(key) ||
			key != KeyOf(request) || !reader.ReadUInt32(fileCount) || fileCount > 4096)
		{
			return std::nullopt;
		}

		Entry entry;
		entry.bUpToDate = true;
		std::vector<std::filesystem::path> files;
		for (uint32_t index = 0; index < fileCount; ++index)
		{
			std::string relative;
			uint64_t hash = 0;
			if (!reader.ReadString(relative) || !reader.ReadUInt64(hash))
			{
				return std::nullopt;
			}
			const std::filesystem::path file = Absolute(relative);
			files.push_back(file);
			std::error_code error;
			if (std::filesystem::exists(file, error) && HashFileContent(file) != hash)
			{
				entry.bUpToDate = false;
			}
		}

		BinaryReader resultReader;
		uint32_t requestId = 0;
		if (!reader.ReadBlock(resultReader) || !ShaderCompileProtocol::ReadResult(resultReader, requestId, entry.Result) || !entry.Result.bSucceeded)
		{
			return std::nullopt;
		}
		entry.Result.Dependencies = std::move(files);
		return entry;
	}
}
