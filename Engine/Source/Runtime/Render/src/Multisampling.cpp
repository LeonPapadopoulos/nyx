#include "NyxPCH.h"
#include "Multisampling.h"

namespace Nyx
{
	uint32_t ChooseMsaaSamples(uint32_t requested, uint32_t supported)
	{
		uint32_t samples = MaxMsaaSamples;
		while (samples > 1 && (samples > requested || (supported & samples) == 0))
		{
			samples /= 2;
		}
		return samples;
	}
}
