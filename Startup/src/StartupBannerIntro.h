#pragma once

#include <windows.h>
#include <gdiplus.h>
#include "WindowsStartupBanner.h"
#include "StartupBannerFracture.h"
#include "StartupBannerSoftwareIntro.h"

#include <memory>
#include <vector>

namespace Nyx
{
	// Composes the optional intro into a premultiplied-alpha bitmap. Window
	// presentation is kept separate so the same frames can be inspected headlessly.
	class StartupBannerIntro
	{
	public:
		static constexpr float Width = 1040.0f;
		static constexpr float Height = 720.0f;
		static constexpr float DurationSeconds = 3.4f;
		static constexpr float BannerX = 200.0f;
		static constexpr float BannerY = 230.0f;
		static constexpr float RealityRecombinedSeconds = 2.16f;
		static constexpr float RealityImpactSeconds = 2.32f;

		void Prepare(int width, int height, Gdiplus::Bitmap& banner,
			EStartupBannerMode mode, Gdiplus::Bitmap* desktop = nullptr);
		Gdiplus::Bitmap* Render(float elapsedSeconds, Gdiplus::Bitmap& banner);

		// True when the intro builds the banner while it animates: the banner's clock
		// then runs from the start of the intro instead of from its end.
		bool UsesLiveBanner() const { return bLiveBanner; }

	private:
		struct Fragment
		{
			std::unique_ptr<Gdiplus::Bitmap> Image;
			Gdiplus::PointF Destination;
			Gdiplus::PointF Center;
			Gdiplus::PointF Displacement;
			std::vector<Gdiplus::PointF> Outline;
			Gdiplus::PointF Tilt;
			float Spin = 0.0f;
			float Depth = 0.0f;
			float DelaySeconds = 0.0f;
		};

		struct FragmentPose
		{
			const Fragment* Piece = nullptr;
			Gdiplus::PointF Offset;
			float Motion = 0.0f;
			float Depth = 0.0f;
			float Opacity = 1.0f;
		};

		struct Bolt
		{
			std::vector<Gdiplus::PointF> Points;
			float Width = 1.0f;
		};

		void PrepareFragments(Gdiplus::Bitmap& source, bool bDesktop);
		void PrepareLightning();
		void PrepareImpact();
		void DrawFragments(Gdiplus::Graphics& canvas, float elapsedSeconds);
		void DrawGlassFragment(Gdiplus::Graphics& canvas, const FragmentPose& pose);
		void DrawLightning(Gdiplus::Graphics& canvas, float elapsedSeconds);
		void DrawCuts(Gdiplus::Graphics& canvas, float elapsedSeconds);
		void DrawImpact(Gdiplus::Graphics& canvas, float elapsedSeconds);
		void DrawParticles(Gdiplus::Graphics& canvas, float elapsedSeconds);
		void DrawBanner(Gdiplus::Graphics& canvas, Gdiplus::Bitmap& banner, float opacity);
		void DrawGlow(Gdiplus::Graphics& canvas, float radius, float opacity);

	private:
		float Scale = 1.0f;
		EStartupBannerMode Mode = EStartupBannerMode::Lightning;
		std::unique_ptr<Gdiplus::Bitmap> FrameBuffer;
		std::unique_ptr<Gdiplus::Bitmap> Glow;
		std::unique_ptr<Gdiplus::Bitmap> Lightning;
		std::unique_ptr<Gdiplus::Bitmap> ImpactBurst;
		std::vector<Fragment> Fragments;
		std::vector<FragmentPose> FragmentPoses;
		std::vector<Bolt> Bolts;
		std::unique_ptr<StartupBannerFracture> Fracture;
		// Set for intros drawn on the CPU (rift, assemble) instead of with GDI+.
		std::unique_ptr<StartupBannerSoftwareIntro> SoftwareIntro;
		bool bLiveBanner = false; // Kept after the intro ends, so the banner clock never jumps.
		// Top-left of the banner on the canvas, in logical pixels: (BannerX, BannerY),
		// or the origin for intros played within the banner's own rectangle.
		Gdiplus::PointF BannerOrigin{ BannerX, BannerY };
	};
}
