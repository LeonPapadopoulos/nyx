// Compiling in NyxShaderCompiler.exe (RemoteShaderCompileService) gives what compiling in this
// process gives; a crash of the compiler costs the request it was working on, and the next
// requests are answered by a new one; a compiler that can't start is reported. The protocol
// carries a whole result unchanged.
#include "BinaryArchive.h"
#include "Paths.h"
#include "ShaderCompileService.h"
#include "ShaderCompiler.h"
#include "TestSupport.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
	using namespace Nyx;
	using Clock = std::chrono::steady_clock;

	const std::filesystem::path TestShaders = NYX_TEST_SHADERS_DIR;

	using Nyx::Tests::Require;

	std::vector<std::filesystem::path> SearchPaths()
	{
		return { Nyx::Paths::GetShaderLibraryDir() };
	}

	std::filesystem::path CompilerProgram()
	{
		return Nyx::Paths::GetExecutableDir() / "NyxShaderCompiler.exe";
	}

	ShaderCompileRequest Request(uint32_t id, EShaderCompileKind kind, const std::filesystem::path& file)
	{
		ShaderCompileRequest request;
		request.Id = id;
		request.Kind = kind;
		request.File = file;
		if (kind == EShaderCompileKind::Material)
		{
			request.Domain = ShaderDomain::Surface();
		}
		return request;
	}

	// Takes results until `count` arrived, for at most 30 seconds (the first start loads Slang)
	std::vector<ShaderCompileResponse> WaitForResults(IShaderCompileService& service, size_t count)
	{
		std::vector<ShaderCompileResponse> results;
		const Clock::time_point deadline = Clock::now() + std::chrono::seconds(30);
		while (results.size() < count && Clock::now() < deadline)
		{
			for (ShaderCompileResponse& response : service.TakeResults())
			{
				results.push_back(std::move(response));
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		Require(results.size() == count, "got " + std::to_string(results.size()) + " results of " + std::to_string(count) + " within 30 seconds");
		return results;
	}

	bool SameDiagnostics(const ShaderCompileResult& a, const ShaderCompileResult& b)
	{
		if (a.Diagnostics.size() != b.Diagnostics.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.Diagnostics.size(); ++index)
		{
			if (a.Diagnostics[index].ToString() != b.Diagnostics[index].ToString())
			{
				return false;
			}
		}
		return true;
	}

	bool SameFields(const std::vector<ShaderField>& a, const std::vector<ShaderField>& b)
	{
		if (a.size() != b.size())
		{
			return false;
		}
		for (size_t index = 0; index < a.size(); ++index)
		{
			const ShaderField& x = a[index];
			const ShaderField& y = b[index];
			if (x.Name != y.Name || x.TypeName != y.TypeName || x.Kind != y.Kind || x.Scalar != y.Scalar || x.Rows != y.Rows || x.Columns != y.Columns ||
				x.Offset != y.Offset || x.Size != y.Size || x.ArrayCount != y.ArrayCount || x.ArrayStride != y.ArrayStride || x.ScalarDefault != y.ScalarDefault ||
				x.Attributes.size() != y.Attributes.size() || !SameFields(x.Fields, y.Fields))
			{
				return false;
			}
			for (size_t attribute = 0; attribute < x.Attributes.size(); ++attribute)
			{
				const ShaderAttribute& p = x.Attributes[attribute];
				const ShaderAttribute& q = y.Attributes[attribute];
				if (p.Name != q.Name || p.Arguments.size() != q.Arguments.size())
				{
					return false;
				}
				for (size_t argument = 0; argument < p.Arguments.size(); ++argument)
				{
					if (p.Arguments[argument].bText != q.Arguments[argument].bText || p.Arguments[argument].Text != q.Arguments[argument].Text ||
						p.Arguments[argument].Number != q.Arguments[argument].Number)
					{
						return false;
					}
				}
			}
		}
		return true;
	}

	// Everything a result holds, apart from the time it took
	void RequireSameResult(const ShaderCompileResult& a, const ShaderCompileResult& b, const std::string& what)
	{
		Require(a.bSucceeded == b.bSucceeded, what + ": success differs");
		Require(a.EntryPoints.size() == b.EntryPoints.size(), what + ": entry points differ");
		for (size_t index = 0; index < a.EntryPoints.size(); ++index)
		{
			Require(a.EntryPoints[index].Name == b.EntryPoints[index].Name && a.EntryPoints[index].Stage == b.EntryPoints[index].Stage &&
					a.EntryPoints[index].Spirv == b.EntryPoints[index].Spirv,
				what + ": entry point " + a.EntryPoints[index].Name + " differs");
		}
		Require(SameDiagnostics(a, b), what + ": diagnostics differ");
		Require(a.Dependencies == b.Dependencies, what + ": dependencies differ");
		Require(a.MaterialLayout.has_value() == b.MaterialLayout.has_value(), what + ": one has a layout, the other doesn't");
		if (a.MaterialLayout)
		{
			Require(a.MaterialLayout->TypeName == b.MaterialLayout->TypeName && a.MaterialLayout->Size == b.MaterialLayout->Size &&
					a.MaterialLayout->Alignment == b.MaterialLayout->Alignment && SameFields(a.MaterialLayout->Fields, b.MaterialLayout->Fields),
				what + ": layouts differ");
		}
	}

	void TestTheProtocolCarriesAWholeResult()
	{
		LocalShaderCompileService local(SearchPaths());
		local.Submit(Request(7, EShaderCompileKind::Material, TestShaders / "Stone.slang"));
		local.Submit(Request(8, EShaderCompileKind::Material, TestShaders / "Broken.slang"));
		for (const ShaderCompileResponse& response : local.TakeResults())
		{
			BinaryWriter writer;
			ShaderCompileProtocol::WriteResult(writer, response.Request.Id, response.Result);
			BinaryReader reader;
			reader.LoadFromMemory(writer.GetBytes());
			uint32_t id = 0;
			ShaderCompileResult read;
			Require(ShaderCompileProtocol::ReadResult(reader, id, read) && reader.GetRemainingSize() == 0, "a result doesn't read back");
			Require(id == response.Request.Id, "the request id doesn't read back");
			RequireSameResult(read, response.Result, response.Request.File.filename().string() + " through the protocol");
		}

		ShaderCompileRequest request = Request(9, EShaderCompileKind::Material, TestShaders / "Stone.slang");
		BinaryWriter writer;
		ShaderCompileProtocol::WriteRequest(writer, request);
		BinaryReader reader;
		reader.LoadFromMemory(writer.GetBytes());
		ShaderCompileRequest read;
		Require(ShaderCompileProtocol::ReadRequest(reader, read), "a request doesn't read back");
		Require(read.Id == 9 && read.Kind == EShaderCompileKind::Material && read.File == request.File && read.Domain.Module == "nyx.surface" &&
				read.Domain.EntryPoints == request.Domain.EntryPoints,
			"a request reads back different");
	}

	void TestTheCompilerProgramGivesWhatThisProcessGives()
	{
		Require(std::filesystem::exists(CompilerProgram()), CompilerProgram().string() + " is missing");
		const std::vector<ShaderCompileRequest> requests{
			Request(1, EShaderCompileKind::Material, TestShaders / "Stone.slang"),
			Request(2, EShaderCompileKind::Material, TestShaders / "Broken.slang"),
			Request(3, EShaderCompileKind::Program, TestShaders / "Includes.frag"),
			Request(4, EShaderCompileKind::DefaultsWriter, TestShaders / "Stone.slang"),
		};

		RemoteShaderCompileService remote(CompilerProgram(), SearchPaths());
		LocalShaderCompileService local(SearchPaths());
		for (const ShaderCompileRequest& request : requests)
		{
			remote.Submit(request);
			local.Submit(request);
		}
		const std::vector<ShaderCompileResponse> remoteResults = WaitForResults(remote, requests.size());
		const std::vector<ShaderCompileResponse> localResults = local.TakeResults();
		for (size_t index = 0; index < requests.size(); ++index)
		{
			Require(remoteResults[index].Request.Id == requests[index].Id, "results arrive out of order");
			RequireSameResult(remoteResults[index].Result, localResults[index].Result, requests[index].File.filename().string() + " in NyxShaderCompiler");
		}
		Require(remote.GetStartCount() == 1, "the compiler was started " + std::to_string(remote.GetStartCount()) + " times, expected once");
		Require(remote.GetPendingCount() == 0, "requests are still pending");
	}

	void TestACrashCostsOneRequest()
	{
		RemoteShaderCompileService remote(CompilerProgram(), SearchPaths(), true);
		remote.Submit(Request(1, EShaderCompileKind::Material, TestShaders / "Stone.slang"));
		remote.Submit(Request(2, EShaderCompileKind::TestCrash, TestShaders / "Stone.slang"));
		remote.Submit(Request(3, EShaderCompileKind::Material, TestShaders / "Stone.slang"));

		const std::vector<ShaderCompileResponse> results = WaitForResults(remote, 3);
		Require(results[0].Request.Id == 1 && results[0].Result.bSucceeded, "the request before the crash failed");
		Require(results[1].Request.Id == 2 && !results[1].Result.bSucceeded, "the crashing request didn't fail");
		Require(!results[1].Result.Diagnostics.empty() && results[1].Result.Diagnostics[0].Message.find("crashed") != std::string::npos,
			"the crash isn't named: " + (results[1].Result.Diagnostics.empty() ? std::string("no diagnostics") : results[1].Result.Diagnostics[0].ToString()));
		Require(results[2].Request.Id == 3 && results[2].Result.bSucceeded, "the request after the crash failed");
		Require(remote.GetStartCount() == 2, "the compiler was started " + std::to_string(remote.GetStartCount()) + " times, expected twice");
	}

	void TestACompilerThatCantStartIsReported()
	{
		RemoteShaderCompileService remote(Nyx::Paths::GetExecutableDir() / "NoSuchCompiler.exe", SearchPaths());
		remote.Submit(Request(1, EShaderCompileKind::Material, TestShaders / "Stone.slang"));
		const std::vector<ShaderCompileResponse> results = WaitForResults(remote, 1);
		Require(!results[0].Result.bSucceeded && !results[0].Result.Diagnostics.empty(), "a compiler that can't start isn't reported");
	}
}

int main()
{
	try
	{
		TestTheProtocolCarriesAWholeResult();
		TestTheCompilerProgramGivesWhatThisProcessGives();
		TestACrashCostsOneRequest();
		TestACompilerThatCantStartIsReported();
	}
	catch (const std::exception& error)
	{
		std::cerr << "FAILED: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All shader compile service tests passed\n";
	return 0;
}
