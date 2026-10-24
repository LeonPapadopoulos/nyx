#include "NyxPCH.h"
#include "EditorLinkMessages.h"

#include "Paths.h"

#include <algorithm>
#include <vector>

namespace
{
	using namespace Nyx;

	// Bytes with their size in front (u32)
	void WriteSizedBytes(BinaryWriter& writer, const std::vector<std::byte>& bytes)
	{
		writer.WriteUInt32(static_cast<uint32_t>(bytes.size()));
		writer.WriteBytes(bytes.data(), bytes.size());
	}

	bool ReadSizedBytes(BinaryReader& reader, std::vector<std::byte>& outBytes)
	{
		uint32_t size = 0;
		if (!reader.ReadUInt32(size) || size > reader.GetRemainingSize())
		{
			return false;
		}

		outBytes.resize(size);
		return reader.ReadBytes(outBytes.data(), size);
	}

	// A block as BinaryWriter::WriteBlock wrote it, with its size, so ReadBlock can read it again
	bool ReadWholeBlock(BinaryReader& reader, std::vector<std::byte>& outBlock)
	{
		std::vector<std::byte> content;
		if (!ReadSizedBytes(reader, content))
		{
			return false;
		}

		BinaryWriter block;
		block.WriteUInt32(static_cast<uint32_t>(content.size()));
		block.WriteBytes(content.data(), content.size());
		outBlock = block.GetBytes();
		return true;
	}
}

namespace Nyx
{
	void HelloMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(ProtocolVersion);
		writer.WriteString(ProgramName);
		writer.WriteUInt32(ProcessId);
	}

	bool HelloMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt32(ProtocolVersion) && reader.ReadString(ProgramName) && reader.ReadUInt32(ProcessId);
	}

	void LogLineMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt8(static_cast<uint8_t>(Level));
		writer.WriteUInt64(TimeMs);
		writer.WriteString(LoggerName);
		writer.WriteString(Text);
	}

	bool LogLineMessage::Read(BinaryReader& reader)
	{
		uint8_t level = 0;
		if (!reader.ReadUInt8(level) || !reader.ReadUInt64(TimeMs) || !reader.ReadString(LoggerName) || !reader.ReadString(Text))
		{
			return false;
		}

		Level = static_cast<ELogLevel>(level);
		return true;
	}

	void SetPropertiesMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt64(Entity.Value);
		writer.WriteString(ComponentType);

		// Already a block, with its size in front
		writer.WriteBytes(Properties.data(), Properties.size());
	}

	bool SetPropertiesMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt64(Entity.Value) && reader.ReadString(ComponentType) && ReadWholeBlock(reader, Properties);
	}

	void CreateEntityMessage::Write(BinaryWriter& writer) const
	{
		WriteSizedBytes(writer, Entity);
	}

	bool CreateEntityMessage::Read(BinaryReader& reader)
	{
		return ReadSizedBytes(reader, Entity);
	}

	void DeleteEntityMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt64(Entity.Value);
	}

	bool DeleteEntityMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt64(Entity.Value);
	}

	void InspectEntityMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(Request);
		writer.WriteUInt64(Entity.Value);
	}

	bool InspectEntityMessage::Read(BinaryReader& reader)
	{
		return reader.ReadUInt32(Request) && reader.ReadUInt64(Entity.Value);
	}

	// An answer has at most this many components and values each, so damaged data can't make the
	// reader allocate a lot
	constexpr uint32_t MaxInspectedComponents = 4096;
	constexpr uint32_t MaxInspectedValues = 4096;

	void InspectedEntityMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(Request);
		writer.WriteUInt64(Entity.Value);
		writer.WriteBool(bFound);
		writer.WriteUInt64(Step);
		writer.WriteUInt32(static_cast<uint32_t>(Components.size()));
		for (const InspectedComponent& component : Components)
		{
			writer.WriteString(component.Type);
			writer.WriteString(component.DisplayName);
			WriteSizedBytes(writer, component.Properties);
			writer.WriteUInt32(static_cast<uint32_t>(component.Values.size()));
			for (const InspectedValue& value : component.Values)
			{
				writer.WriteString(value.Name);
				writer.WriteString(value.Value);
			}
		}
	}

	bool InspectedEntityMessage::Read(BinaryReader& reader)
	{
		uint32_t componentCount = 0;
		if (!reader.ReadUInt32(Request) || !reader.ReadUInt64(Entity.Value) || !reader.ReadBool(bFound) || !reader.ReadUInt64(Step) ||
			!reader.ReadUInt32(componentCount) || componentCount > MaxInspectedComponents)
		{
			return false;
		}

		Components.clear();
		Components.resize(componentCount);
		for (InspectedComponent& component : Components)
		{
			uint32_t valueCount = 0;
			if (!reader.ReadString(component.Type) || !reader.ReadString(component.DisplayName) || !ReadSizedBytes(reader, component.Properties) ||
				!reader.ReadUInt32(valueCount) || valueCount > MaxInspectedValues)
			{
				return false;
			}
			component.Values.resize(valueCount);
			for (InspectedValue& value : component.Values)
			{
				if (!reader.ReadString(value.Name) || !reader.ReadString(value.Value))
				{
					return false;
				}
			}
		}
		return true;
	}

	// A crash report has at most this many frames (the crash handler sends at most 64), so
	// damaged data can't make the reader allocate a lot
	constexpr uint32_t MaxCrashStackFrames = 256;

	void CrashMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteUInt32(Report.ExceptionCode);
		writer.WriteString(Report.Description);

		const uint32_t frameCount = static_cast<uint32_t>((std::min)(Report.Frames.size(), static_cast<size_t>(MaxCrashStackFrames)));
		writer.WriteUInt32(frameCount);
		for (uint32_t i = 0; i < frameCount; ++i)
		{
			const CrashStackFrame& frame = Report.Frames[i];
			writer.WriteUInt64(frame.Address);
			writer.WriteString(frame.Module);
			writer.WriteString(frame.Function);
			writer.WriteString(frame.File);
			writer.WriteUInt32(frame.Line);
		}
	}

	bool CrashMessage::Read(BinaryReader& reader)
	{
		uint32_t frameCount = 0;
		if (!reader.ReadUInt32(Report.ExceptionCode) || !reader.ReadString(Report.Description) || !reader.ReadUInt32(frameCount) ||
			frameCount > MaxCrashStackFrames)
		{
			return false;
		}

		Report.Frames.resize(frameCount);
		for (CrashStackFrame& frame : Report.Frames)
		{
			if (!reader.ReadUInt64(frame.Address) || !reader.ReadString(frame.Module) || !reader.ReadString(frame.Function) ||
				!reader.ReadString(frame.File) || !reader.ReadUInt32(frame.Line))
			{
				return false;
			}
		}

		return true;
	}

	std::filesystem::path ToLinkPath(const std::filesystem::path& path)
	{
		std::error_code error;
		const std::filesystem::path relative = std::filesystem::relative(path, Nyx::Paths::GetProjectRoot(), error);
		return !error && !relative.empty() && *relative.begin() != ".." ? relative : path;
	}

	namespace
	{
		std::filesystem::path FromLinkPath(const std::filesystem::path& path)
		{
			return path.is_relative() && !path.empty() ? (Nyx::Paths::GetProjectRoot() / path).lexically_normal() : path;
		}

		// The request's and result's paths, each through `convert`
		template <typename TConvert>
		void ConvertPaths(ShaderCompileRequest& request, ShaderCompileResult& result, TConvert convert)
		{
			request.File = convert(request.File);
			for (std::filesystem::path& dependency : result.Dependencies)
			{
				dependency = convert(dependency);
			}
			for (ShaderDiagnostic& diagnostic : result.Diagnostics)
			{
				diagnostic.File = diagnostic.File.empty() ? diagnostic.File : convert(diagnostic.File);
			}
		}
	}

	void ShaderProgramMessage::Write(BinaryWriter& writer) const
	{
		ShaderCompileRequest request = Request;
		ShaderCompileResult result = Result;
		ConvertPaths(request, result, ToLinkPath);
		ShaderCompileProtocol::WriteRequest(writer, request);
		ShaderCompileProtocol::WriteResult(writer, 0, result);
	}

	bool ShaderProgramMessage::Read(BinaryReader& reader)
	{
		uint32_t requestId = 0;
		if (!ShaderCompileProtocol::ReadRequest(reader, Request) || !ShaderCompileProtocol::ReadResult(reader, requestId, Result))
		{
			return false;
		}
		ConvertPaths(Request, Result, FromLinkPath);
		return true;
	}

	void MaterialValuesMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteString(Path);
		writer.WriteString(Text);
	}

	bool MaterialValuesMessage::Read(BinaryReader& reader)
	{
		return reader.ReadString(Path) && reader.ReadString(Text);
	}

	void PhysicsMaterialValuesMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteString(Path);
		writer.WriteString(Text);
	}

	bool PhysicsMaterialValuesMessage::Read(BinaryReader& reader)
	{
		return reader.ReadString(Path) && reader.ReadString(Text);
	}

	void SetSimulationPausedMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteBool(bPaused);
	}

	bool SetSimulationPausedMessage::Read(BinaryReader& reader)
	{
		return reader.ReadBool(bPaused);
	}

	void SetInputAssetMessage::Write(BinaryWriter& writer) const
	{
		// Already a block, with its size in front
		writer.WriteBytes(Asset.data(), Asset.size());
	}

	bool SetInputAssetMessage::Read(BinaryReader& reader)
	{
		return ReadWholeBlock(reader, Asset);
	}

	void SetInputBackgroundMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteBool(bGamepadsInBackground);
	}

	bool SetInputBackgroundMessage::Read(BinaryReader& reader)
	{
		return reader.ReadBool(bGamepadsInBackground);
	}

	void InputSubscribeMessage::Write(BinaryWriter& writer) const
	{
		writer.WriteBool(bSubscribed);
	}

	bool InputSubscribeMessage::Read(BinaryReader& reader)
	{
		return reader.ReadBool(bSubscribed);
	}

	void InputDefinitionsMessage::Write(BinaryWriter& writer) const
	{
		WriteSizedBytes(writer, Record);
	}

	bool InputDefinitionsMessage::Read(BinaryReader& reader)
	{
		return ReadSizedBytes(reader, Record);
	}

	void InputFrameMessage::Write(BinaryWriter& writer) const
	{
		WriteSizedBytes(writer, Record);
	}

	bool InputFrameMessage::Read(BinaryReader& reader)
	{
		return ReadSizedBytes(reader, Record);
	}
}
