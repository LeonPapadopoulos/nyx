#include "ShaderCompileService.h"

#include "BinaryArchive.h"
#include "NetConnection.h"

#include <chrono>
#include <cstring>
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

		std::string PathText(const std::filesystem::path& path)
		{
			const std::u8string text = path.u8string();
			return std::string(text.begin(), text.end());
		}

		std::filesystem::path TextPath(const std::string& text)
		{
			return std::filesystem::path(std::u8string(text.begin(), text.end()));
		}

		void WriteDouble(BinaryWriter& writer, double value)
		{
			uint64_t bits = 0;
			std::memcpy(&bits, &value, sizeof(bits));
			writer.WriteUInt64(bits);
		}

		bool ReadDouble(BinaryReader& reader, double& outValue)
		{
			uint64_t bits = 0;
			if (!reader.ReadUInt64(bits))
			{
				return false;
			}
			std::memcpy(&outValue, &bits, sizeof(bits));
			return true;
		}

		void WriteField(BinaryWriter& writer, const ShaderField& field)
		{
			writer.WriteString(field.Name);
			writer.WriteString(field.TypeName);
			writer.WriteUInt8(static_cast<uint8_t>(field.Kind));
			writer.WriteUInt8(static_cast<uint8_t>(field.Scalar));
			writer.WriteUInt32(field.Rows);
			writer.WriteUInt32(field.Columns);
			writer.WriteUInt32(field.Offset);
			writer.WriteUInt32(field.Size);
			writer.WriteUInt32(field.ArrayCount);
			writer.WriteUInt32(field.ArrayStride);
			writer.WriteUInt32(static_cast<uint32_t>(field.Attributes.size()));
			for (const ShaderAttribute& attribute : field.Attributes)
			{
				writer.WriteString(attribute.Name);
				writer.WriteUInt32(static_cast<uint32_t>(attribute.Arguments.size()));
				for (const ShaderAttributeArgument& argument : attribute.Arguments)
				{
					writer.WriteBool(argument.bText);
					writer.WriteString(argument.Text);
					WriteDouble(writer, argument.Number);
				}
			}
			writer.WriteBool(field.ScalarDefault.has_value());
			WriteDouble(writer, field.ScalarDefault.value_or(0.0));
			writer.WriteUInt32(static_cast<uint32_t>(field.Fields.size()));
			for (const ShaderField& member : field.Fields)
			{
				WriteField(writer, member);
			}
		}

		bool ReadField(BinaryReader& reader, ShaderField& field, int depth)
		{
			uint8_t kind = 0;
			uint8_t scalar = 0;
			uint32_t attributeCount = 0;
			if (depth > 32 || !reader.ReadString(field.Name) || !reader.ReadString(field.TypeName) || !reader.ReadUInt8(kind) || !reader.ReadUInt8(scalar) ||
				!reader.ReadUInt32(field.Rows) || !reader.ReadUInt32(field.Columns) || !reader.ReadUInt32(field.Offset) || !reader.ReadUInt32(field.Size) ||
				!reader.ReadUInt32(field.ArrayCount) || !reader.ReadUInt32(field.ArrayStride) || !reader.ReadUInt32(attributeCount))
			{
				return false;
			}
			field.Kind = static_cast<EShaderFieldKind>(kind);
			field.Scalar = static_cast<EShaderScalarType>(scalar);
			for (uint32_t index = 0; index < attributeCount && reader.IsValid(); ++index)
			{
				ShaderAttribute attribute;
				uint32_t argumentCount = 0;
				if (!reader.ReadString(attribute.Name) || !reader.ReadUInt32(argumentCount))
				{
					return false;
				}
				for (uint32_t argumentIndex = 0; argumentIndex < argumentCount && reader.IsValid(); ++argumentIndex)
				{
					ShaderAttributeArgument argument;
					if (!reader.ReadBool(argument.bText) || !reader.ReadString(argument.Text) || !ReadDouble(reader, argument.Number))
					{
						return false;
					}
					attribute.Arguments.push_back(std::move(argument));
				}
				field.Attributes.push_back(std::move(attribute));
			}
			bool bHasDefault = false;
			double scalarDefault = 0.0;
			uint32_t memberCount = 0;
			if (!reader.ReadBool(bHasDefault) || !ReadDouble(reader, scalarDefault) || !reader.ReadUInt32(memberCount))
			{
				return false;
			}
			if (bHasDefault)
			{
				field.ScalarDefault = scalarDefault;
			}
			for (uint32_t index = 0; index < memberCount && reader.IsValid(); ++index)
			{
				ShaderField member;
				if (!ReadField(reader, member, depth + 1))
				{
					return false;
				}
				field.Fields.push_back(std::move(member));
			}
			return reader.IsValid();
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
	// Protocol
	// -------------------------------------------------------------------------

	namespace ShaderCompileProtocol
	{
		void WriteRequest(BinaryWriter& writer, const ShaderCompileRequest& request)
		{
			writer.WriteUInt32(request.Id);
			writer.WriteUInt8(static_cast<uint8_t>(request.Kind));
			writer.WriteString(PathText(request.File));
			writer.WriteString(request.Domain.Module);
			writer.WriteUInt32(static_cast<uint32_t>(request.Domain.EntryPoints.size()));
			for (const std::string& entryPoint : request.Domain.EntryPoints)
			{
				writer.WriteString(entryPoint);
			}
		}

		bool ReadRequest(BinaryReader& reader, ShaderCompileRequest& outRequest)
		{
			uint8_t kind = 0;
			std::string file;
			uint32_t entryPointCount = 0;
			if (!reader.ReadUInt32(outRequest.Id) || !reader.ReadUInt8(kind) || !reader.ReadString(file) || !reader.ReadString(outRequest.Domain.Module) ||
				!reader.ReadUInt32(entryPointCount) || kind > static_cast<uint8_t>(EShaderCompileKind::TestCrash))
			{
				return false;
			}
			outRequest.Kind = static_cast<EShaderCompileKind>(kind);
			outRequest.File = TextPath(file);
			outRequest.Domain.EntryPoints.clear();
			for (uint32_t index = 0; index < entryPointCount && reader.IsValid(); ++index)
			{
				std::string entryPoint;
				reader.ReadString(entryPoint);
				outRequest.Domain.EntryPoints.push_back(std::move(entryPoint));
			}
			return reader.IsValid();
		}

		void WriteResult(BinaryWriter& writer, uint32_t requestId, const ShaderCompileResult& result)
		{
			writer.WriteUInt32(requestId);
			writer.WriteBool(result.bSucceeded);
			WriteDouble(writer, result.Milliseconds);

			writer.WriteUInt32(static_cast<uint32_t>(result.EntryPoints.size()));
			for (const CompiledEntryPoint& entryPoint : result.EntryPoints)
			{
				writer.WriteString(entryPoint.Name);
				writer.WriteUInt8(static_cast<uint8_t>(entryPoint.Stage));
				writer.WriteUInt32(static_cast<uint32_t>(entryPoint.Spirv.size()));
				writer.WriteBytes(entryPoint.Spirv.data(), entryPoint.Spirv.size() * sizeof(uint32_t));
			}

			writer.WriteUInt32(static_cast<uint32_t>(result.Diagnostics.size()));
			for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
			{
				writer.WriteString(PathText(diagnostic.File));
				writer.WriteUInt32(diagnostic.Line);
				writer.WriteUInt32(diagnostic.Column);
				writer.WriteUInt8(static_cast<uint8_t>(diagnostic.Severity));
				writer.WriteInt32(diagnostic.Code);
				writer.WriteString(diagnostic.Message);
			}

			writer.WriteUInt32(static_cast<uint32_t>(result.Dependencies.size()));
			for (const std::filesystem::path& dependency : result.Dependencies)
			{
				writer.WriteString(PathText(dependency));
			}

			writer.WriteBool(result.MaterialLayout.has_value());
			if (result.MaterialLayout)
			{
				const ShaderLayout& layout = *result.MaterialLayout;
				writer.WriteString(layout.TypeName);
				writer.WriteUInt32(layout.Size);
				writer.WriteUInt32(layout.Alignment);
				writer.WriteUInt32(static_cast<uint32_t>(layout.Fields.size()));
				for (const ShaderField& field : layout.Fields)
				{
					WriteField(writer, field);
				}
			}
		}

		bool ReadResult(BinaryReader& reader, uint32_t& outRequestId, ShaderCompileResult& outResult)
		{
			outResult = ShaderCompileResult{};
			uint32_t entryPointCount = 0;
			if (!reader.ReadUInt32(outRequestId) || !reader.ReadBool(outResult.bSucceeded) || !ReadDouble(reader, outResult.Milliseconds) ||
				!reader.ReadUInt32(entryPointCount))
			{
				return false;
			}
			for (uint32_t index = 0; index < entryPointCount && reader.IsValid(); ++index)
			{
				CompiledEntryPoint entryPoint;
				uint8_t stage = 0;
				uint32_t wordCount = 0;
				if (!reader.ReadString(entryPoint.Name) || !reader.ReadUInt8(stage) || !reader.ReadUInt32(wordCount) ||
					static_cast<size_t>(wordCount) * sizeof(uint32_t) > reader.GetRemainingSize())
				{
					return false;
				}
				entryPoint.Stage = static_cast<EShaderStage>(stage);
				entryPoint.Spirv.resize(wordCount);
				reader.ReadBytes(entryPoint.Spirv.data(), entryPoint.Spirv.size() * sizeof(uint32_t));
				outResult.EntryPoints.push_back(std::move(entryPoint));
			}

			uint32_t diagnosticCount = 0;
			if (!reader.ReadUInt32(diagnosticCount))
			{
				return false;
			}
			for (uint32_t index = 0; index < diagnosticCount && reader.IsValid(); ++index)
			{
				ShaderDiagnostic diagnostic;
				std::string file;
				uint8_t severity = 0;
				if (!reader.ReadString(file) || !reader.ReadUInt32(diagnostic.Line) || !reader.ReadUInt32(diagnostic.Column) || !reader.ReadUInt8(severity) ||
					!reader.ReadInt32(diagnostic.Code) || !reader.ReadString(diagnostic.Message))
				{
					return false;
				}
				diagnostic.File = TextPath(file);
				diagnostic.Severity = static_cast<EShaderDiagnosticSeverity>(severity);
				outResult.Diagnostics.push_back(std::move(diagnostic));
			}

			uint32_t dependencyCount = 0;
			if (!reader.ReadUInt32(dependencyCount))
			{
				return false;
			}
			for (uint32_t index = 0; index < dependencyCount && reader.IsValid(); ++index)
			{
				std::string dependency;
				reader.ReadString(dependency);
				outResult.Dependencies.push_back(TextPath(dependency));
			}

			bool bHasLayout = false;
			if (!reader.ReadBool(bHasLayout))
			{
				return false;
			}
			if (bHasLayout)
			{
				ShaderLayout layout;
				uint32_t fieldCount = 0;
				if (!reader.ReadString(layout.TypeName) || !reader.ReadUInt32(layout.Size) || !reader.ReadUInt32(layout.Alignment) || !reader.ReadUInt32(fieldCount))
				{
					return false;
				}
				for (uint32_t index = 0; index < fieldCount && reader.IsValid(); ++index)
				{
					ShaderField field;
					if (!ReadField(reader, field, 0))
					{
						return false;
					}
					layout.Fields.push_back(std::move(field));
				}
				outResult.MaterialLayout = std::move(layout);
			}
			return reader.IsValid();
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
