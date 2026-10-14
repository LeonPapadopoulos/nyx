#pragma once

#include "ShaderLayout.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx::Engine
{
	// Compiles shaders to SPIR-V with Slang (Design/shader-materials.md, stage 1, change 2):
	//   - materials (.slang): a struct named like the file, specialised into a domain's entry points;
	//   - programs: the engine's GLSL (.vert, .frag, .comp), or a .slang file with entry points.
	// No Vulkan; the renderer makes pipelines from what it returns.
	//
	// Slang is delay-loaded: a program that never makes a ShaderCompiler doesn't need slang.dll, and
	// one without it gets IsAvailable() false instead of a crash. Not thread-safe: one thread at a
	// time per compiler.

	enum class EShaderStage : uint8_t
	{
		Vertex,
		Fragment,
		Compute,
	};

	enum class EShaderDiagnosticSeverity : uint8_t
	{
		Note,
		Warning,
		Error,
	};

	struct ShaderDiagnostic
	{
		std::filesystem::path File; // empty for a message about no file (e.g. an internal error)
		uint32_t Line = 0;
		uint32_t Column = 0; // 0 when Slang showed no position in the line
		EShaderDiagnosticSeverity Severity = EShaderDiagnosticSeverity::Error;
		int Code = 0;
		std::string Message;

		// "Stone.slang(13,32): error 30015: undefined identifier 'Tilling'."
		std::string ToString() const;
	};

	struct CompiledEntryPoint
	{
		std::string Name; // the name in the SPIR-V, to make the pipeline stage with
		EShaderStage Stage = EShaderStage::Vertex;
		std::vector<uint32_t> Spirv;
	};

	struct ShaderCompileResult
	{
		bool bSucceeded = false;
		std::vector<CompiledEntryPoint> EntryPoints;
		// Everything Slang said, in its order; a successful compile may have warnings
		std::vector<ShaderDiagnostic> Diagnostics;
		// The file itself and every file it imports or includes, for reloading on change
		std::vector<std::filesystem::path> Dependencies;
		// Materials only: the material's struct
		std::optional<ShaderLayout> MaterialLayout;
		double Milliseconds = 0.0;

		const CompiledEntryPoint* FindEntryPoint(std::string_view name) const;
		bool HasErrors() const;
	};

	// Where materials of one kind plug in: a module of the shader library and its entry points.
	// Entry points that are generic over the material are specialised with the material's struct.
	struct ShaderDomain
	{
		std::string Module;                   // as imported, e.g. "nyx.surface"
		std::vector<std::string> EntryPoints; // e.g. "surfaceVertex", "surfaceFragment"

		static ShaderDomain Surface();
	};

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
