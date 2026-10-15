#pragma once

#include <string>

namespace Nyx::Engine
{
	class Resource
	{
	public:
		explicit Resource(const std::string& id)
			: ResourceId(id)
		{
		}
		virtual ~Resource() = default;

		const std::string& GetId() const
		{
			return ResourceId;
		}

		bool IsLoaded() const
		{
			return bLoaded;
		}

		bool Load()
		{
			bLoaded = DoLoad();
			return bLoaded;
		}

		void Unload()
		{
			DoUnload();
			bLoaded = false;
		}

	protected:
		// Option for resource-specific loading/unloading behavior
		virtual bool DoLoad() = 0;
		virtual bool DoUnload() = 0;

	private:
		std::string ResourceId;
		bool bLoaded = false;
	};
}
