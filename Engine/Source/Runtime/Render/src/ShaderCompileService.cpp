#include "ShaderCompileService.h"

#include "BinaryArchive.h"
#include "NetConnection.h"
#include "StringUtil.h"

#include <chrono>
#include <sstream>

namespace Nyx::Engine
{
	namespace
	{
		// The program has this long to connect after it was started
		constexpr double StartTimeLimitSeconds = 20.0;

		double NowSeconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		ShaderCompileResult FailedResult(const std::filesystem::path& file, const std::string& message)
		{
			ShaderCompileResult result;
			ShaderDiagnostic diagnostic;
			diagnostic.File = file;
			diagnostic.Severity = EShaderDiagnosticSeverity::Error;
			diagnostic.Message = message;
			result.Diagnostics.push_back(std::move(diagnostic));
			if (!file.empty())
			{
				result.Dependencies.push_back(file);
			}
			return result;
		}
	}

	// -------------------------------------------------------------------------
	// LocalShaderCompileService
	// -------------------------------------------------------------------------

	LocalShaderCompileService::LocalShaderCompileService(std::vector<std::filesystem::path> searchPaths)
		: Compiler(std::move(searchPaths))
	{
	}

	void LocalShaderCompileService::Submit(const ShaderCompileRequest& request)
	{
		ShaderCompileResponse response{ request, {} };
		switch (request.Kind)
		{
		case EShaderCompileKind::Program:
			response.Result = Compiler.CompileProgram(request.File);
			break;
		case EShaderCompileKind::Material:
			response.Result = Compiler.CompileMaterial(request.File, request.Domain);
			break;
		case EShaderCompileKind::DefaultsWriter:
			response.Result = Compiler.CompileDefaultsWriter(request.File);
			break;
		case EShaderCompileKind::TestCrash:
			response.Result = FailedResult(request.File, "a test crash only happens in NyxShaderCompiler.exe");
			break;
		}
		Results.push_back(std::move(response));
	}

	std::vector<ShaderCompileResponse> LocalShaderCompileService::TakeResults()
	{
		return std::exchange(Results, {});
	}

	// -------------------------------------------------------------------------
	// RemoteShaderCompileService
	// -------------------------------------------------------------------------

	RemoteShaderCompileService::RemoteShaderCompileService(std::filesystem::path executable, std::vector<std::filesystem::path> searchPaths, bool bAllowTestCrash)
		: Executable(std::move(executable))
		, SearchPaths(std::move(searchPaths))
		, bAllowTestCrash(bAllowTestCrash)
	{
	}

	RemoteShaderCompileService::~RemoteShaderCompileService()
	{
		StopProgram();
	}

	void RemoteShaderCompileService::Submit(const ShaderCompileRequest& request)
	{
		Waiting.push_back(request);
	}

	void RemoteShaderCompileService::StartProgram(std::vector<ShaderCompileResponse>& results)
	{
		StopProgram();
		Listener = std::make_unique<Nyx::Net::Listener>();
		if (!Listener->Listen())
		{
			FailAll(Waiting, "the shader compiler can't be reached: " + Listener->GetError(), results);
			Listener.reset();
			return;
		}

		std::vector<std::string> arguments{ "--port", std::to_string(Listener->GetPort()) };
		for (const std::filesystem::path& path : SearchPaths)
		{
			arguments.push_back("--search-path");
			arguments.push_back(PathText(path));
		}
		if (bAllowTestCrash)
		{
			arguments.push_back("--allow-test-crash");
		}
		if (!Process.Start(Executable, arguments, false))
		{
			FailAll(Waiting, "the shader compiler couldn't be started: " + PathText(Executable), results);
			Listener.reset();
			return;
		}
		bStarted = true;
		StartedAtSeconds = NowSeconds();
		++StartCount;
	}

	void RemoteShaderCompileService::StopProgram()
	{
		if (Connection)
		{
			Connection->Close("shader compiler no longer needed");
			Connection.reset();
		}
		Listener.reset();
		if (bStarted)
		{
			// It ends by itself once the connection is gone; one that hangs is ended
			if (!Process.WaitForExit(1000))
			{
				Process.Terminate();
			}
			bStarted = false;
		}
	}

	void RemoteShaderCompileService::FailAll(std::deque<ShaderCompileRequest>& requests, const std::string& message, std::vector<ShaderCompileResponse>& results)
	{
		for (const ShaderCompileRequest& request : requests)
		{
			results.push_back({ request, FailedResult(request.File, message) });
		}
		requests.clear();
	}

	std::vector<ShaderCompileResponse> RemoteShaderCompileService::TakeResults()
	{
		std::vector<ShaderCompileResponse> results;
		if (Waiting.empty() && Sent.empty())
		{
			return results;
		}

		if (!bStarted)
		{
			StartProgram(results);
			if (!bStarted)
			{
				return results;
			}
		}

		// Waiting for the program to connect
		if (!Connection)
		{
			Connection = Listener->Accept();
			if (!Connection)
			{
				if (!Process.IsRunning() || NowSeconds() - StartedAtSeconds > StartTimeLimitSeconds)
				{
					const std::optional<uint32_t> exitCode = Process.GetExitCode();
					StopProgram();
					FailAll(Waiting, "the shader compiler didn't start" + (exitCode ? " (exit code " + std::to_string(*exitCode) + ")" : std::string()), results);
				}
				return results;
			}
			Listener.reset();
		}

		// One request at a time: when the program crashes, the request it was working on is the only
		// one without an answer. (With several sent, answers it had sent before crashing can be lost:
		// Windows drops data a crashed process sent that wasn't read yet.)
		bool bProgress = true;
		while (bProgress)
		{
			bProgress = false;
			if (Sent.empty() && !Waiting.empty())
			{
				BinaryWriter writer;
				ShaderCompileProtocol::WriteRequest(writer, Waiting.front());
				Connection->Send(ShaderCompileProtocol::RequestMessage, writer.GetBytes());
				Sent.push_back(Waiting.front());
				Waiting.pop_front();
			}

			Connection->Poll();
			while (std::optional<Nyx::Net::Message> message = Connection->Receive())
			{
				if (message->Type != ShaderCompileProtocol::ResultMessage || Sent.empty())
				{
					continue;
				}
				BinaryReader reader;
				reader.LoadFromMemory(std::move(message->Payload));
				uint32_t requestId = 0;
				ShaderCompileResult result;
				if (!ShaderCompileProtocol::ReadResult(reader, requestId, result) || requestId != Sent.front().Id)
				{
					result = FailedResult(Sent.front().File, "the shader compiler sent an unreadable result");
				}
				results.push_back({ Sent.front(), std::move(result) });
				Sent.pop_front();
				bProgress = !Waiting.empty();
			}
		}

		// Gone with requests unanswered: it crashed on the first of them. The others go to the next one.
		if (!Connection->IsConnected())
		{
			Process.WaitForExit(2000);
			const std::optional<uint32_t> exitCode = Process.GetExitCode();
			std::ostringstream code;
			if (exitCode)
			{
				code << " (exit code 0x" << std::hex << *exitCode << ")";
			}
			StopProgram();
			if (!Sent.empty())
			{
				results.push_back({ Sent.front(), FailedResult(Sent.front().File, "the shader compiler crashed compiling this file" + code.str()) });
				Sent.pop_front();
			}
			Waiting.insert(Waiting.begin(), Sent.begin(), Sent.end());
			Sent.clear();
		}
		return results;
	}
}
