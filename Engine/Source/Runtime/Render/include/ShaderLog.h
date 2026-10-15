#pragma once

#include "ShaderCompiler.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Nyx::Engine
{
	// The last compile of a shader, for the Shader Log
	struct ShaderLogEntry
	{
		// "Grid.frag", or a material shader's path in the assets folder ("Shaders/Stone.slang")
		std::string Name;
		std::filesystem::path File;
		bool bSucceeded = false;
		double Milliseconds = 0.0;
		uint32_t EntryPoints = 0;
		size_t SpirvBytes = 0;
		std::chrono::system_clock::time_point When{};
		// Everything the compiler said, in its order
		std::vector<ShaderDiagnostic> Diagnostics;

		// The first error, null without one
		const ShaderDiagnostic* FindFirstError() const;
	};

	// The last compile of every shader that compiled since the start, the newest last: a shader's
	// compile replaces its entry and moves it to the end. No Vulkan; the renderer fills it from the
	// updates it takes (VulkanRenderer::ApplyShaderUpdates()).
	class ShaderLog
	{
	public:
		void Add(ShaderLogEntry entry);
		// Takes a shader's entry out, e.g. of a compile the user doesn't need to see once it works
		void Remove(const std::string& name);

		const std::vector<ShaderLogEntry>& GetEntries() const { return Entries; }
		// The entry of a shader's file; null without one
		const ShaderLogEntry* FindByFile(const std::filesystem::path& file) const;
		size_t CountFailures() const;
		// Goes up with every change
		uint64_t GetRevision() const { return Revision; }

	private:
		std::vector<ShaderLogEntry> Entries;
		uint64_t Revision = 0;
	};
}
