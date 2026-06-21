#include <filesystem>
#include <fstream>
#include <iostream>
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

static void RequireFileEquals(const fs::path& actualPath, const fs::path& expectedPath)
{
	const std::string actual = ReadAllText(actualPath);
	const std::string expected = ReadAllText(expectedPath);

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

static int RunTool(
	const fs::path& toolPath,
	const fs::path& fixtureDir,
	const fs::path& caseOutputDir,
	const fs::path& actualInitHeader,
	const fs::path& actualInitCpp)
{
	const std::wstring tool = ToWide(toolPath);
	const std::wstring scanRoot = ToWide(fixtureDir);
	const std::wstring outputDir = ToWide(caseOutputDir);
	const std::wstring initHeader = ToWide(actualInitHeader);
	const std::wstring initCpp = ToWide(actualInitCpp);

	std::wcout << L"Tool:    " << tool << L"\n";
	std::wcout << L"Fixture: " << scanRoot << L"\n";
	std::wcout << L"Output:  " << outputDir << L"\n";

	std::vector<std::wstring> ownedArgs =
	{
		tool,
		L"--scan-root", scanRoot,
		L"--output-dir", outputDir,
		L"--module-init-header", initHeader,
		L"--module-init-cpp", initCpp
	};

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

	const fs::path actualReflect = caseOutputDir / relativeOutputName;
	const fs::path actualInitHeader = caseOutputDir / "Runtime.reflect.init.h";
	const fs::path actualInitCpp = caseOutputDir / "Runtime.reflect.init.cpp";

	if (!fs::exists(inputHeader))
	{
		throw std::runtime_error("Fixture missing Input.h: " + fixtureDir.string());
	}

	const int exitCode = RunTool(
		toolPath,
		fixtureDir,
		caseOutputDir,
		actualInitHeader,
		actualInitCpp);

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

	RequireFileEquals(actualReflect, expectedReflect);
	RequireFileEquals(actualInitHeader, expectedInitHeader);
	RequireFileEquals(actualInitCpp, expectedInitCpp);
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

		std::cout << "All NyxHeaderTool fixtures passed.\n";
		return 0;
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Test failure: " << ex.what() << "\n";
		return 1;
	}
}