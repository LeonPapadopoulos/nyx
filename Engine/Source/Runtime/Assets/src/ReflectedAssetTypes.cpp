#include "ReflectedAssetTypes.h"

#include "Log.h"
#include "PhysicsMaterialAsset.h"
#include "StringUtil.h"

#include <algorithm>
#include <utility>

namespace Nyx
{
	namespace
	{
		// ".nyxphysmat", whether it was given with its dot or not
		std::string_view WithoutDot(std::string_view extension)
		{
			return extension.starts_with('.') ? extension.substr(1) : extension;
		}

		bool IsExtension(std::string_view a, std::string_view b)
		{
			return EqualsIgnoringCase(WithoutDot(a), WithoutDot(b));
		}
	}

	ReflectedAssetTypes& ReflectedAssetTypes::Get()
	{
		static ReflectedAssetTypes Instance;
		return Instance;
	}

	void ReflectedAssetTypes::Register(ReflectedAssetType type)
	{
		for (ReflectedAssetType& registered : Types)
		{
			const bool bSameExtension = IsExtension(registered.Extension, type.Extension);
			const bool bSameId = registered.TypeId == type.TypeId;
			if (!bSameExtension && !bSameId)
			{
				continue;
			}
			if (bSameExtension && bSameId && registered.Struct.Metadata == type.Struct.Metadata)
			{
				registered = std::move(type);
				return;
			}
			LOG_ERROR("The asset type '{0}' ({1}) isn't registered: '{2}' ({3}) has its extension or type id", type.TypeId, type.Extension,
				registered.TypeId, registered.Extension);
			return;
		}
		Types.push_back(std::move(type));
	}

	const ReflectedAssetType* ReflectedAssetTypes::FindByExtension(std::string_view extension) const
	{
		const auto found = std::find_if(Types.begin(), Types.end(), [extension](const ReflectedAssetType& type)
			{
				return IsExtension(type.Extension, extension);
			});
		return found != Types.end() ? &*found : nullptr;
	}

	const ReflectedAssetType* ReflectedAssetTypes::FindForPath(const std::filesystem::path& path) const
	{
		const std::string extension = path.extension().string();
		return extension.empty() ? nullptr : FindByExtension(extension);
	}

	const ReflectedAssetType* ReflectedAssetTypes::FindByTypeId(std::string_view typeId) const
	{
		const auto found = std::find_if(Types.begin(), Types.end(), [typeId](const ReflectedAssetType& type)
			{
				return type.TypeId == typeId;
			});
		return found != Types.end() ? &*found : nullptr;
	}

	uint64_t ReflectedAssetTypes::Listen(std::string extension, ReflectedAssetTextFn take)
	{
		const uint64_t id = NextListener++;
		Listeners.push_back(Listener{ id, std::move(extension), std::move(take) });
		return id;
	}

	void ReflectedAssetTypes::StopListening(uint64_t listener)
	{
		std::erase_if(Listeners, [listener](const Listener& candidate)
			{
				return candidate.Id == listener;
			});
	}

	bool ReflectedAssetTypes::TakeLiveText(const std::string& path, std::string_view text) const
	{
		const std::string extension = std::filesystem::path(path).extension().string();
		bool bTaken = false;
		// A copy: a listener may stop listening, or another start, while it takes the text
		const std::vector<Listener> listeners = Listeners;
		for (const Listener& listener : listeners)
		{
			if (!extension.empty() && IsExtension(listener.Extension, extension))
			{
				listener.Take(path, text);
				bTaken = true;
			}
		}
		return bTaken;
	}

	ReflectedAssetListener::ReflectedAssetListener(std::string extension, ReflectedAssetTextFn take)
		: Id(ReflectedAssetTypes::Get().Listen(std::move(extension), std::move(take)))
	{
	}

	ReflectedAssetListener::~ReflectedAssetListener()
	{
		if (Id)
		{
			ReflectedAssetTypes::Get().StopListening(Id);
		}
	}

	ReflectedAssetListener::ReflectedAssetListener(ReflectedAssetListener&& other) noexcept
		: Id(std::exchange(other.Id, 0))
	{
	}

	ReflectedAssetListener& ReflectedAssetListener::operator=(ReflectedAssetListener&& other) noexcept
	{
		if (this != &other)
		{
			if (Id)
			{
				ReflectedAssetTypes::Get().StopListening(Id);
			}
			Id = std::exchange(other.Id, 0);
		}
		return *this;
	}

	std::vector<PropertyProblem> ValidateReflectedAsset(const ReflectedAssetType& type, const void* data)
	{
		std::vector<PropertyProblem> problems;
		CheckEntryNames(data, *type.Struct.Metadata, problems);
		if (type.Validate)
		{
			type.Validate(data, problems);
		}
		return problems;
	}

	bool LoadReflectedAsset(const std::filesystem::path& file, const ReflectedAssetType& type, void* data, std::vector<PropertyProblem>& outProblems,
		std::string& outError)
	{
		ReflectedAssetText text;
		if (!text.Load(file, outError))
		{
			return false;
		}
		text.Read(data, *type.Struct.Metadata, &outProblems);
		return true;
	}

	void RegisterEngineAssetTypes()
	{
		RegisterPhysicsMaterialAssetType();
	}
}
