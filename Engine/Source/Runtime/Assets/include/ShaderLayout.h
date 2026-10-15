#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	// What reflection says about a material's struct, as plain data: the parameters panel is built
	// from it, and material values are written at its offsets. Offsets are in scalar layout, the
	// layout ByteAddressBuffer.Load<T>() reads.

	// An argument of an attribute: text, or a number
	struct ShaderAttributeArgument
	{
		bool bText = false;
		std::string Text;
		double Number = 0.0;
	};

	// An attribute on a field, e.g. [Range(0, 1)]: Name "Range", Arguments 0 and 1
	struct ShaderAttribute
	{
		std::string Name;
		std::vector<ShaderAttributeArgument> Arguments;
	};

	enum class EShaderFieldKind : uint8_t
	{
		Scalar,
		Vector,
		Matrix,
		Texture, // nyx::Texture: an index into the global texture array (one uint)
		Struct,  // the members are in Fields
		Other,   // something the material system doesn't know; shown, but not editable
	};

	enum class EShaderScalarType : uint8_t
	{
		None,
		Float,
		Int,
		UInt,
		Bool, // 4 bytes, 0 or 1
	};

	struct ShaderField
	{
		std::string Name;
		std::string TypeName; // as Slang writes it, e.g. "vector<float,3>" or "nyx.Texture"
		EShaderFieldKind Kind = EShaderFieldKind::Other;
		EShaderScalarType Scalar = EShaderScalarType::None;
		uint32_t Rows = 1;    // Matrix only
		uint32_t Columns = 1; // components of a Vector, columns of a Matrix

		uint32_t Offset = 0; // from the start of the struct that holds the field
		uint32_t Size = 0;   // of the whole field, all array elements included

		// An array has ArrayCount elements, ArrayStride bytes apart; 0 for a field that isn't one
		uint32_t ArrayCount = 0;
		uint32_t ArrayStride = 0;

		std::vector<ShaderAttribute> Attributes;
		// Reflection gives a default value for scalars only; the full defaults come from the
		// defaults writer (ShaderCompiler::CompileDefaultsWriter())
		std::optional<double> ScalarDefault;

		std::vector<ShaderField> Fields; // Struct only

		const ShaderAttribute* FindAttribute(std::string_view name) const;
	};

	struct ShaderLayout
	{
		std::string TypeName;
		uint32_t Size = 0;
		uint32_t Alignment = 0;
		std::vector<ShaderField> Fields;

		const ShaderField* FindField(std::string_view name) const;
	};
}
