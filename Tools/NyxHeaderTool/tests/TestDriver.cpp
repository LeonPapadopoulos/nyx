#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <process.h>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <windows.h>

namespace fs = std::filesystem;

static std::string ReadAllText(const fs::path& path)
{
	std::ifstream file(path, std::ios::in | std::ios::binary);
	if (!file)
	{
		throw std::runtime_error("Failed to open file: " + path.string());
	}

	std::ostringstream stream;
	stream << file.rdbuf();
	return stream.str();
}

static void RequireFileEquals(const fs::path& actualPath, const fs::path& expectedPath, const fs::path& sourcePath = {})
{
	const std::string actual = ReadAllText(actualPath);
	std::string expected = ReadAllText(expectedPath);
	// Source locations are absolute in a build, but golden files must work in any checkout.
	const std::string sourceFile = sourcePath.empty() ? "" : fs::absolute(sourcePath).generic_string();
	const std::string marker = "@SOURCE_FILE@";
	for (size_t position = expected.find(marker); position != std::string::npos; position = expected.find(marker, position + sourceFile.size()))
	{
		expected.replace(position, marker.size(), sourceFile);
	}

	if (actual != expected)
	{
		std::cerr << "Mismatch:\n";
		std::cerr << "  Actual:   " << actualPath.string() << "\n";
		std::cerr << "  Expected: " << expectedPath.string() << "\n";
		throw std::runtime_error("Generated file does not match expected output.");
	}
}

static std::wstring ToWide(const std::string& text)
{
	if (text.empty())
	{
		return {};
	}

	const int required = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
	if (required <= 0)
	{
		throw std::runtime_error("Failed converting UTF-8 string to wide string.");
	}

	std::wstring result(static_cast<size_t>(required - 1), L'\0');

	const int written = MultiByteToWideChar(
		CP_UTF8,
		0,
		text.c_str(),
		-1,
		result.data(),
		required);

	if (written <= 0)
	{
		throw std::runtime_error("Failed writing converted UTF-8 wide string.");
	}

	return result;
}

static std::wstring ToWide(const fs::path& path)
{
	return path.wstring();
}

// A fixture names its module in Module.txt; without one the tool is run without --module-name,
// so the engine's default, Runtime, stays tested too.
static std::optional<std::string> ReadModuleName(const fs::path& fixtureDir)
{
	const fs::path moduleFile = fixtureDir / "Module.txt";
	if (!fs::exists(moduleFile))
	{
		return std::nullopt;
	}

	std::string moduleName = ReadAllText(moduleFile);
	while (!moduleName.empty() && std::isspace(static_cast<unsigned char>(moduleName.back())))
	{
		moduleName.pop_back();
	}
	return moduleName;
}

static int RunTool(
	const fs::path& toolPath,
	const fs::path& fixtureDir,
	const fs::path& caseOutputDir,
	const std::optional<std::string>& moduleName)
{
	const std::string fileStem = moduleName.value_or("Runtime");
	const std::wstring tool = ToWide(toolPath);
	const std::wstring scanRoot = ToWide(fixtureDir);
	const std::wstring outputDir = ToWide(caseOutputDir);

	std::wcout << L"Tool:    " << tool << L"\n";
	std::wcout << L"Fixture: " << scanRoot << L"\n";
	std::wcout << L"Output:  " << outputDir << L"\n";

	std::vector<std::wstring> ownedArgs = {
		tool,
		L"--scan-root", scanRoot,
		L"--output-dir", outputDir,
		L"--module-init-header", ToWide(caseOutputDir / (fileStem + ".reflect.init.h")),
		L"--module-init-cpp", ToWide(caseOutputDir / (fileStem + ".reflect.init.cpp")),
		L"--editor-sources-cpp", ToWide(caseOutputDir / (fileStem + ".reflect.sources.cpp"))
	};

	if (moduleName.has_value())
	{
		ownedArgs.push_back(L"--module-name");
		ownedArgs.push_back(ToWide(moduleName.value()));
	}

	std::vector<const wchar_t*> argv;
	argv.reserve(ownedArgs.size() + 1);

	for (const std::wstring& arg : ownedArgs)
	{
		argv.push_back(arg.c_str());
	}
	argv.push_back(nullptr);

	return _wspawnv(_P_WAIT, tool.c_str(), argv.data());
}

static void RunFixture(
	const fs::path& toolPath,
	const fs::path& fixtureDir,
	const std::string& relativeOutputName)
{
	const fs::path inputHeader = fixtureDir / "Input.h";
	const fs::path expectedReflect = fixtureDir / "Expected.reflect.h";
	const fs::path expectedInitHeader = fixtureDir / "Expected.init.h";
	const fs::path expectedInitCpp = fixtureDir / "Expected.init.cpp";

	const fs::path tempRoot = fs::temp_directory_path() / "NyxHeaderToolTests";
	const fs::path caseOutputDir = tempRoot / fixtureDir.filename();

	fs::remove_all(caseOutputDir);
	fs::create_directories(caseOutputDir);

	const std::optional<std::string> moduleName = ReadModuleName(fixtureDir);
	const std::string fileStem = moduleName.value_or("Runtime");

	const fs::path actualReflect = caseOutputDir / relativeOutputName;
	const fs::path actualInitHeader = caseOutputDir / (fileStem + ".reflect.init.h");
	const fs::path actualInitCpp = caseOutputDir / (fileStem + ".reflect.init.cpp");
	const fs::path actualSourcesCpp = caseOutputDir / (fileStem + ".reflect.sources.cpp");

	if (!fs::exists(inputHeader))
	{
		throw std::runtime_error("Fixture missing Input.h: " + fixtureDir.string());
	}

	const int exitCode = RunTool(toolPath, fixtureDir, caseOutputDir, moduleName);

	if (exitCode != 0)
	{
		throw std::runtime_error(
			"NyxHeaderTool returned non-zero exit code for fixture: " + fixtureDir.string());
	}

	if (!fs::exists(actualReflect))
	{
		throw std::runtime_error("Expected generated reflect file missing: " + actualReflect.string());
	}

	if (!fs::exists(actualInitHeader))
	{
		throw std::runtime_error("Expected generated init header missing: " + actualInitHeader.string());
	}

	if (!fs::exists(actualInitCpp))
	{
		throw std::runtime_error("Expected generated init cpp missing: " + actualInitCpp.string());
	}

	RequireFileEquals(actualReflect, expectedReflect, inputHeader);
	RequireFileEquals(actualInitHeader, expectedInitHeader);
	RequireFileEquals(actualInitCpp, expectedInitCpp);
	RequireFileEquals(actualSourcesCpp, fixtureDir / "Expected.sources.cpp", inputHeader);
}

// Runs the tool on a fixture it must reject, for example because two property names have the same
// hash, or its module name is no C++ identifier.
static void RunFailingFixture(const fs::path& toolPath, const fs::path& fixtureDir, const std::string& inputHeaderName)
{
	// Without its header, the tool would fail for the wrong reason and the test would still pass
	if (!fs::exists(fixtureDir / inputHeaderName))
	{
		throw std::runtime_error("Fixture missing " + inputHeaderName + ": " + fixtureDir.string());
	}

	const fs::path caseOutputDir = fs::temp_directory_path() / "NyxHeaderToolTests" / fixtureDir.filename();

	fs::remove_all(caseOutputDir);
	fs::create_directories(caseOutputDir);

	const int exitCode = RunTool(toolPath, fixtureDir, caseOutputDir, ReadModuleName(fixtureDir));

	if (exitCode == 0)
	{
		throw std::runtime_error("NyxHeaderTool accepted a fixture it must reject: " + fixtureDir.string());
	}
}

int main(int argc, char** argv)
{
	try
	{
		if (argc != 3)
		{
			std::cerr << "Usage: NyxHeaderToolTests <path-to-NyxHeaderTool> <fixtures-root>\n";
			return 1;
		}

		const fs::path toolPath = argv[1];
		const fs::path fixturesRoot = argv[2];

		if (!fs::exists(toolPath))
		{
			throw std::runtime_error("NyxHeaderTool executable not found: " + toolPath.string());
		}

		if (!fs::exists(fixturesRoot))
		{
			throw std::runtime_error("Fixtures root not found: " + fixturesRoot.string());
		}

		RunFixture(toolPath, fixturesRoot / "basic", "Input.reflect.h");
		RunFixture(toolPath, fixturesRoot / "multi_type", "Input.reflect.h");
		RunFixture(toolPath, fixturesRoot / "nested_namespace", "Input.reflect.h");
		RunFixture(toolPath, fixturesRoot / "nested_type", "Input.reflect.h");
		RunFixture(toolPath, fixturesRoot / "guid_property", "Input.reflect.h");
		RunFixture(toolPath, fixturesRoot / "module_name", "Input.reflect.h");
		RunFailingFixture(toolPath, fixturesRoot / "name_hash_collision", "Collision.h");
		RunFailingFixture(toolPath, fixturesRoot / "invalid_module_name", "Invalid.h");

		std::cout << "All NyxHeaderTool fixtures passed.\n";
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Test failure: " << ex.what() << "\n";
		return 1;
	}
}
