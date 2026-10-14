// NyxShaderCompiler.exe: compiles shaders for a program that runs it (RemoteShaderCompileService),
// so that a compiler crash ends this process and not that one. Connects back to the port it is
// given, answers requests in order, and exits once the connection is gone.
//
//   NyxShaderCompiler --port <port> [--search-path <folder>]... [--allow-test-crash]
#include "BinaryArchive.h"
#include "NetConnection.h"
#include "ShaderCompileService.h"
#include "ShaderCompiler.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	std::filesystem::path TextPath(const std::string& text)
	{
		return std::filesystem::path(std::u8string(text.begin(), text.end()));
	}

	ShaderCompileResult Compile(ShaderCompiler& compiler, const ShaderCompileRequest& request, bool bAllowTestCrash)
	{
		switch (request.Kind)
		{
		case EShaderCompileKind::Program:
			return compiler.CompileProgram(request.File);
		case EShaderCompileKind::Material:
			return compiler.CompileMaterial(request.File, request.Domain);
		case EShaderCompileKind::DefaultsWriter:
			return compiler.CompileDefaultsWriter(request.File);
		case EShaderCompileKind::TestCrash:
			if (bAllowTestCrash)
			{
				volatile int* nowhere = nullptr;
				*nowhere = 1;
			}
			break;
		}
		return ShaderCompileResult{};
	}
}

int main(int argc, char** argv)
{
	// A crash ends the process quietly, without the system's error dialog: the program that
	// started it reports it and starts a new one
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

	uint16_t port = 0;
	std::vector<std::filesystem::path> searchPaths;
	bool bAllowTestCrash = false;
	for (int index = 1; index < argc; ++index)
	{
		const std::string argument = argv[index];
		if (argument == "--port" && index + 1 < argc)
		{
			port = static_cast<uint16_t>(std::atoi(argv[++index]));
		}
		else if (argument == "--search-path" && index + 1 < argc)
		{
			searchPaths.push_back(TextPath(argv[++index]));
		}
		else if (argument == "--allow-test-crash")
		{
			bAllowTestCrash = true;
		}
	}
	if (port == 0)
	{
		return 2;
	}

	ShaderCompiler compiler(searchPaths);
	std::unique_ptr<Nyx::Net::Connection> connection = Nyx::Net::Connection::ConnectToLocalPort(port);
	while (connection->GetState() != Nyx::Net::EConnectionState::Closed)
	{
		connection->Poll();
		bool bWorked = false;
		while (std::optional<Nyx::Net::Message> message = connection->Receive())
		{
			if (message->Type != ShaderCompileProtocol::RequestMessage)
			{
				continue;
			}
			BinaryReader reader;
			reader.LoadFromMemory(std::move(message->Payload));
			ShaderCompileRequest request;
			if (!ShaderCompileProtocol::ReadRequest(reader, request))
			{
				return 3;
			}

			const ShaderCompileResult result = Compile(compiler, request, bAllowTestCrash);
			BinaryWriter writer;
			ShaderCompileProtocol::WriteResult(writer, request.Id, result);
			connection->Send(ShaderCompileProtocol::ResultMessage, writer.GetBytes());
			connection->Poll();
			bWorked = true;
		}
		if (!bWorked)
		{
			std::this_thread::sleep_for(std::chrono::milliseconds(2));
		}
	}
	return 0;
}
