#include "NyxPCH.h"
#include "EnvironmentComponent.h"

#include "Entity.h"
#include "Log.h"
#include "WindComponent.h"

#include <cmath>
#include <vector>

namespace Nyx
{
	void FoldLegacySkyMode(EnvironmentComponent& environment)
	{
		if (!environment.bSkyFollowsSun)
		{
			environment.SkyMode = ESkyMode::CubeMap;
			environment.bSkyFollowsSun = true;
		}
	}

	void MoveLegacyEnvironmentWind(Registry& world)
	{
		bool bHasWind = FindWorldWind(world) != nullptr;
		std::vector<std::pair<Entity, glm::vec3>> legacy;
		world.Each<EnvironmentComponent>(
			[&](Entity entity, EnvironmentComponent& environment)
			{
				if (environment.Wind != glm::vec3(0.0f))
				{
					legacy.emplace_back(entity, environment.Wind);
					environment.Wind = glm::vec3(0.0f);
				}
			});

		for (const auto& [entity, air] : legacy)
		{
			if (bHasWind)
			{
				break;
			}
			if (air.y != 0.0f)
			{
				LOG_WARNING("The environment's wind blew {0} m/s up; the world's wind blows only along the ground, so its particles no longer rise with it", air.y);
			}
			const float speed = std::sqrt(air.x * air.x + air.z * air.z);
			if (!(speed > 0.0f))
			{
				continue;
			}

			// Steady, the same at every height, as it carried particles
			WindComponent& wind = world.Add<WindComponent>(entity);
			wind.DirectionDegrees = glm::degrees(std::atan2(-air.z, air.x));
			wind.Speed = speed;
			wind.Gustiness = 0.0f;
			wind.Turbulence = 0.0f;
			wind.GroundFactor = 1.0f;
			wind.Aloft = 0.0f;
			bHasWind = true;
		}
	}
}
