// Material files without a GPU: a .nyxmat file reads and writes back as it was, comments and
// values the shader doesn't know included; its values land at the offsets reflection gives,
// over the shader's defaults; after the shader's fields change, values are found again by name,
// and those that no longer fit are left out and listed; textures take their slot, their
// Default(...) slot when unset, and the missing texture's when they can't be loaded; a [Linear]
// texture is asked for as linear.
#include "BindlessTextures.h"
#include "MaterialAsset.h"
#include "Paths.h"
#include "ShaderCompiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using namespace Nyx::Engine;

	const std::filesystem::path TestShaders = NYX_TEST_SHADERS_DIR;

	void Require(bool condition, const std::string& message)
	{
		if (!condition)
		{
			throw std::runtime_error(message);
		}
	}

	ShaderCompiler& Compiler()
	{
		static ShaderCompiler compiler({ Nyx::Paths::GetShaderLibraryDir() });
		return compiler;
	}

	ShaderLayout LayoutOf(const std::filesystem::path& file)
	{
		const ShaderCompileResult result = Compiler().CompileMaterial(file, ShaderDomain::Surface());
		std::string diagnostics;
		for (const ShaderDiagnostic& diagnostic : result.Diagnostics)
		{
			diagnostics += "\n  " + diagnostic.ToString();
		}
		Require(result.bSucceeded && result.MaterialLayout.has_value(), file.filename().string() + " doesn't compile as a material:" + diagnostics);
		return *result.MaterialLayout;
	}

	MaterialAsset Parsed(const std::string& text)
	{
		MaterialAsset asset;
		std::string error;
		Require(asset.Parse(text, error), "the material doesn't parse: " + error);
		return asset;
	}

	uint32_t WordAt(const std::vector<std::byte>& bytes, uint32_t offset)
	{
		uint32_t word = 0;
		std::memcpy(&word, bytes.data() + offset, sizeof(word));
		return word;
	}

	float FloatAt(const std::vector<std::byte>& bytes, uint32_t offset)
	{
		float value = 0.0f;
		std::memcpy(&value, bytes.data() + offset, sizeof(value));
		return value;
	}

	uint32_t OffsetOf(const ShaderLayout& layout, const char* name)
	{
		const ShaderField* field = layout.FindField(name);
		Require(field != nullptr, std::string("the layout has no field ") + name);
		return field->Offset;
	}

	bool HasProblem(const std::vector<MaterialValueProblem>& problems, const std::string& name)
	{
		return std::any_of(problems.begin(), problems.end(), [&name](const MaterialValueProblem& problem)
			{
				return problem.Name == name;
			});
	}

	// "Defaults" that no value written over them could have
	constexpr std::byte Untouched{ 0xCD };
	constexpr uint32_t UntouchedWord = 0xCDCDCDCDu;

	struct TempFolder
	{
		std::filesystem::path Path = std::filesystem::temp_directory_path() / ("NyxMaterialTests-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));

		TempFolder()
		{
			std::filesystem::create_directories(Path);
		}

		~TempFolder()
		{
			std::error_code error;
			std::filesystem::remove_all(Path, error);
		}

		std::filesystem::path Write(const std::string& name, const std::string& text) const
		{
			std::ofstream stream(Path / name, std::ios::binary | std::ios::trunc);
			stream << text;
			return Path / name;
		}
	};

	void TestFilesReadAndWriteBackAsTheyWere()
	{
		const std::string text = "# A stone, wet\n"
								 "Shader    = Shaders/Stone.slang\n"
								 "\n"
								 "BaseColor = 0.62 0.60 0.56\n"
								 "Shinyness = 3  # a value the shader doesn't know\n"
								 "Roughness=0.78\n";
		MaterialAsset asset = Parsed(text);
		Require(asset.ToText() == text, "the file doesn't write back as it was:\n" + asset.ToText());
		Require(asset.GetShader() == "Shaders/Stone.slang", "the shader is " + asset.GetShader());
		Require(asset.FindValue("Roughness") && *asset.FindValue("Roughness") == "0.78", "Roughness isn't read");
		Require(asset.GetValues().size() == 3, "the shader counts as a value, or values are missing");

		// Setting a value changes its line alone; a new one goes at the end
		asset.SetValue("Roughness", "0.5");
		asset.SetValue("Wetness", "0.3");
		Require(asset.ToText() == "# A stone, wet\n"
								  "Shader    = Shaders/Stone.slang\n"
								  "\n"
								  "BaseColor = 0.62 0.60 0.56\n"
								  "Shinyness = 3  # a value the shader doesn't know\n"
								  "Roughness = 0.5\n"
								  "Wetness = 0.3\n",
			"setting values changed more than their lines:\n" + asset.ToText());
		Require(asset.RemoveValue("BaseColor") && !asset.FindValue("BaseColor") && !asset.RemoveValue("BaseColor"), "a value can't be removed");

		// Saved and loaded again
		TempFolder folder;
		std::string error;
		Require(asset.Save(folder.Path / "Stone.nyxmat", error), "the material can't be saved: " + error);
		MaterialAsset loaded;
		Require(loaded.Load(folder.Path / "Stone.nyxmat", error), "the material can't be loaded: " + error);
		Require(loaded.ToText() == asset.ToText(), "the loaded material differs from the saved one");

		// Windows line ends read the same
		Require(Parsed("Shader = A.slang\r\nRoughness = 1\r\n").FindValue("Roughness")->size() == 1, "a value keeps its line's \\r");
	}

	void TestMistakesInFilesAreNamed()
	{
		MaterialAsset asset;
		std::string error;
		Require(!asset.Parse("Shader = Shaders/Stone.slang\n\nRoughness 0.5\n", error), "a line without '=' is accepted");
		Require(error.find("line 3") != std::string::npos, "the error doesn't name line 3: " + error);
		Require(!asset.Parse("= 0.5\n", error), "a value without a name is accepted");
		Require(!asset.Load(std::filesystem::temp_directory_path() / "NyxMaterialTests-missing.nyxmat", error), "a missing file loads");
	}

	// Stone's values over defaults that none of them has, at the offsets reflection gives
	void TestValuesLandAtTheirOffsets()
	{
		const ShaderLayout layout = LayoutOf(TestShaders / "Stone.slang");
		const MaterialAsset asset = Parsed("Shader = Shaders/Stone.slang\n"
										   "BaseColor = 0.25, 0.5, 0.75\n"
										   "Roughness = 0.125\n"
										   "Albedo = Textures/Stone.png\n"
										   "Tiling = 2 3\n"
										   "Mode = -4\n"
										   "Shinyness = 3\n");
		std::vector<std::byte> bytes(layout.Size, Untouched);
		std::vector<std::string> asked;
		const std::vector<MaterialValueProblem> problems = WriteMaterialValues(layout, asset, bytes, [&asked](const std::string& path, bool) -> std::optional<uint32_t>
			{
				asked.push_back(path);
				return 42u;
			});

		const uint32_t baseColor = OffsetOf(layout, "BaseColor");
		Require(FloatAt(bytes, baseColor) == 0.25f && FloatAt(bytes, baseColor + 4) == 0.5f && FloatAt(bytes, baseColor + 8) == 0.75f, "BaseColor isn't at its offset");
		Require(FloatAt(bytes, OffsetOf(layout, "Roughness")) == 0.125f, "Roughness isn't at its offset");
		Require(WordAt(bytes, OffsetOf(layout, "Wetness")) == UntouchedWord, "Wetness, which the file doesn't set, lost its default");
		Require(WordAt(bytes, OffsetOf(layout, "Albedo")) == 42u && asked == std::vector<std::string>{ "Textures/Stone.png" }, "Albedo doesn't hold its texture's slot");
		const uint32_t tiling = OffsetOf(layout, "Tiling");
		Require(FloatAt(bytes, tiling) == 2.0f && FloatAt(bytes, tiling + 4) == 3.0f, "Tiling isn't at its offset");
		Require(static_cast<int32_t>(WordAt(bytes, OffsetOf(layout, "Mode"))) == -4, "Mode isn't written as an int");

		Require(problems.size() == 1 && problems[0].Name == "Shinyness", "only the value the shader doesn't know is a problem");
		Require(Parsed(asset.ToText()).FindValue("Shinyness"), "the file lost the value the shader doesn't know");
	}

	// Values that don't fit their field are left out and listed; the defaults stay
	void TestValuesThatDontFitAreListed()
	{
		const ShaderLayout layout = LayoutOf(TestShaders / "Stone.slang");
		const MaterialAsset asset = Parsed("Shader = Shaders/Stone.slang\n"
										   "BaseColor = 1 2\n"
										   "Roughness = rough\n"
										   "Mode = 1.5\n");
		std::vector<std::byte> bytes(layout.Size, Untouched);
		const std::vector<MaterialValueProblem> problems = WriteMaterialValues(layout, asset, bytes, nullptr);
		Require(problems.size() == 3 && HasProblem(problems, "BaseColor") && HasProblem(problems, "Roughness") && HasProblem(problems, "Mode"), "a value that doesn't fit isn't listed");
		// Albedo takes its Default("White") slot; nothing else is written
		const uint32_t albedo = OffsetOf(layout, "Albedo");
		Require(WordAt(bytes, albedo) == static_cast<uint32_t>(EBindlessTexture::White), "the unset Albedo isn't white");
		for (uint32_t offset = 0; offset < layout.Size; ++offset)
		{
			Require(offset - albedo < 4 || bytes[offset] == Untouched, "a value that doesn't fit was written anyway, at byte " + std::to_string(offset));
		}
	}

	// After the shader changes its fields, the file's values are found again by name
	void TestValuesFollowTheirNamesAcrossLayouts()
	{
		TempFolder folder;
		const std::string shade = "    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }\n";
		const ShaderLayout before = LayoutOf(folder.Write("Probe.slang", "import nyx.surface;\nstruct Probe : ISurface\n{\n    float A = 1.0;\n    float3 B = float3(0.0);\n" + shade + "}\n"));
		// Fields reordered, one gone, one new, one of another type
		const ShaderLayout after = LayoutOf(folder.Write("Probe.slang", "import nyx.surface;\nstruct Probe : ISurface\n{\n    int2 A = int2(0, 0);\n    float C = 5.0;\n    float3 B = float3(0.0);\n" + shade + "}\n"));
		Require(OffsetOf(before, "B") != OffsetOf(after, "B"), "B didn't move; the test proves nothing");

		const MaterialAsset asset = Parsed("Shader = Shaders/Probe.slang\nA = 0.5\nB = 1 2 3\n");
		std::vector<std::byte> bytes(after.Size, Untouched);
		const std::vector<MaterialValueProblem> problems = WriteMaterialValues(after, asset, bytes, nullptr);
		const uint32_t b = OffsetOf(after, "B");
		Require(FloatAt(bytes, b) == 1.0f && FloatAt(bytes, b + 4) == 2.0f && FloatAt(bytes, b + 8) == 3.0f, "B isn't at its new offset");
		Require(WordAt(bytes, OffsetOf(after, "C")) == UntouchedWord, "C, new and unset, lost its default");
		Require(problems.size() == 1 && problems[0].Name == "A", "A, now of another type, isn't the one problem");
		Require(WordAt(bytes, OffsetOf(after, "A")) == UntouchedWord, "A was written although it doesn't fit");
	}

	// An unset texture takes its Default(...) slot; one that can't be loaded the missing texture's
	void TestTextureSlots()
	{
		TempFolder folder;
		const ShaderLayout layout = LayoutOf(folder.Write("Maps.slang", "import nyx.surface;\nstruct Maps : ISurface\n{\n"
																		"    [Default(\"FlatNormal\")] nyx::Texture Normal;\n"
																		"    [Default(\"Black\")] nyx::Texture Glow;\n"
																		"    nyx::Texture Plain;\n"
																		"    [Linear] nyx::Texture Broken;\n"
																		"    SurfaceOutput shade(SurfaceInput input) { return SurfaceOutput::defaults(input); }\n}\n"));
		const MaterialAsset asset = Parsed("Shader = Shaders/Maps.slang\nBroken = Textures/Nowhere.png\n");
		std::vector<std::byte> bytes(layout.Size, Untouched);
		std::vector<std::pair<std::string, bool>> asked;
		const std::vector<MaterialValueProblem> problems = WriteMaterialValues(layout, asset, bytes, [&asked](const std::string& path, bool bLinear) -> std::optional<uint32_t>
			{
				asked.emplace_back(path, bLinear);
				return std::nullopt;
			});
		Require(asked == std::vector<std::pair<std::string, bool>>{ { "Textures/Nowhere.png", true } }, "the [Linear] texture isn't asked for as linear");
		Require(WordAt(bytes, OffsetOf(layout, "Normal")) == static_cast<uint32_t>(EBindlessTexture::FlatNormal), "an unset normal map isn't flat");
		Require(WordAt(bytes, OffsetOf(layout, "Glow")) == static_cast<uint32_t>(EBindlessTexture::Black), "Default(\"Black\") isn't black");
		Require(WordAt(bytes, OffsetOf(layout, "Plain")) == UntouchedWord, "a texture without a Default(...) lost the shader's default");
		Require(WordAt(bytes, OffsetOf(layout, "Broken")) == static_cast<uint32_t>(EBindlessTexture::Missing), "a texture that can't be loaded doesn't show as missing");
		Require(problems.size() == 1 && problems[0].Name == "Broken", "the texture that can't be loaded isn't listed");
	}
}

int main()
{
	try
	{
		TestFilesReadAndWriteBackAsTheyWere();
		TestMistakesInFilesAreNamed();
		TestValuesLandAtTheirOffsets();
		TestValuesThatDontFitAreListed();
		TestValuesFollowTheirNamesAcrossLayouts();
		TestTextureSlots();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All material tests passed\n";
	return 0;
}
