#pragma once

#include "ShaderCompileTypes.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Engine
{
	// Compiles shaders to SPIR-V with Slang:
	//   - materials (.slang): a struct named like the file, specialised into a domain's entry points;
	//   - programs: the engine's GLSL (.vert, .frag, .comp), or a .slang file with entry points.
	// No Vulkan; the renderer makes pipelines from what it returns.
	//
	// Slang is delay-loaded: a program that never makes a ShaderCompiler doesn't need slang.dll, and
	// one without it gets IsAvailable() false instead of a crash. Not thread-safe: one thread at a
	// time per compiler.

	class ShaderCompiler
	{
	public:
		// `searchPaths`: where imports and includes are found, e.g. Paths::GetShaderLibraryDir().
		// The compiled file's own folder is searched first.
		explicit ShaderCompiler(std::vector<std::filesystem::path> searchPaths);
		~ShaderCompiler();

		ShaderCompiler(const ShaderCompiler&) = delete;
		ShaderCompiler& operator=(const ShaderCompiler&) = delete;

		// False when slang.dll couldn't be loaded or started; GetProblem() says why
		bool IsAvailable() const;
		const std::string& GetProblem() const;

		// Slang's version, e.g. "2026.1-52-gc8ddf20bb"; part of every cache key later
		static std::string_view GetSlangVersion();

		// A program: GLSL by extension (.vert, .frag, .comp; entry point "main"), or a .slang file
		// with entry points marked [shader("...")]
		ShaderCompileResult CompileProgram(const std::filesystem::path& file);

		// A material: the struct named like the file (Stone.slang: struct Stone), specialised into
		// the domain's entry points. MaterialLayout holds the struct's fields.
		ShaderCompileResult CompileMaterial(const std::filesystem::path& file, const ShaderDomain& domain);

		// A compute shader "writeDefaults" that stores the material's struct, made with its
		// defaults, at offset 0 of a RWByteAddressBuffer at set 0, binding 3 (a binding the
		// bindless set leaves free). Run once, it gives a new material's bytes, vectors included.
		ShaderCompileResult CompileDefaultsWriter(const std::filesystem::path& file);

	private:
		struct SlangGlobal;
		std::unique_ptr<SlangGlobal> Global;
		std::vector<std::filesystem::path> SearchPaths;
		std::string Problem;
	};

	// Slang's diagnostics are text only: "file(line): severity code: message", then the source line
	// and a line with a caret under the column. Exposed for tests.
	std::vector<ShaderDiagnostic> ParseSlangDiagnostics(std::string_view text);
}
