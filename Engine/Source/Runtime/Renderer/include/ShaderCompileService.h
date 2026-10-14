#pragma once

#include "ChildProcess.h"
#include "ShaderCompiler.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Nyx::Net
{
	class Connection;
	class Listener;
}

namespace Nyx::Engine
{
	class BinaryReader;
	class BinaryWriter;

	// Where shaders get compiled for live reload (Design/shader-materials.md, stage 1, change 3):
	// in NyxShaderCompiler.exe, so that a compiler crash costs one compile and not the program
	// (RemoteShaderCompileService), or in this process, for tests (LocalShaderCompileService).
	// Nothing blocks: requests go in with Submit(), results come out of TakeResults().

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

	struct ShaderCompileResponse
	{
		ShaderCompileRequest Request;
		ShaderCompileResult Result;
	};

	class IShaderCompileService
	{
	public:
		virtual ~IShaderCompileService() = default;

		virtual void Submit(const ShaderCompileRequest& request) = 0;

		// Results that arrived since the last call, in the order the requests went in
		virtual std::vector<ShaderCompileResponse> TakeResults() = 0;

		// Requests submitted that have no result yet
		virtual size_t GetPendingCount() const = 0;
	};

	// Compiles at once, in this process
	class LocalShaderCompileService final : public IShaderCompileService
	{
	public:
		explicit LocalShaderCompileService(std::vector<std::filesystem::path> searchPaths);

		void Submit(const ShaderCompileRequest& request) override;
		std::vector<ShaderCompileResponse> TakeResults() override;
		size_t GetPendingCount() const override
		{
			return 0;
		}

	private:
		ShaderCompiler Compiler;
		std::vector<ShaderCompileResponse> Results;
	};

	// Compiles in NyxShaderCompiler.exe, started on the first request and again after it crashed or
	// exited. The program connects back over a local connection (NetConnection.h) and gets one
	// request at a time. When it crashes, the request it was working on gets a result with an error
	// saying so; the requests after it go to the next one.
	class RemoteShaderCompileService final : public IShaderCompileService
	{
	public:
		// `bAllowTestCrash`: the program obeys EShaderCompileKind::TestCrash (tests only)
		RemoteShaderCompileService(std::filesystem::path executable, std::vector<std::filesystem::path> searchPaths, bool bAllowTestCrash = false);
		~RemoteShaderCompileService() override;

		RemoteShaderCompileService(const RemoteShaderCompileService&) = delete;
		RemoteShaderCompileService& operator=(const RemoteShaderCompileService&) = delete;

		void Submit(const ShaderCompileRequest& request) override;
		std::vector<ShaderCompileResponse> TakeResults() override;
		size_t GetPendingCount() const override
		{
			return Waiting.size() + Sent.size();
		}

		// How often the program was started, e.g. 2 after one crash
		uint32_t GetStartCount() const
		{
			return StartCount;
		}

	private:
		void StartProgram(std::vector<ShaderCompileResponse>& results);
		void StopProgram();
		void FailAll(std::deque<ShaderCompileRequest>& requests, const std::string& message, std::vector<ShaderCompileResponse>& results);

	private:
		std::filesystem::path Executable;
		std::vector<std::filesystem::path> SearchPaths;
		bool bAllowTestCrash = false;

		std::unique_ptr<Nyx::Net::Listener> Listener;
		std::unique_ptr<Nyx::Net::Connection> Connection;
		ChildProcess Process;
		bool bStarted = false; // the program was started and hasn't been seen to end
		double StartedAtSeconds = 0.0;
		uint32_t StartCount = 0;

		std::deque<ShaderCompileRequest> Waiting; // not sent yet
		std::deque<ShaderCompileRequest> Sent;    // sent, no result yet; answered in order
	};

	// How requests and results travel between the engine and NyxShaderCompiler.exe
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
