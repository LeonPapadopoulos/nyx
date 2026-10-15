#include "UITheme.h"

namespace Nyx::UI
{
	Theme& GetTheme()
	{
		static Theme theme;
		return theme;
	}
}
