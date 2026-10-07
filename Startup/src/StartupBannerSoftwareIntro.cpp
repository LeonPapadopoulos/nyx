#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include "StartupBannerSoftwareIntro.h"

#include <algorithm>
#include <cstring>
#include <optional>

namespace
{
	// Access to a bitmap's premultiplied pixels for the lifetime of the scope.
	class LockedPixels
	{
	public:
		LockedPixels(Gdiplus::Bitmap& bitmap, Gdiplus::ImageLockMode mode)
			: Bitmap(bitmap)
		{
			Gdiplus::Rect bounds(0, 0, static_cast<INT>(bitmap.GetWidth()), static_cast<INT>(bitmap.GetHeight()));
			bLocked = bitmap.LockBits(&bounds, mode, PixelFormat32bppPARGB, &Data) == Gdiplus::Ok;
		}

		~LockedPixels()
		{
			if (bLocked)
			{
				Bitmap.UnlockBits(&Data);
			}
		}

		LockedPixels(const LockedPixels&) = delete;
		LockedPixels& operator=(const LockedPixels&) = delete;

		bool IsValid() const { return bLocked && Data.Stride > 0; }
		uint32_t* Pixels() const { return static_cast<uint32_t*>(Data.Scan0); }
		int StrideInPixels() const { return Data.Stride / 4; }

	private:
		Gdiplus::Bitmap& Bitmap;
		Gdiplus::BitmapData Data{};
		bool bLocked = false;
	};
}

namespace Nyx
{
	StartupBannerSoftwareIntro::StartupBannerSoftwareIntro(std::unique_ptr<ISoftwareIntro> intro)
		: Intro(std::move(intro))
	{
	}

	void StartupBannerSoftwareIntro::Prepare(int width, int height, Gdiplus::Bitmap& banner, Gdiplus::Bitmap* desktop,
		float bannerX, float bannerY)
	{
		LockedPixels bannerPixels(banner, Gdiplus::ImageLockModeRead);
		if (!bannerPixels.IsValid())
		{
			return;
		}
		SoftwareIntroInputs inputs;
		inputs.Width = width;
		inputs.Height = height;
		inputs.Banner = bannerPixels.Pixels();
		inputs.BannerWidth = static_cast<int>(banner.GetWidth());
		inputs.BannerHeight = static_cast<int>(banner.GetHeight());
		inputs.BannerStride = bannerPixels.StrideInPixels();
		inputs.BannerX = bannerX;
		inputs.BannerY = bannerY;

		// A missing or mismatched capture is left out; the intro uses its fallback.
		const bool bDesktopFits = desktop && static_cast<int>(desktop->GetWidth()) == width &&
			static_cast<int>(desktop->GetHeight()) == height;
		if (!bDesktopFits)
		{
			Intro->Prepare(inputs);
			return;
		}
		LockedPixels desktopPixels(*desktop, Gdiplus::ImageLockModeRead);
		if (desktopPixels.IsValid())
		{
			inputs.Desktop = desktopPixels.Pixels();
			inputs.DesktopStride = desktopPixels.StrideInPixels();
		}
		Intro->Prepare(inputs);
	}

	void StartupBannerSoftwareIntro::Render(float elapsedSeconds, Gdiplus::Bitmap& target, Gdiplus::Bitmap& liveBanner)
	{
		// An intro that builds the banner while it animates reads the banner's current
		// pixels, which stay locked until the frame has been rendered.
		std::optional<LockedPixels> bannerPixels;
		if (Intro->UsesLiveBanner())
		{
			bannerPixels.emplace(liveBanner, Gdiplus::ImageLockModeRead);
			if (bannerPixels->IsValid())
			{
				Intro->SetLiveBanner(bannerPixels->Pixels(), bannerPixels->StrideInPixels());
			}
		}
		const uint32_t* frame = Intro->Render(elapsedSeconds);
		const int width = std::min(Intro->GetWidth(), static_cast<int>(target.GetWidth()));
		const int height = std::min(Intro->GetHeight(), static_cast<int>(target.GetHeight()));
		LockedPixels targetPixels(target, Gdiplus::ImageLockModeWrite);
		if (!frame || !targetPixels.IsValid())
		{
			return;
		}
		for (int row = 0; row < height; ++row)
		{
			std::memcpy(targetPixels.Pixels() + static_cast<size_t>(row) * targetPixels.StrideInPixels(),
				frame + static_cast<size_t>(row) * Intro->GetWidth(), static_cast<size_t>(width) * 4);
		}
	}

	void StartupBannerSoftwareIntro::Release()
	{
		Intro->Release();
	}
}
