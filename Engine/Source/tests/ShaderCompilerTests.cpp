// ShaderCompiler without a GPU: materials compiled into their domain, the layout reflection
// reports, diagnostics with file, line and column, dependencies, and the engine's GLSL compiled
// by Slang with the same layouts as glslc's. SPIR-V is checked with SPIRV-Tools' validator.
#include "Paths.h"
#include "ShaderCompiler.h"

#include <spirv-tools/libspirv.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	const std::filesystem::path TestShaders = NYX_TEST_SHADERS_DIR;
	const std::filesystem::path EngineShaders = NYX_ENGINE_SHADERS_DIR;
	// What glslc made of the engine's shaders, while the build still runs it (until change 4)
	const std::filesystem::path GlslcShaders = NYX_GLSLC_SHADERS_DIR;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	std::string DescribeDiagnostics(const ShaderCompileResult& result)
	{
		std::string text;
		for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
		{
			text += "\n  " + diagnostic.ToString();
		}
		return text;
	}

	void RequireSucceeded(const ShaderCompileResult& result, const std::string& what)
	{
		Require(result.bSucceeded, what + " doesn't compile:" + DescribeDiagnostics(result));
	}

	ShaderCompiler& Compiler()
	{
		static ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		return compiler;
	}

	// -------------------------------------------------------------------------
	// SPIR-V
	// -------------------------------------------------------------------------

	// Empty when valid. `bScalarBlockLayout`: Vulkan 1.2's scalarBlockLayout, which materials need.
	std::string ValidateSpirv(const std::vector<uint32_t>& words, bool bScalarBlockLayout)
	{
		spv_context context = spvContextCreate(SPV_ENV_VULKAN_1_3);
		spv_validator_options options = spvValidatorOptionsCreate();
		spvValidatorOptionsSetScalarBlockLayout(options, bScalarBlockLayout);
		spv_const_binary_t binary{ words.data(), words.size() };
		spv_diagnostic diagnostic = nullptr;
		std::string message;
		if (spvValidateWithOptions(context, options, &binary, &diagnostic) != SPV_SUCCESS)
		{
			message = diagnostic && diagnostic->error ? diagnostic->error : "invalid";
		}
		spvDiagnosticDestroy(diagnostic);
		spvValidatorOptionsDestroy(options);
		spvContextDestroy(context);
		return message;
	}

	std::vector<uint32_t> ReadSpirvFile(const std::filesystem::path& file)
	{
		std::ifstream stream(file, std::ios::binary);
		Require(static_cast<bool>(stream), "can't read " + file.string());
		std::vector<char> bytes((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
		std::vector<uint32_t> words(bytes.size() / 4);
		std::memcpy(words.data(), bytes.data(), words.size() * 4);
		return words;
	}

	// The decorations that decide how a shader reads its inputs and buffers: offsets, matrix
	// strides and majorness, bindings, sets and locations. Ids differ between compilers, so each is
	// described without its target; the result is sorted, each once. Once, because Slang sometimes
	// declares a second view of a buffer with the same layout. Built-ins are left out: glslc
	// declares gl_Position in a gl_PerVertex block, Slang as a variable of its own; both are valid.
	std::vector<std::string> InterfaceDecorations(const std::vector<uint32_t>& words)
	{
		constexpr uint32_t OpDecorate = 71;
		constexpr uint32_t OpMemberDecorate = 72;
		const auto interesting = [](uint32_t decoration)
		{
			// RowMajor, ColMajor, MatrixStride, Location, Binding, DescriptorSet, Offset
			return decoration == 4 || decoration == 5 || decoration == 7 || decoration == 30 || decoration == 33 || decoration == 34 ||
				decoration == 35;
		};
		std::vector<std::string> decorations;
		for (size_t index = 5; index < words.size();)
		{
			const uint32_t wordCount = words[index] >> 16;
			const uint32_t opcode = words[index] & 0xFFFF;
			if (wordCount == 0)
			{
				break;
			}
			if (opcode == OpDecorate && wordCount >= 3 && interesting(words[index + 2]))
			{
				std::string text = "decoration " + std::to_string(words[index + 2]);
				for (uint32_t literal = 3; literal < wordCount; ++literal)
				{
					text += " " + std::to_string(words[index + literal]);
				}
				decorations.push_back(text);
			}
			else if (opcode == OpMemberDecorate && wordCount >= 4 && interesting(words[index + 3]))
			{
				std::string text = "member " + std::to_string(words[index + 2]) + " decoration " + std::to_string(words[index + 3]);
				for (uint32_t literal = 4; literal < wordCount; ++literal)
				{
					text += " " + std::to_string(words[index + literal]);
				}
				decorations.push_back(text);
			}
			index += wordCount;
		}
		std::sort(decorations.begin(), decorations.end());
		decorations.erase(std::unique(decorations.begin(), decorations.end()), decorations.end());
		return decorations;
	}

	// The capabilities a module declares (OpCapability), e.g. 1 for Shader
	std::vector<uint32_t> Capabilities(const std::vector<uint32_t>& words)
	{
		constexpr uint32_t OpCapability = 17;
		std::vector<uint32_t> capabilities;
		for (size_t index = 5; index < words.size();)
		{
			const uint32_t wordCount = words[index] >> 16;
			if (wordCount == 0)
			{
				break;
			}
			if ((words[index] & 0xFFFF) == OpCapability && wordCount == 2)
			{
				capabilities.push_back(words[index + 1]);
			}
			index += wordCount;
		}
		return capabilities;
	}

	// "set.binding" of every decorated variable
	std::vector<std::string> DescriptorBindings(const std::vector<uint32_t>& words)
	{
		constexpr uint32_t OpDecorate = 71;
		std::vector<std::pair<uint32_t, uint32_t>> sets;
		std::vector<std::pair<uint32_t, uint32_t>> bindings;
		for (size_t index = 5; index < words.size();)
		{
			const uint32_t wordCount = words[index] >> 16;
			if (wordCount == 0)
			{
				break;
			}
			if ((words[index] & 0xFFFF) == OpDecorate && wordCount == 4)
			{
				if (words[index + 2] == 34)
				{
					sets.emplace_back(words[index + 1], words[index + 3]);
				}
				else if (words[index + 2] == 33)
				{
					bindings.emplace_back(words[index + 1], words[index + 3]);
				}
			}
			index += wordCount;
		}
		std::vector<std::string> result;
		for (const auto& [id, binding] : bindings)
		{
			const auto set = std::find_if(sets.begin(), sets.end(), [id](const auto& entry)
				{
					return entry.first == id;
				});
			const std::string text = std::to_string(set != sets.end() ? set->second : 99) + "." + std::to_string(binding);
			if (std::find(result.begin(), result.end(), text) == result.end())
			{
				result.push_back(text);
			}
		}
		std::sort(result.begin(), result.end());
		return result;
	}

	bool HasFile(const std::vector<std::filesystem::path>& files, const std::string& name)
	{
		return std::any_of(files.begin(), files.end(), [&name](const std::filesystem::path& file)
			{
				return file.filename() == name;
			});
	}

	const ShaderDiagnostic* FirstError(const ShaderCompileResult& result)
	{
		const auto found = std::find_if(result.Diagnostics.begin(), result.Diagnostics.end(), [](const ShaderDiagnostic& diagnostic)
			{
				return diagnostic.Severity == EShaderDiagnosticSeverity::Error;
			});
		return found != result.Diagnostics.end() ? &*found : nullptr;
	}

	// -------------------------------------------------------------------------
	// Tests
	// -------------------------------------------------------------------------

	void TestCompilerStarts()
	{
		Require(Compiler().IsAvailable(), "the compiler isn't available: " + Compiler().GetProblem());
		Require(!ShaderCompiler::GetSlangVersion().empty(), "no Slang version");
	}

	void TestMaterialCompilesIntoItsDomain()
	{
		const ShaderCompileResult result = Compiler().CompileMaterial(TestShaders / "Stone.slang", ShaderDomain::Surface());
		RequireSucceeded(result, "Stone");
		Require(result.EntryPoints.size() == 2, "Stone has " + std::to_string(result.EntryPoints.size()) + " entry points, expected 2");

		const CompiledEntryPoint* vertex = result.FindEntryPoint("surfaceVertex");
		const CompiledEntryPoint* fragment = result.FindEntryPoint("surfaceFragment");
		Require(vertex && vertex->Stage == EShaderStage::Vertex, "no vertex entry point surfaceVertex");
		Require(fragment && fragment->Stage == EShaderStage::Fragment, "no fragment entry point surfaceFragment");

		for (const CompiledEntryPoint* entryPoint : { vertex, fragment })
		{
			const std::string problem = ValidateSpirv(entryPoint->Spirv, true);
			Require(problem.empty(), entryPoint->Name + " isn't valid SPIR-V: " + problem);
		}

		// The bindless plan: 0.0 textures, 0.1 samplers, 0.2 materials, 1.0 the view
		const std::vector<std::string> bindings = DescriptorBindings(fragment->Spirv);
		Require(bindings == std::vector<std::string>{ "0.0", "0.1", "0.2", "1.0" }, "the fragment's bindings aren't the plan's");
	}

	void TestMaterialLayout()
	{
		const ShaderCompileResult result = Compiler().CompileMaterial(TestShaders / "Stone.slang", ShaderDomain::Surface());
		RequireSucceeded(result, "Stone");
		Require(result.MaterialLayout.has_value(), "Stone has no layout");
		const ShaderLayout& layout = *result.MaterialLayout;

		// Scalar layout: a float3 takes 12 bytes, and the float after it follows directly
		Require(layout.Size == 36, "Stone is " + std::to_string(layout.Size) + " bytes, expected 36");
		Require(layout.Fields.size() == 6, "Stone has " + std::to_string(layout.Fields.size()) + " fields, expected 6");

		struct Expected
		{
			const char* Name;
			EShaderFieldKind Kind;
			EShaderScalarType Scalar;
			uint32_t Columns;
			uint32_t Offset;
			uint32_t Size;
		};
		const Expected expected[] = {
			{ "BaseColor", EShaderFieldKind::Vector, EShaderScalarType::Float, 3, 0, 12 },
			{ "Roughness", EShaderFieldKind::Scalar, EShaderScalarType::Float, 1, 12, 4 },
			{ "Wetness", EShaderFieldKind::Scalar, EShaderScalarType::Float, 1, 16, 4 },
			{ "Albedo", EShaderFieldKind::Texture, EShaderScalarType::None, 1, 20, 4 },
			{ "Tiling", EShaderFieldKind::Vector, EShaderScalarType::Float, 2, 24, 8 },
			{ "Mode", EShaderFieldKind::Scalar, EShaderScalarType::Int, 1, 32, 4 },
		};
		for (size_t index = 0; index < std::size(expected); ++index)
		{
			const ShaderField& field = layout.Fields[index];
			const Expected& want = expected[index];
			Require(field.Name == want.Name, "field " + std::to_string(index) + " is " + field.Name + ", expected " + want.Name);
			Require(field.Kind == want.Kind && field.Scalar == want.Scalar && field.Columns == want.Columns, field.Name + " has the wrong type (" + field.TypeName + ")");
			Require(field.Offset == want.Offset, field.Name + " is at " + std::to_string(field.Offset) + ", expected " + std::to_string(want.Offset));
			Require(field.Size == want.Size, field.Name + " is " + std::to_string(field.Size) + " bytes, expected " + std::to_string(want.Size));
		}

		// Attributes and their arguments
		const ShaderAttribute* range = layout.FindField("Roughness")->FindAttribute("Range");
		Require(range && range->Arguments.size() == 2 && !range->Arguments[0].bText && range->Arguments[0].Number == 0.0 && range->Arguments[1].Number == 1.0,
			"Roughness's Range(0, 1) doesn't come back");
		const ShaderAttribute* category = layout.FindField("BaseColor")->FindAttribute("Category");
		Require(category && category->Arguments.size() == 1 && category->Arguments[0].bText && category->Arguments[0].Text == "Surface",
			"BaseColor's Category(\"Surface\") doesn't come back");
		Require(layout.FindField("BaseColor")->FindAttribute("Color") != nullptr, "BaseColor's Color doesn't come back");
		const ShaderAttribute* defaultTexture = layout.FindField("Albedo")->FindAttribute("Default");
		Require(defaultTexture && defaultTexture->Arguments.size() == 1 && defaultTexture->Arguments[0].Text == "White", "Albedo's Default(\"White\") doesn't come back");
		const ShaderAttribute* dragSpeed = layout.FindField("Tiling")->FindAttribute("DragSpeed");
		Require(dragSpeed && std::abs(dragSpeed->Arguments[0].Number - 0.01) < 1e-6, "Tiling's DragSpeed(0.01) doesn't come back");
		Require(layout.FindField("Mode")->FindAttribute("Hidden") != nullptr, "Mode's Hidden doesn't come back");

		// Scalar defaults through reflection; vectors have none there (the defaults writer has them)
		const ShaderField& roughness = *layout.FindField("Roughness");
		Require(roughness.ScalarDefault && std::abs(*roughness.ScalarDefault - 0.78) < 1e-6, "Roughness's default isn't 0.78");
		const ShaderField& mode = *layout.FindField("Mode");
		Require(mode.ScalarDefault && *mode.ScalarDefault == 2.0, "Mode's default isn't 2");
		Require(!layout.FindField("BaseColor")->ScalarDefault, "a vector got a scalar default");
	}

	void TestSlangDiagnostics()
	{
		const ShaderCompileResult result = Compiler().CompileMaterial(TestShaders / "Broken.slang", ShaderDomain::Surface());
		Require(!result.bSucceeded && result.EntryPoints.empty(), "Broken compiled");
		const ShaderDiagnostic* error = FirstError(result);
		Require(error != nullptr, "Broken has no error");
		Require(error->File.filename() == "Broken.slang", "the error names " + error->File.string() + ":" + DescribeDiagnostics(result));
		Require(error->Line == 13 && error->Column == 32, "the error is at " + std::to_string(error->Line) + "," + std::to_string(error->Column) + ", expected 13,32");
		Require(error->Code == 30015 && error->Message.find("Tilling") != std::string::npos, "unexpected message: " + error->ToString());
		Require(error->ToString() == "Broken.slang(13,32): error 30015: " + error->Message, "ToString() is " + error->ToString());
	}

	void TestGlslDiagnostics()
	{
		const ShaderCompileResult result = Compiler().CompileProgram(TestShaders / "Broken.frag");
		Require(!result.bSucceeded, "Broken.frag compiled");
		const ShaderDiagnostic* error = FirstError(result);
		Require(error != nullptr, "Broken.frag has no error");
		Require(error->File.filename() == "Broken.frag", "the error names " + error->File.string() + ":" + DescribeDiagnostics(result));
		Require(error->Line == 9 && error->Column == 18, "the error is at " + std::to_string(error->Line) + "," + std::to_string(error->Column) + ", expected 9,18");
		Require(error->Message.find("colr") != std::string::npos, "unexpected message: " + error->ToString());
	}

	void TestMaterialMistakesAreNamed()
	{
		const ShaderCompileResult misnamed = Compiler().CompileMaterial(TestShaders / "Misnamed.slang", ShaderDomain::Surface());
		Require(!misnamed.bSucceeded, "Misnamed compiled");
		const ShaderDiagnostic* error = FirstError(misnamed);
		Require(error && error->Message.find("no struct named Misnamed") != std::string::npos, "unexpected message:" + DescribeDiagnostics(misnamed));

		const ShaderCompileResult missing = Compiler().CompileMaterial(TestShaders / "Missing.slang", ShaderDomain::Surface());
		Require(!missing.bSucceeded && FirstError(missing) && FirstError(missing)->Message == "file not found", "a missing file isn't reported as such");

		const ShaderCompileResult wrongName = Compiler().CompileMaterial(TestShaders / "Not a name.slang", ShaderDomain::Surface());
		Require(!wrongName.bSucceeded && FirstError(wrongName), "a file name that can't be a struct isn't reported");

		// Slang 2026.1 crashes generating code for it: refused before, with its layout still read
		for (const ShaderCompileResult& withArray : { Compiler().CompileMaterial(TestShaders / "WithArray.slang", ShaderDomain::Surface()),
				 Compiler().CompileDefaultsWriter(TestShaders / "WithArray.slang") })
		{
			Require(!withArray.bSucceeded, "a material with an array compiled");
			const ShaderDiagnostic* arrayError = FirstError(withArray);
			Require(arrayError && arrayError->Message.find("Weights is an array") == 0, "unexpected message:" + DescribeDiagnostics(withArray));
			Require(withArray.MaterialLayout && withArray.MaterialLayout->FindField("Weights")->ArrayCount == 3, "the array's layout isn't read");
		}
	}

	void TestDependencies()
	{
		const ShaderCompileResult stone = Compiler().CompileMaterial(TestShaders / "Stone.slang", ShaderDomain::Surface());
		Require(HasFile(stone.Dependencies, "Stone.slang") && HasFile(stone.Dependencies, "surface.slang"), "Stone's dependencies miss its file or nyx/surface.slang");

		// A broken file still lists what it depends on, so fixing it reloads it
		const ShaderCompileResult broken = Compiler().CompileMaterial(TestShaders / "Broken.slang", ShaderDomain::Surface());
		Require(HasFile(broken.Dependencies, "Broken.slang"), "Broken's dependencies miss its file");

		const ShaderCompileResult includes = Compiler().CompileProgram(TestShaders / "Includes.frag");
		RequireSucceeded(includes, "Includes.frag");
		Require(HasFile(includes.Dependencies, "Includes.frag") && HasFile(includes.Dependencies, "Common.glsl"), "Includes.frag's dependencies miss its file or Common.glsl");
	}

	// Every engine shader compiles with Slang, and reads its inputs and buffers exactly as glslc's
	// SPIR-V does. Slang's matrix setting for GLSL once flipped every matrix; this catches that.
	void TestEngineShadersMatchGlslc()
	{
		int compared = 0;
		for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(EngineShaders))
		{
			const std::string extension = entry.path().extension().string();
			if (extension != ".vert" && extension != ".frag" && extension != ".comp")
			{
				continue;
			}
			const std::string name = entry.path().filename().string();
			const ShaderCompileResult result = Compiler().CompileProgram(entry.path());
			RequireSucceeded(result, name);
			Require(result.EntryPoints.size() == 1 && result.EntryPoints[0].Name == "main", name + " has no entry point main");
			const EShaderStage stage = extension == ".vert" ? EShaderStage::Vertex : extension == ".frag" ? EShaderStage::Fragment : EShaderStage::Compute;
			Require(result.EntryPoints[0].Stage == stage, name + " has the wrong stage");

			const std::string problem = ValidateSpirv(result.EntryPoints[0].Spirv, false);
			Require(problem.empty(), name + " isn't valid SPIR-V: " + problem);

			const std::vector<uint32_t> glslc = ReadSpirvFile(GlslcShaders / (name + ".spv"));
			const std::vector<std::string> ours = InterfaceDecorations(result.EntryPoints[0].Spirv);
			const std::vector<std::string> theirs = InterfaceDecorations(glslc);
			if (ours != theirs)
			{
				std::string difference;
				for (const std::string& decoration : ours)
				{
					if (!std::binary_search(theirs.begin(), theirs.end(), decoration))
					{
						difference += "\n  only Slang: " + decoration;
					}
				}
				for (const std::string& decoration : theirs)
				{
					if (!std::binary_search(ours.begin(), ours.end(), decoration))
					{
						difference += "\n  only glslc: " + decoration;
					}
				}
				Require(false, name + ": Slang's offsets, matrices, bindings or locations differ from glslc's:" + difference);
			}

			// Nothing the device doesn't turn on: what glslc's code needs, and DemoteToHelperInvocation
			// (discard, in SPIR-V 1.6), which VulkanContext enables
			constexpr uint32_t DemoteToHelperInvocation = 5379;
			const std::vector<uint32_t> glslcCapabilities = Capabilities(glslc);
			for (uint32_t capability : Capabilities(result.EntryPoints[0].Spirv))
			{
				const bool bAllowed = capability == DemoteToHelperInvocation ||
					std::find(glslcCapabilities.begin(), glslcCapabilities.end(), capability) != glslcCapabilities.end();
				Require(bAllowed, name + ": Slang's code needs SPIR-V capability " + std::to_string(capability) + ", which glslc's doesn't and the device may not turn on");
			}
			++compared;
		}
		Require(compared >= 19, "only " + std::to_string(compared) + " engine shaders were compared");
	}

	void TestDefaultsWriterCompiles()
	{
		for (const char* material : { "Stone.slang", "NoDefault.slang" })
		{
			const ShaderCompileResult result = Compiler().CompileDefaultsWriter(TestShaders / material);
			RequireSucceeded(result, std::string("the defaults writer of ") + material);
			Require(result.EntryPoints.size() == 1 && result.EntryPoints[0].Name == "writeDefaults" && result.EntryPoints[0].Stage == EShaderStage::Compute,
				std::string("the defaults writer of ") + material + " has no compute entry point writeDefaults");
			Require(DescriptorBindings(result.EntryPoints[0].Spirv) == std::vector<std::string>{ "0.3" }, "the defaults writer doesn't write to 0.3 alone");
		}
	}

	void TestDiagnosticsParsing()
	{
		const std::string text =
			"C:/Shaders/A.slang(4): warning 30081: implicit conversion\n"
			"    float x = y;\n"
			"              ^\n"
			"C:/Shaders/nyx/surface.slang(16): note: see declaration of 'TextureHeap'\n"
			"(0): error 99999: Slang compilation aborted\n";
		const std::vector<ShaderDiagnostic> diagnostics = ParseSlangDiagnostics(text);
		Require(diagnostics.size() == 3, "parsed " + std::to_string(diagnostics.size()) + " diagnostics, expected 3");
		Require(diagnostics[0].Severity == EShaderDiagnosticSeverity::Warning && diagnostics[0].Line == 4 && diagnostics[0].Column == 15 && diagnostics[0].Code == 30081,
			"the warning is parsed wrongly: " + diagnostics[0].ToString());
		Require(diagnostics[1].Severity == EShaderDiagnosticSeverity::Note && diagnostics[1].Code == 0 && diagnostics[1].Column == 0, "the note is parsed wrongly: " + diagnostics[1].ToString());
		Require(diagnostics[2].Severity == EShaderDiagnosticSeverity::Error && diagnostics[2].File.empty() && diagnostics[2].Code == 99999, "the fileless error is parsed wrongly");
	}
}

int main()
{
	try
	{
		TestCompilerStarts();
		TestMaterialCompilesIntoItsDomain();
		TestMaterialLayout();
		TestSlangDiagnostics();
		TestGlslDiagnostics();
		TestMaterialMistakesAreNamed();
		TestDependencies();
		TestEngineShadersMatchGlslc();
		TestDefaultsWriterCompiles();
		TestDiagnosticsParsing();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shader compiler tests passed\n";
	return 0;
}
