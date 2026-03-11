#pragma once
#include "ResourceManager.h"

namespace Engine
{
	class Mesh : public Resource
	{
	public:
		explicit Mesh(const std::string& id)
			: Resource(id)
		{
		}

		~Mesh() override
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