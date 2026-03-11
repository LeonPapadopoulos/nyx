#pragma once
#include "ResourceManager.h"

namespace Engine
{
	class Texture : public Resource
	{
	public:
		explicit Texture(const std::string& id)
			: Resource(id)
		{
		}

		~Texture() override
		{
			Unload();
		}

		bool DoLoad() override
		{
			// @todo
		}

		bool DoUnload() override
		{
			// @todo
		}

	private:
	};

} // Engine
