#include "SceneComponentTypeRegistry.h"

namespace Nyx::Engine
{
	SceneComponentTypeRegistry& SceneComponentTypeRegistry::Get()
	{
		static SceneComponentTypeRegistry Instance;
		return Instance;
	}

	void SceneComponentTypeRegistry::Register(SceneComponentTypeOps ops)
	{
		for (const SceneComponentTypeOps& existing : Types)
		{
			if (std::string_view(existing.SerializedTypeName) == std::string_view(ops.SerializedTypeName))
			{
				return;
			}
		}

		Types.push_back(ops);
	}

	const std::vector<SceneComponentTypeOps>& SceneComponentTypeRegistry::GetAll() const
	{
		return Types;
	}

	const SceneComponentTypeOps* SceneComponentTypeRegistry::FindBySerializedTypeName(std::string_view typeName) const
	{
		for (const SceneComponentTypeOps& ops : Types)
		{
			if (std::string_view(ops.SerializedTypeName) == typeName)
			{
				return &ops;
			}
		}

		return nullptr;
	}
}