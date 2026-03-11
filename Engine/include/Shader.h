#pragma once
#include "ResourceManager.h"

namespace Engine
{
	class Shader : public Resource
	{
	public:
		explicit Shader(const std::string& id)
			: Resource(id)
		{
		}

		~Shader() override
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