#include "NyxPCH.h"
#include "WindField.h"

#include <vector>

namespace Nyx
{
	void SampleWindGridAtCentres(const IWindField& field, const WindFieldGrid& grid, float time, std::span<glm::vec3> outVelocities)
	{
		std::vector<glm::vec3> centres;
		centres.reserve(grid.GetCellCount());
		for (int32_t z = 0; z < grid.Cells.z; ++z)
		{
			for (int32_t y = 0; y < grid.Cells.y; ++y)
			{
				for (int32_t x = 0; x < grid.Cells.x; ++x)
				{
					centres.push_back(grid.GetCellCentre(x, y, z));
				}
			}
		}
		field.SampleWind(centres, time, outVelocities.first(std::min(outVelocities.size(), centres.size())));
	}
}
