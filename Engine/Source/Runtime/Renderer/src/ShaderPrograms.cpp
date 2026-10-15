#include "ShaderPrograms.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>

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
	}

	ShaderPrograms::ShaderPrograms(std::filesystem::path builtFolder, std::filesystem::path sourceFolder, std::filesystem::path savedFolder)
		: SourceFolder(std::move(sourceFolder))
		, Built(std::move(builtFolder), SourceFolder)
	{
		if (!savedFolder.empty())
		{
			Saved.emplace(std::move(savedFolder), SourceFolder);
		}
	}

	ShaderCompileRequest ShaderPrograms::RequestFor(const std::string& name) const
	{
		ShaderCompileRequest request;
		request.Kind = EShaderCompileKind::Program;
		request.File = SourceFolder / name;
		return request;
	}

	std::string ShaderPrograms::GetResultKey(const ShaderCompileRequest& request)
	{
		const char* kind = request.Kind == EShaderCompileKind::Material ? "material" : request.Kind == EShaderCompileKind::DefaultsWriter ? "defaults"
																																		  : "program";
		return PathKey(request.File) + "|" + kind + "|" + request.Domain.Module;
	}

	std::optional<ShaderPrograms::Program> ShaderPrograms::FromCaches(const ShaderCompileRequest& request) const
	{
		// The newest code that matches the files: live reload's, then the build's. Failing that,
		// code that doesn't match any more, live reload's first.
		std::optional<ShaderCache::Entry> saved = Saved ? Saved->Find(request) : std::nullopt;
		std::optional<ShaderCache::Entry> built = Built.Find(request);
		std::optional<ShaderCache::Entry>* chosen = saved && saved->bUpToDate ? &saved : built && built->bUpToDate ? &built
			: saved                                                                                                ? &saved
																												   : &built;
		if (!*chosen || (*chosen)->Result.EntryPoints.empty())
		{
			return std::nullopt;
		}

		Program program;
		program.Request = request;
		program.Code = std::move((*chosen)->Result);
		program.Dependencies = program.Code.Dependencies;
		program.Dependencies.push_back(request.File);
		program.bStartedOutOfDate = !(*chosen)->bUpToDate;
		// Compiled at the first update, without waiting for anything to settle; following the
		// editor, its compiles bring the new code
		program.bDirty = program.bStartedOutOfDate && !bFollowingEditor;
		return program;
	}

	ShaderPrograms::Program& ShaderPrograms::Load(const std::string& name)
	{
		auto found = Programs.find(name);
		if (found == Programs.end())
		{
			std::optional<Program> program = FromCaches(RequestFor(name));
			if (!program)
			{
				throw std::runtime_error("No compiled code for shader " + name + " in " + Built.GetFolder().string() + ": build the shaders (NyxShaders)");
			}
			found = Programs.emplace(name, std::move(*program)).first;
		}
		return found->second;
	}

	const ShaderCompileResult* ShaderPrograms::GetResult(const ShaderCompileRequest& request)
	{
		const std::string key = GetResultKey(request);
		auto found = Programs.find(key);
		if (found == Programs.end())
		{
			std::optional<Program> program = FromCaches(request);
			if (!program)
			{
				// Nothing compiled yet: at the first update
				program.emplace();
				program->Request = request;
				program->Dependencies.push_back(request.File);
				program->bStartedOutOfDate = true;
				program->bDirty = true;
			}
			found = Programs.emplace(key, std::move(*program)).first;
		}
		return found->second.Code.bSucceeded ? &found->second.Code : nullptr;
	}

	bool ShaderPrograms::IsPending(const ShaderCompileRequest& request) const
	{
		const auto found = Programs.find(GetResultKey(request));
		return found != Programs.end() && (found->second.bDirty || found->second.RequestId != 0);
	}

	const std::vector<uint32_t>& ShaderPrograms::GetSpirv(const std::string& name)
	{
		Program& program = Load(name);
		if (program.Code.EntryPoints.size() != 1)
		{
			throw std::runtime_error("Shader " + name + " has " + std::to_string(program.Code.EntryPoints.size()) + " entry points: name the one to use");
		}
		return program.Code.EntryPoints[0].Spirv;
	}

	const std::vector<uint32_t>& ShaderPrograms::GetSpirv(const std::string& name, const std::string& entryPoint)
	{
		Program& program = Load(name);
		for (const CompiledEntryPoint& compiled : program.Code.EntryPoints)
		{
			if (compiled.Name == entryPoint)
			{
				return compiled.Spirv;
			}
		}
		throw std::runtime_error("Shader " + name + " has no entry point " + entryPoint);
	}

	bool ShaderPrograms::StartedOutOfDate(const std::string& name) const
	{
		const auto found = Programs.find(name);
		return found != Programs.end() && found->second.bStartedOutOfDate;
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

	void ShaderPrograms::Inject(const ShaderCompileRequest& request, ShaderCompileResult result)
	{
		if (!result.bSucceeded)
		{
			return;
		}
		// Programs are named by their file in the source folder, material shaders by their key
		const std::string name = request.Kind == EShaderCompileKind::Program ? request.File.filename().string() : GetResultKey(request);
		Program& program = Programs[name];
		program.Request = request;
		program.Request.Id = 0;
		program.Code = result;
		program.Dependencies = result.Dependencies;
		program.Dependencies.push_back(request.File);
		program.bDirty = false;
		program.bStartedOutOfDate = false;

		ShaderProgramUpdate update;
		update.Name = name;
		update.Request = program.Request;
		update.Result = std::move(result);
		update.bChanged = true;
		Injected.push_back(std::move(update));
	}

	void ShaderPrograms::FollowEditor()
	{
		bFollowingEditor = true;
		// Out-of-date code waits for the editor's compile; what has no code at all still compiles
		for (auto& [name, program] : Programs)
		{
			if (!program.Code.EntryPoints.empty())
			{
				program.bDirty = false;
			}
		}
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
		std::vector<ShaderProgramUpdate> updates = std::exchange(Injected, {});
		if (!Service)
		{
			return updates;
		}

		// Which programs the changed files concern; following the editor, none: its compiles come
		for (const std::filesystem::path& changed : Watcher.TakeChanges())
		{
			if (bFollowingEditor)
			{
				continue;
			}
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
					ShaderCompileRequest request = program.Request;
					request.Id = NextRequestId++;
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

			// Pipelines are made from the entry points by name: a compile without one of them
			// doesn't replace them
			if (response.Result.bSucceeded)
			{
				for (const CompiledEntryPoint& known : program.Code.EntryPoints)
				{
					const bool bStill = std::any_of(response.Result.EntryPoints.begin(), response.Result.EntryPoints.end(), [&known](const CompiledEntryPoint& compiled)
						{
							return compiled.Name == known.Name;
						});
					if (!bStill)
					{
						response.Result.bSucceeded = false;
						response.Result.Diagnostics.push_back(ShaderDiagnostic{ .File = response.Request.File, .Message = "the entry point " + known.Name + " is gone; the engine draws with it" });
					}
				}
			}

			ShaderProgramUpdate update;
			update.Name = found->first;
			update.Request = program.Request;
			if (response.Result.bSucceeded)
			{
				program.Code = response.Result;
				update.bChanged = true;
				// The next start begins from it
				if (Saved)
				{
					Saved->Store(response.Request, response.Result);
				}
			}
			update.Result = std::move(response.Result);
			updates.push_back(std::move(update));
		}
		return updates;
	}
}
