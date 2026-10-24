#include "PhysicsMaterialAsset.h"

#include "ReflectedAssetTypes.h"

#include <algorithm>

namespace Nyx
{
	float GetExtra(const PhysicsMaterialData& material, std::string_view name, float fallback)
	{
		const auto found = std::find_if(material.Extras.begin(), material.Extras.end(), [name](const PhysicsMaterialExtra& extra)
			{
				return extra.Name == name;
			});
		return found != material.Extras.end() ? found->Value : fallback;
	}

	void RegisterPhysicsMaterialAssetType()
	{
		ReflectedAssetTypes::Get().Register(ReflectedAssetDesc<PhysicsMaterialData>{
			.TypeId = "PhysicsMaterial",
			.DisplayName = "Physics Material",
			.Extension = std::string(PhysicsMaterialExtension),
			.Description = "How a collider's surface slides and bounces, how heavy its stuff is, and a game's own numbers (Extra.<Name> = value)",
			.Validate = [](const PhysicsMaterialData& material, std::vector<PropertyProblem>& outProblems)
			{
				if (material.Friction < 0.0f)
				{
					outProblems.push_back({ "Friction", "can't be below 0" });
				}
				if (material.Restitution < 0.0f || material.Restitution > 1.0f)
				{
					outProblems.push_back({ "Restitution", "is from 0 (no bounce) to 1" });
				}
				if (material.Density <= 0.0f)
				{
					outProblems.push_back({ "Density", "must be above 0" });
				}
			} });
	}
}
