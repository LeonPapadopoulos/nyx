#include "ShaderPrograms.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace Nyx::Engine
{
	namespace
	{
		// Paths as Windows compares them: whole, normalised, without regard to case
		std::string PathKey(const std::filesystem::path& path)
		{
			std::error_code error;
			std::filesystem::path absolute = std::filesystem::absolute(path, error);
			std::string key = (error ? path : absolute).lexically_normal().generic_string();
			std::transform(key.begin(), key.end(), key.begin(), [](unsigned char c)
				{
					return static_cast<char>(std::tolower(c));
				});
			while (!key.empty() && key.back() == '/')
			{
				key.pop_back();
			}
			return key;
		}

		// A changed path concerns a dependency when it is the file, or a folder it lies in (the
		// watcher reports a folder when it lost track of what changed there)
		bool Concerns(const std::string& changedKey, const std::string& dependencyKey)
		{
			return dependencyKey == changedKey || (dependencyKey.size() > changedKey.size() && dependencyKey.compare(0, changedKey.size(), changedKey) == 0 && dependencyKey[changedKey.size()] == '/');
		}

		std::vector<uint32_t> ReadSpirvFile(const std::filesystem::path& file)
		{
			std::ifstream stream(file, std::ios::binary | std::ios::ate);
			if (!stream)
			{
				throw std::runtime_error("Failed to open SPIR-V file: " + file.string());
			}
			const std::streamsize size = stream.tellg();
			if (size <= 0 || size % 4 != 0)
			{
				throw std::runtime_error("Not a SPIR-V file: " + file.string());
			}
			std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
			stream.seekg(0);
			stream.read(reinterpret_cast<char*>(words.data()), size);
			return words;
		}
	}

	ShaderPrograms::ShaderPrograms(std::filesystem::path compiledFolder, std::filesystem::path sourceFolder)
		: CompiledFolder(std::move(compiledFolder))
		, SourceFolder(std::move(sourceFolder))
	{
	}

	const std::vector<uint32_t>& ShaderPrograms::GetSpirv(const std::string& name)
	{
		auto found = Programs.find(name);
		if (found == Programs.end())
		{
			Program program;
			program.Spirv = ReadSpirvFile(CompiledFolder / (name + ".spv"));
			program.Dependencies.push_back(SourceFolder / name);
			found = Programs.emplace(name, std::move(program)).first;
		}
		return found->second.Spirv;
	}

	bool ShaderPrograms::EnableLiveReload(std::unique_ptr<IShaderCompileService> service, const std::vector<std::filesystem::path>& moreFolders)
	{
		if (!Watcher.Watch(SourceFolder))
		{
			Problem = Watcher.GetProblem();
			return false;
		}
		for (const std::filesystem::path& folder : moreFolders)
		{
			// The library usually lies inside the source folder, which is watched already
			if (Concerns(PathKey(SourceFolder), PathKey(folder)))
			{
				continue;
			}
			if (!Watcher.Watch(folder))
			{
				Problem = Watcher.GetProblem();
				return false;
			}
		}
		Service = std::move(service);
		return true;
	}

	bool ShaderPrograms::IsBusy() const
	{
		return std::any_of(Programs.begin(), Programs.end(), [](const auto& entry)
			{
				return entry.second.bDirty || entry.second.RequestId != 0;
			});
	}

	std::vector<ShaderProgramUpdate> ShaderPrograms::Update(Clock::time_point now)
	{
		std::vector<ShaderProgramUpdate> updates;
		if (!Service)
		{
			return updates;
		}

		// Which programs the changed files concern
		for (const std::filesystem::path& changed : Watcher.TakeChanges())
		{
			const std::string changedKey = PathKey(changed);
			for (auto& [name, program] : Programs)
			{
				const bool bConcerned = std::any_of(program.Dependencies.begin(), program.Dependencies.end(),
					[&changedKey](const std::filesystem::path& dependency)
					{
						return Concerns(changedKey, PathKey(dependency));
					});
				if (bConcerned)
				{
					program.bDirty = true;
					LastChange = now;
				}
			}
		}

		// Once the files have settled, every changed program that isn't compiling already goes out.
		// One that is compiling goes again once its result is in.
		if (now - LastChange >= SettleTime)
		{
			for (auto& [name, program] : Programs)
			{
				if (program.bDirty && program.RequestId == 0)
				{
					ShaderCompileRequest request;
					request.Id = NextRequestId++;
					request.Kind = EShaderCompileKind::Program;
					request.File = SourceFolder / name;
					program.bDirty = false;
					program.RequestId = request.Id;
					Service->Submit(request);
				}
			}
		}

		for (ShaderCompileResponse& response : Service->TakeResults())
		{
			const auto found = std::find_if(Programs.begin(), Programs.end(), [&response](const auto& entry)
				{
					return entry.second.RequestId == response.Request.Id;
				});
			if (found == Programs.end())
			{
				continue;
			}
			Program& program = found->second;
			program.RequestId = 0;

			// What the compile read; a failed one may know less, so nothing known is forgotten
			for (const std::filesystem::path& dependency : response.Result.Dependencies)
			{
				const std::string key = PathKey(dependency);
				const bool bKnown = std::any_of(program.Dependencies.begin(), program.Dependencies.end(), [&key](const std::filesystem::path& known)
					{
						return PathKey(known) == key;
					});
				if (!bKnown)
				{
					program.Dependencies.push_back(dependency);
				}
			}

			ShaderProgramUpdate update;
			update.Name = found->first;
			if (response.Result.bSucceeded && response.Result.EntryPoints.size() == 1)
			{
				program.Spirv = response.Result.EntryPoints[0].Spirv;
				update.bChanged = true;
			}
			update.Result = std::move(response.Result);
			updates.push_back(std::move(update));
		}
		return updates;
	}
}
