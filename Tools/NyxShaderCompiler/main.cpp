// NyxShaderCompiler.exe compiles shaders, two ways:
//
//   NyxShaderCompiler --port <port> [--search-path <folder>]... [--allow-test-crash]
//     For a program that runs it (RemoteShaderCompileService), so that a compiler crash ends this
//     process and not that one. Connects back to the port, answers requests one at a time, and
//     exits once the connection is gone.
//
//   NyxShaderCompiler build --source <folder> --output <folder> [--search-path <folder>]...
//     For the build: compiles the engine's shaders in the source folder (.slang, .vert, .frag, .comp) into
//     a shader cache in the output folder (ShaderCache), each only when it or a file it read
//     changed. Errors are printed the way Visual Studio lists them; the exit code is 1 when any
//     shader doesn't compile.
#include "BinaryArchive.h"
#include "NetConnection.h"
#include "ShaderCache.h"
#include "ShaderCompileService.h"
#include "ShaderCompiler.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;

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

	struct Arguments
	{
		bool bBuild = false;
		uint16_t Port = 0;
		std::filesystem::path Source;
		std::filesystem::path Output;
		std::vector<std::filesystem::path> SearchPaths;
		bool bAllowTestCrash = false;
	};

	Arguments ReadArguments(int argc, char** argv)
	{
		Arguments arguments;
		for (int index = 1; index < argc; ++index)
		{
			const std::string argument = argv[index];
			const bool bHasValue = index + 1 < argc;
			if (argument == "build" && index == 1)
			{
				arguments.bBuild = true;
			}
			else if (argument == "--port" && bHasValue)
			{
				arguments.Port = static_cast<uint16_t>(std::atoi(argv[++index]));
			}
			else if (argument == "--source" && bHasValue)
			{
				arguments.Source = TextPath(argv[++index]);
			}
			else if (argument == "--output" && bHasValue)
			{
				arguments.Output = TextPath(argv[++index]);
			}
			else if (argument == "--search-path" && bHasValue)
			{
				arguments.SearchPaths.push_back(TextPath(argv[++index]));
			}
			else if (argument == "--allow-test-crash")
			{
				arguments.bAllowTestCrash = true;
			}
		}
		return arguments;
	}

	// "C:\...\Grid.frag(12,5): error S30015: message", as Visual Studio's error list reads it
	void PrintDiagnostic(const ShaderDiagnostic& diagnostic, const std::filesystem::path& file)
	{
		const std::filesystem::path where = diagnostic.File.empty() ? file : diagnostic.File;
		std::string text = where.string();
		if (diagnostic.Line > 0)
		{
			text += "(" + std::to_string(diagnostic.Line) + (diagnostic.Column > 0 ? "," + std::to_string(diagnostic.Column) : "") + ")";
		}
		switch (diagnostic.Severity)
		{
		case EShaderDiagnosticSeverity::Error:
			text += ": error";
			break;
		case EShaderDiagnosticSeverity::Warning:
			text += ": warning";
			break;
		default:
			text += ": note";
			break;
		}
		text += diagnostic.Code != 0 ? " S" + std::to_string(diagnostic.Code) + ": " : ": ";
		std::cout << text << diagnostic.Message << "\n";
	}

	int Build(const Arguments& arguments)
	{
		if (arguments.Source.empty() || arguments.Output.empty())
		{
			std::cout << "NyxShaderCompiler build: --source and --output are needed\n";
			return 2;
		}

		std::vector<std::filesystem::path> files;
		std::error_code error;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(arguments.Source, error))
		{
			const std::string extension = entry.path().extension().string();
			if (entry.is_regular_file() && (extension == ".slang" || extension == ".vert" || extension == ".frag" || extension == ".comp"))
			{
				files.push_back(entry.path());
			}
		}
		if (error)
		{
			std::cout << arguments.Source.string() << ": error: can't read the folder\n";
			return 1;
		}
		std::sort(files.begin(), files.end());

		const ShaderCache cache(arguments.Output, arguments.Source);
		std::unique_ptr<ShaderCompiler> compiler; // Made only when something needs compiling
		int compiled = 0;
		int failed = 0;
		for (const std::filesystem::path& file : files)
		{
			ShaderCompileRequest request;
			request.Kind = EShaderCompileKind::Program;
			request.File = file;
			const std::optional<ShaderCache::Entry> entry = cache.Find(request);
			if (entry && entry->bUpToDate)
			{
				continue;
			}

			if (!compiler)
			{
				compiler = std::make_unique<ShaderCompiler>(arguments.SearchPaths);
				if (!compiler->IsAvailable())
				{
					std::cout << "NyxShaderCompiler: error: " << compiler->GetProblem() << "\n";
					return 1;
				}
			}
			const ShaderCompileResult result = compiler->CompileProgram(file);
			for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
			{
				PrintDiagnostic(diagnostic, file);
			}
			if (!result.bSucceeded)
			{
				++failed;
				continue;
			}
			if (!cache.Store(request, result))
			{
				std::cout << file.string() << ": error: compiled, but can't be stored in " << arguments.Output.string() << "\n";
				++failed;
				continue;
			}
			++compiled;
		}

		std::cout << "NyxShaderCompiler: " << files.size() << " shaders, " << compiled << " compiled, " << failed << " failed, "
				  << files.size() - compiled - failed << " up to date\n";
		return failed == 0 ? 0 : 1;
	}

	int Serve(const Arguments& arguments)
	{
		// A crash ends the process quietly, without the system's error dialog: the program that
		// started it reports it and starts a new one
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		if (arguments.Port == 0)
		{
			return 2;
		}

		ShaderCompiler compiler(arguments.SearchPaths);
		std::unique_ptr<Nyx::Net::Connection> connection = Nyx::Net::Connection::ConnectToLocalPort(arguments.Port);
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

				const ShaderCompileResult result = Compile(compiler, request, arguments.bAllowTestCrash);
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
}

int main(int argc, char** argv)
{
	const Arguments arguments = ReadArguments(argc, argv);
	return arguments.bBuild ? Build(arguments) : Serve(arguments);
}
