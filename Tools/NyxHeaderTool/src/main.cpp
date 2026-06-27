#include "CodeGenerator.h"
#include "Lexer.h"
#include "Parser.h"
#include "ReflectionSemantics.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace fs = std::filesystem;
using namespace Nyx::HeaderTool;

static std::string ReadAllText(const fs::path& path)
{
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Failed to open input file: " + path.string());
	}

	std::ostringstream stream;
	stream << file.rdbuf();
	return stream.str();
}

static void WriteAllText(const fs::path& path, const std::string& text)
{
	fs::create_directories(path.parent_path());

	std::ofstream file(path, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!file)
	{
		throw std::runtime_error("Failed to open output file: " + path.string());
	}

	file << text;
}

static bool IsHeaderFile(const fs::path& path)
{
	const std::string ext = path.extension().string();
	return ext == ".h" || ext == ".hpp";
}

static bool MightContainReflection(std::string_view text)
{
	return text.find("NYX_REFLECT") != std::string_view::npos;
}

static ParsedHeader ParseReflectedHeaderText(const std::string& text, const fs::path& sourcePath)
{
	Lexer lexer(text);
	Parser parser(lexer.LexAll(), sourcePath.string());

	ParsedHeader parsedHeader = parser.ParseHeader();
	ReflectionSemantics::Apply(parsedHeader);
	return parsedHeader;
}

static void GenerateForSingleHeader(const fs::path& inputHeader, const fs::path& outputHeader)
{
	const std::string source = ReadAllText(inputHeader);
	const ParsedHeader parsedHeader = ParseReflectedHeaderText(source, inputHeader);

	if (parsedHeader.Types.empty())
	{
		throw std::runtime_error("No NYX_REFLECT() types found in: " + inputHeader.string());
	}

	WriteAllText(outputHeader, CodeGenerator::GenerateMetadataHeader(parsedHeader));
	std::cout << "Generated: " << outputHeader.string() << "\n";
}

static size_t GenerateForScanRoots(
	const std::vector<fs::path>& scanRoots,
	const fs::path& outputDir,
	std::vector<ScannedHeader>& outScannedHeaders)
{
	size_t generatedCount = 0;
	std::unordered_set<std::string> processedHeaders;

	for (const fs::path& root : scanRoots)
	{
		if (!fs::exists(root))
		{
			throw std::runtime_error("Scan root does not exist: " + root.string());
		}

		for (const fs::directory_entry& entry : fs::recursive_directory_iterator(root))
		{
			if (!entry.is_regular_file())
			{
				continue;
			}

			const fs::path headerPath = entry.path();
			if (!IsHeaderFile(headerPath))
			{
				continue;
			}

			const std::string canonicalHeaderPath = fs::weakly_canonical(headerPath).string();
			if (processedHeaders.contains(canonicalHeaderPath))
			{
				continue;
			}

			const std::string source = ReadAllText(headerPath);
			if (!MightContainReflection(source))
			{
				continue;
			}

			const ParsedHeader parsedHeader = ParseReflectedHeaderText(source, headerPath);
			if (parsedHeader.Types.empty())
			{
				continue;
			}

			const fs::path relativePath = fs::relative(headerPath, root);
			const fs::path outputHeader = CodeGenerator::MakeGeneratedHeaderPath(relativePath, outputDir);

			WriteAllText(outputHeader, CodeGenerator::GenerateMetadataHeader(parsedHeader));
			std::cout << "Generated: " << outputHeader.string() << "\n";

			outScannedHeaders.push_back(ScannedHeader{
				.HeaderPath = headerPath,
				.RelativePath = relativePath,
				.Parsed = parsedHeader
				});

			processedHeaders.insert(canonicalHeaderPath);
			++generatedCount;
		}
	}

	return generatedCount;
}

static int RunSingleHeaderMode(int argc, char** argv)
{
	if (argc != 4)
	{
		std::cerr << "Usage: NyxHeaderTool <input_header> <output_header> <qualified_type_name>\n";
		return 1;
	}

	const fs::path inputHeader = argv[1];
	const fs::path outputHeader = argv[2];
	(void)argv[3];

	GenerateForSingleHeader(inputHeader, outputHeader);
	return 0;
}

static int RunScanMode(int argc, char** argv)
{
	std::vector<fs::path> scanRoots;
	std::optional<fs::path> outputDir;
	std::optional<fs::path> moduleInitHeader;
	std::optional<fs::path> moduleInitCpp;

	for (int i = 1; i < argc; ++i)
	{
		const std::string arg = argv[i];

		if (arg == "--scan-root")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--scan-root requires a value");
			}
			scanRoots.emplace_back(argv[++i]);
		}
		else if (arg == "--output-dir")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--output-dir requires a value");
			}
			outputDir = fs::path(argv[++i]);
		}
		else if (arg == "--module-init-header")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--module-init-header requires a value");
			}
			moduleInitHeader = fs::path(argv[++i]);
		}
		else if (arg == "--module-init-cpp")
		{
			if (i + 1 >= argc)
			{
				throw std::runtime_error("--module-init-cpp requires a value");
			}
			moduleInitCpp = fs::path(argv[++i]);
		}
		else
		{
			throw std::runtime_error("Unknown argument: " + arg);
		}
	}

	if (scanRoots.empty())
	{
		throw std::runtime_error("No --scan-root arguments were provided.");
	}

	if (!outputDir.has_value())
	{
		throw std::runtime_error("Missing --output-dir argument.");
	}

	std::vector<ScannedHeader> scannedHeaders;
	const size_t count = GenerateForScanRoots(scanRoots, outputDir.value(), scannedHeaders);

	if (moduleInitHeader.has_value())
	{
		WriteAllText(moduleInitHeader.value(), CodeGenerator::GenerateModuleInitHeader());
		std::cout << "Generated: " << moduleInitHeader.value().string() << "\n";
	}

	if (moduleInitCpp.has_value())
	{
		WriteAllText(moduleInitCpp.value(), CodeGenerator::GenerateModuleInitCpp(scannedHeaders));
		std::cout << "Generated: " << moduleInitCpp.value().string() << "\n";
	}

	std::cout << "Reflection generation complete. Generated " << count << " header(s).\n";
	return 0;
}

int main(int argc, char** argv)
{
	try
	{
		if (argc <= 1)
		{
			std::cerr << "Usage:\n";
			std::cerr << "  NyxHeaderTool <input_header> <output_header> <qualified_type_name>\n";
			std::cerr << "  NyxHeaderTool --scan-root <dir> [--scan-root <dir> ...] --output-dir <dir>\n";
			std::cerr << "                [--module-init-header <file>] [--module-init-cpp <file>]\n";
			return 1;
		}

		if (std::string_view(argv[1]) == "--scan-root")
		{
			return RunScanMode(argc, argv);
		}

		return RunSingleHeaderMode(argc, argv);
	}
	catch (const std::exception& ex)
	{
		std::cerr << "NyxHeaderTool error: " << ex.what() << "\n";
		return 1;
	}
}