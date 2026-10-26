#include "NyxPCH.h"
#include "EnvironmentComponent.h"

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
}
