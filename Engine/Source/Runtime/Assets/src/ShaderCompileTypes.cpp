#include "ShaderCompileTypes.h"

#include "BinaryArchive.h"
#include "StringUtil.h"

#include <algorithm>
#include <cstring>

namespace Nyx
{
	namespace
	{
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
	}

	// -------------------------------------------------------------------------
	// Plain data
	// -------------------------------------------------------------------------

	const ShaderAttribute* ShaderField::FindAttribute(std::string_view name) const
	{
		const auto found = std::find_if(Attributes.begin(), Attributes.end(), [name](const ShaderAttribute& attribute)
			{
				return attribute.Name == name;
			});
		return found != Attributes.end() ? &*found : nullptr;
	}

	const ShaderField* ShaderLayout::FindField(std::string_view name) const
	{
		const auto found = std::find_if(Fields.begin(), Fields.end(), [name](const ShaderField& field)
			{
				return field.Name == name;
			});
		return found != Fields.end() ? &*found : nullptr;
	}

	std::string ShaderDiagnostic::ToString() const
	{
		std::string text = File.empty() ? std::string() : File.filename().string();
		if (Line > 0)
		{
			text += "(" + std::to_string(Line) + (Column > 0 ? "," + std::to_string(Column) : "") + ")";
		}
		if (!text.empty())
		{
			text += ": ";
		}
		text += Severity == EShaderDiagnosticSeverity::Error ? "error" : Severity == EShaderDiagnosticSeverity::Warning ? "warning"
																														: "note";
		if (Code != 0)
		{
			text += " " + std::to_string(Code);
		}
		return text + ": " + Message;
	}

	const CompiledEntryPoint* ShaderCompileResult::FindEntryPoint(std::string_view name) const
	{
		const auto found = std::find_if(EntryPoints.begin(), EntryPoints.end(), [name](const CompiledEntryPoint& entryPoint)
			{
				return entryPoint.Name == name;
			});
		return found != EntryPoints.end() ? &*found : nullptr;
	}

	bool ShaderCompileResult::HasErrors() const
	{
		return std::any_of(Diagnostics.begin(), Diagnostics.end(), [](const ShaderDiagnostic& diagnostic)
			{
				return diagnostic.Severity == EShaderDiagnosticSeverity::Error;
			});
	}

	ShaderDomain ShaderDomain::Surface()
	{
		return ShaderDomain{ "nyx.surface", { "surfaceVertex", "surfaceFragment" } };
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
}
