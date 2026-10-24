#pragma once

#include "ReflectedAssetText.h"

#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace Nyx
{
	// An asset type whose file is a reflected struct's values as text (ReflectedAssetText): the editor
	// opens it in a window of its properties, makes new ones, and sends its values to the running
	// games as they change, where whoever listens for the type takes them.
	struct ReflectedAssetType
	{
		// What asset references call it (a property's AssetType) and the editor's asset type id, e.g.
		// "PhysicsMaterial"
		std::string TypeId;
		// e.g. "Physics Material": the asset browser's label, "New Physics Material", the window's title
		std::string DisplayName;
		// With the dot, e.g. ".nyxphysmat"
		std::string Extension;
		// One line on what it is: the tooltip of New <DisplayName>, and a new file's first comment
		std::string Description;

		ReflectedStruct Struct;

		// Optional: what is wrong with the values, each at its property (PropertyProblem), e.g. a
		// minimum above its maximum. The editor shows them at the properties; entries' names are
		// checked anyway (CheckEntryNames()).
		std::function<void(const void* data, std::vector<PropertyProblem>& outProblems)> Validate;
	};

	// What a game or Nyx registers: the type's struct T, and its check on a T
	template <typename T>
	struct ReflectedAssetDesc
	{
		std::string TypeId;
		std::string DisplayName;
		std::string Extension;
		std::string Description;
		std::function<void(const T& data, std::vector<PropertyProblem>& outProblems)> Validate;
	};

	// Takes an asset's text as the editor has it, saved or not, e.g. to read it again
	using ReflectedAssetTextFn = std::function<void(const std::string& path, std::string_view text)>;

	// Every reflected asset type: the engine's (RegisterEngineAssetTypes(), in the Application's
	// constructor) and a game module's, which registers its own in its RegisterTypes(). Then, while
	// a game runs, whoever reads assets of a type listens for their live text (Listen()).
	class ReflectedAssetTypes
	{
	public:
		static ReflectedAssetTypes& Get();

		template <typename T>
		void Register(ReflectedAssetDesc<T> desc)
		{
			ReflectedAssetType type{ .TypeId = std::move(desc.TypeId),
				.DisplayName = std::move(desc.DisplayName),
				.Extension = std::move(desc.Extension),
				.Description = std::move(desc.Description),
				.Struct = ReflectedStruct::Of<T>() };
			if (desc.Validate)
			{
				type.Validate = [validate = std::move(desc.Validate)](const void* data, std::vector<PropertyProblem>& outProblems)
				{
					validate(*static_cast<const T*>(data), outProblems);
				};
			}
			Register(std::move(type));
		}

		// Registering a type again replaces it. A type whose extension or type id another struct's
		// type has is refused with an error.
		void Register(ReflectedAssetType type);

		// By extension ("" and case don't matter: ".NYXPHYSMAT" finds the physics materials)
		const ReflectedAssetType* FindByExtension(std::string_view extension) const;
		// The type of the file at `path`, by its extension
		const ReflectedAssetType* FindForPath(const std::filesystem::path& path) const;
		const ReflectedAssetType* FindByTypeId(std::string_view typeId) const;
		// References stay valid as more are registered
		const std::deque<ReflectedAssetType>& GetAll() const
		{
			return Types;
		}

		// Calls `take` with every asset of the extension's type whose text reaches the game live
		// (TakeLiveText()), until StopListening() with what this returns
		uint64_t Listen(std::string extension, ReflectedAssetTextFn take);
		void StopListening(uint64_t listener);

		// Hands an asset's text, as the editor has it, to whoever listens for its type; false when no
		// one does
		bool TakeLiveText(const std::string& path, std::string_view text) const;

	private:
		struct Listener
		{
			uint64_t Id = 0;
			std::string Extension;
			ReflectedAssetTextFn Take;
		};

		std::deque<ReflectedAssetType> Types;
		std::vector<Listener> Listeners;
		uint64_t NextListener = 1;
	};

	// Listens for a type's live text while it lives (ReflectedAssetTypes::Listen())
	class ReflectedAssetListener
	{
	public:
		ReflectedAssetListener() = default;
		ReflectedAssetListener(std::string extension, ReflectedAssetTextFn take);
		~ReflectedAssetListener();

		ReflectedAssetListener(ReflectedAssetListener&& other) noexcept;
		ReflectedAssetListener& operator=(ReflectedAssetListener&& other) noexcept;
		ReflectedAssetListener(const ReflectedAssetListener&) = delete;
		ReflectedAssetListener& operator=(const ReflectedAssetListener&) = delete;

	private:
		uint64_t Id = 0;
	};

	// Everything wrong with an asset's values: its entries' names, then the type's own Validate
	std::vector<PropertyProblem> ValidateReflectedAsset(const ReflectedAssetType& type, const void* data);

	// Reads an asset file of the type into `data`, an object of its struct with its defaults: false,
	// with why in `outError`, for a file that can't be read; values that can't be read are left out
	// and listed in `outProblems`
	bool LoadReflectedAsset(const std::filesystem::path& file, const ReflectedAssetType& type, void* data, std::vector<PropertyProblem>& outProblems,
		std::string& outError);

	// The engine's own reflected asset types: physics materials. The Application registers them before
	// a game module's.
	void RegisterEngineAssetTypes();
}
