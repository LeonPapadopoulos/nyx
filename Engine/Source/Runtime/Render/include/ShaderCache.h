#pragma once

#include "ShaderCompileService.h"
#include "ShaderCompiler.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace Nyx
{
	// Compiled shaders on disk: what the build compiled, next to the programs, and what live
	// reload compiled since, in Saved/ShaderCache.
	// An entry holds a successful compile's result and every file the compile read, with a hash of
	// its content, so finding it tells whether it still matches the files.
	//
	// Paths are kept relative to a root folder (e.g. Engine/Shaders), so a cache made on one
	// machine, or for a copy of the folder, is valid for the same files elsewhere.
	class ShaderCache
	{
	public:
		// The entries' format; a new one makes every entry a miss
		static constexpr uint32_t FormatVersion = 1;

		struct Entry
		{
			// Dependencies as absolute paths under this cache's root
			ShaderCompileResult Result;
			// Every file the compile read still has the content it had. A file that is gone can't be
			// compared and counts as unchanged: without sources (a shipped game), entries are used as
			// they are.
			bool bUpToDate = false;
		};

		// `compilerVersion` is part of every entry; another version is a miss. Tests pass their own.
		ShaderCache(std::filesystem::path folder, std::filesystem::path root, std::string compilerVersion = std::string(ShaderCompiler::GetSlangVersion()));

		// The entry for the request, if there is one for this compiler and format
		std::optional<Entry> Find(const ShaderCompileRequest& request) const;

		// Stores a successful result, replacing what was there. False when the result failed or the
		// file can't be written.
		bool Store(const ShaderCompileRequest& request, const ShaderCompileResult& result) const;

		// Where the entry for the request is kept: "<file name>.<hash of the request>.nyxshader"
		std::filesystem::path GetEntryPath(const ShaderCompileRequest& request) const;

		const std::filesystem::path& GetFolder() const
		{
			return Folder;
		}

	private:
		// What tells requests apart: kind, file relative to the root, domain
		std::string KeyOf(const ShaderCompileRequest& request) const;
		std::string Relative(const std::filesystem::path& path) const;
		std::filesystem::path Absolute(const std::string& relative) const;

	private:
		std::filesystem::path Folder;
		std::filesystem::path Root;
		std::string CompilerVersion;
	};

	// FNV-1a over a file's bytes; nullopt when it can't be read
	std::optional<uint64_t> HashFileContent(const std::filesystem::path& file);
}
