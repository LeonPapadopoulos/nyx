#pragma once

#include <cstdint>

namespace Nyx
{
	// Everything a software intro needs to know when it is prepared. All pixels are
	// premultiplied BGRA (GDI+ PixelFormat32bppPARGB / Windows DIB order) and all
	// strides are counted in pixels.
	struct SoftwareIntroInputs
	{
		// Size of the intro surface in physical pixels. The logical canvas is always
		// 1040 x 720 (StartupBannerIntro::Width/Height), scaled to this size.
		int Width = 0;
		int Height = 0;

		// The captured desktop behind the surface, the same size as the surface.
		// May be null when capturing failed; each intro then supplies a fallback.
		const uint32_t* Desktop = nullptr;
		int DesktopStride = 0;

		// The banner exactly as it appears once the intro has finished.
		const uint32_t* Banner = nullptr;
		int BannerWidth = 0;
		int BannerHeight = 0;
		int BannerStride = 0;

		// Top-left corner of the banner on the logical canvas.
		float BannerX = 0.0f;
		float BannerY = 0.0f;
	};

	// A startup intro drawn on the CPU into raw pixels rather than through GDI+.
	// Implementations have no Windows dependencies, so they can be rendered and
	// tested anywhere; StartupBannerSoftwareIntro connects them to GDI+ bitmaps.
	class ISoftwareIntro
	{
	public:
		virtual ~ISoftwareIntro() = default;

		// Called once, before the intro window appears. Copies what it needs.
		virtual void Prepare(const SoftwareIntroInputs& inputs) = 0;

		// Returns the frame at elapsedSeconds; valid until the next call. From the
		// end of the intro onward only the banner is left, at its final position.
		virtual const uint32_t* Render(float elapsedSeconds) = 0;

		// Most intros work from the banner as it was when they were prepared, and the
		// banner starts animating once they finish. An intro that builds the banner
		// while it already animates returns true here; it then receives the live
		// banner frame before every Render call, valid only during that call.
		virtual bool UsesLiveBanner() const { return false; }
		virtual void SetLiveBanner(const uint32_t* pixels, int stride)
		{
			(void)pixels;
			(void)stride;
		}

		virtual int GetWidth() const = 0;
		virtual int GetHeight() const = 0;

		// Drops the captured desktop and everything derived from it.
		virtual void Release() = 0;
	};
}
