#include "Window.h"

#include <algorithm>

namespace Nyx
{
	namespace
	{
		// How much of the strip along the window's top must lie on a monitor: enough to grab it
		constexpr int TopStripHeight = 32;
		constexpr int GrabWidth = 100;
		constexpr int GrabHeight = 16;

		ScreenRect Intersect(const ScreenRect& a, const ScreenRect& b)
		{
			const int left = std::max(a.X, b.X);
			const int top = std::max(a.Y, b.Y);
			const int right = std::min(a.X + a.Width, b.X + b.Width);
			const int bottom = std::min(a.Y + a.Height, b.Y + b.Height);
			return ScreenRect{ left, top, std::max(right - left, 0), std::max(bottom - top, 0) };
		}
	}

	WindowPlacement FitWindowOnScreen(const WindowPlacement& placement, std::span<const ScreenRect> workAreas)
	{
		if (workAreas.empty())
		{
			return placement;
		}

		const ScreenRect& rect = placement.Rect;
		const ScreenRect topStrip{ rect.X, rect.Y, rect.Width, std::min(rect.Height, TopStripHeight) };
		const ScreenRect* mostOverlapped = &workAreas.front();
		long long mostOverlap = 0;
		for (const ScreenRect& area : workAreas)
		{
			const ScreenRect onStrip = Intersect(topStrip, area);
			if (onStrip.Width >= std::min(rect.Width, GrabWidth) && onStrip.Height >= std::min(topStrip.Height, GrabHeight))
			{
				return placement;
			}
			const ScreenRect overlap = Intersect(rect, area);
			const long long overlapArea = static_cast<long long>(overlap.Width) * overlap.Height;
			if (overlapArea > mostOverlap)
			{
				mostOverlap = overlapArea;
				mostOverlapped = &area;
			}
		}

		const ScreenRect& area = *mostOverlapped;
		WindowPlacement fitted = placement;
		fitted.Rect.Width = std::clamp(rect.Width, 1, std::max(area.Width, 1));
		fitted.Rect.Height = std::clamp(rect.Height, 1, std::max(area.Height, 1));
		fitted.Rect.X = std::clamp(rect.X, area.X, area.X + area.Width - fitted.Rect.Width);
		fitted.Rect.Y = std::clamp(rect.Y, area.Y, area.Y + area.Height - fitted.Rect.Height);
		return fitted;
	}
}
