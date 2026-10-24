#pragma once

#include "ShaderLayout.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	class BinaryReader;
	class BinaryWriter;

	// What a shader compile is asked for and what it gives, as plain data: ShaderCompiler makes
	// results, and the editor link and the shader cache carry them as they are.

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
	// A material that also implements the domain's vertex interface, a vertex function, gets the
	// vertex entry points as well, which run it; the others don't.
	struct ShaderDomain
	{
		std::string Module;                   // as imported, e.g. "nyx.surface"
		std::vector<std::string> EntryPoints; // e.g. "surfaceVertex", "surfaceFragment", "decalFragment", "decalPickingFragment"
		std::string VertexInterface;          // e.g. "ISurfaceVertex"; empty: none
		std::vector<std::string> VertexEntryPoints; // e.g. "displacedSurfaceVertex", "displacedShadowVertex"

		static ShaderDomain Surface();
	};

	enum class EShaderCompileKind : uint8_t
	{
		Program,        // ShaderCompiler::CompileProgram()
		Material,       // ShaderCompiler::CompileMaterial()
		DefaultsWriter, // ShaderCompiler::CompileDefaultsWriter()
		TestCrash,      // Makes NyxShaderCompiler.exe crash, if it was started to allow that (tests)
	};

	struct ShaderCompileRequest
	{
		uint32_t Id = 0;
		EShaderCompileKind Kind = EShaderCompileKind::Program;
		std::filesystem::path File;
		ShaderDomain Domain; // Material only
	};

	// How requests and results travel as bytes: between the engine and NyxShaderCompiler.exe,
	// in the shader cache, and over the editor link
	namespace ShaderCompileProtocol
	{
		constexpr uint16_t RequestMessage = 1;
		constexpr uint16_t ResultMessage = 2;

		void WriteRequest(BinaryWriter& writer, const ShaderCompileRequest& request);
		bool ReadRequest(BinaryReader& reader, ShaderCompileRequest& outRequest);
		void WriteResult(BinaryWriter& writer, uint32_t requestId, const ShaderCompileResult& result);
		bool ReadResult(BinaryReader& reader, uint32_t& outRequestId, ShaderCompileResult& outResult);
	}
}
