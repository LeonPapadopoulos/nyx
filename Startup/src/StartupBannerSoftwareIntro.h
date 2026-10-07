#pragma once

#include <windows.h>
#include <gdiplus.h>
#include "SoftwareIntro.h"

#include <memory>

namespace Nyx
{
	// Connects a CPU-drawn intro (see SoftwareIntros/SoftwareIntro.h) to GDI+: it
	// hands the captured desktop and banner to the intro, and copies each finished
	// frame into the intro's bitmap. It knows nothing about any particular effect.
	class StartupBannerSoftwareIntro
	{
	public:
		explicit StartupBannerSoftwareIntro(std::unique_ptr<ISoftwareIntro> intro);

		// The desktop, when captured, must match the intro surface's size.
		void Prepare(int width, int height, Gdiplus::Bitmap& banner, Gdiplus::Bitmap* desktop,
			float bannerX, float bannerY);

		// Writes the frame at elapsedSeconds into target, which must be width x height.
		// liveBanner is the banner as it looks right now; only intros that build the
		// banner while it animates use it.
		void Render(float elapsedSeconds, Gdiplus::Bitmap& target, Gdiplus::Bitmap& liveBanner);

		bool UsesLiveBanner() const { return Intro->UsesLiveBanner(); }

		// Lets the intro drop its captured pixels once it no longer needs them.
		void Release();

	private:
		std::unique_ptr<ISoftwareIntro> Intro;
	};
}
