#pragma once

#include "SoftwareIntro.h"

#include <cstdint>
#include <vector>

namespace Nyx
{
	// The "assemble" intro: the desktop breaks into small tiles, tiles of matching
	// brightness fly across the screen and build the banner, and the remaining
	// tiles drift away into a dark backdrop.
	//
	// It plays either on a canvas around the banner (assemble) or within the
	// banner's own rectangle (assemble_v2); see Layout.
	//
	// The work happens in this order:
	//   1. PrepareTiles: cut the desktop (sources) and the banner (targets) into
	//      equally sized square tiles.
	//   2. MatchTiles:   give every banner tile a desktop tile of similar
	//      brightness, so the banner seems to condense out of the desktop.
	//   3. PlanFlights:  give every desktop tile a flight: when it lifts off, the
	//      curve it follows, and where it ends (a banner tile, or off into the dark).
	//   4. Render:       backdrop, then desktop tiles still at rest, then tiles in
	//      flight, whose colour turns from desktop to banner as they land.
	class StartupBannerAssembleRenderer final : public ISoftwareIntro
	{
	public:
		static constexpr float DurationSeconds = 3.4f;

		// How the effect fits its window.
		struct Layout
		{
			// Width of the soft edge where the surface fades into the real desktop,
			// in logical pixels.
			float FeatherWidth = 90.0f;
			// Whether tiles flying into the banner fade near the window edge too. Off
			// when the banner fills the window, as its edge tiles land at the border.
			bool bFeatherBannerTiles = true;
		};

		// assemble: a canvas larger than the banner, with the banner inside it.
		static Layout AroundBanner() { return Layout{ 90.0f, true }; }
		// assemble_v2: the window is exactly the banner.
		static Layout BannerOnly() { return Layout{ 24.0f, false }; }

		explicit StartupBannerAssembleRenderer(Layout layout = AroundBanner())
			: Settings(layout)
		{
		}

		// Without a captured desktop, a dark gradient stands in for it.
		void Prepare(const SoftwareIntroInputs& inputs) override;
		const uint32_t* Render(float elapsedSeconds) override;

		// Tiles land on the banner while it is already scrolling, so its animation
		// simply continues once the intro ends.
		bool UsesLiveBanner() const override { return true; }
		void SetLiveBanner(const uint32_t* pixels, int stride) override;
		int GetWidth() const override { return PixelWidth; }
		int GetHeight() const override { return PixelHeight; }
		void Release() override;

	private:
		// A square region of an image, in physical pixels. Edge tiles may be smaller.
		struct Tile
		{
			int X = 0;
			int Y = 0;
			int Width = 0;
			int Height = 0;
			float Luminance = 0.0f;
		};

		// The journey of one desktop tile. Positions are top-left corners in
		// physical pixels; the path is a quadratic curve through Control.
		struct Flight
		{
			int Source = -1;          // Index into SourceTiles.
			int Target = -1;          // Index into TargetTiles, or -1 if it drifts away.
			float StartTime = 0.0f;
			float Duration = 1.0f;
			float StartX = 0.0f, StartY = 0.0f;
			float ControlX = 0.0f, ControlY = 0.0f;
			float EndX = 0.0f, EndY = 0.0f;
		};

		void PrepareDesktop(const SoftwareIntroInputs& inputs);
		void PrepareTiles();
		void MatchTiles();
		void PlanFlights();

		void DrawBackdrop(float time);
		void DrawRestingTiles(float time);
		void DrawFlights(float time);
		void DrawFlight(const Flight& flight, float time);
		void DrawBanner(float opacity);

		// Row y of the banner: the live frame when one was supplied, else the prepared copy.
		const uint32_t* BannerRow(int y) const;

	private:
		Layout Settings;
		int PixelWidth = 0;
		int PixelHeight = 0;
		float Scale = 1.0f;          // Physical pixels per logical pixel.
		float CanvasWidth = 0.0f;    // Size of the surface in logical pixels.
		float CanvasHeight = 0.0f;
		int TileSize = 1;

		std::vector<uint32_t> Desktop;
		std::vector<uint32_t> Banner;          // As prepared; used for brightness matching.
		const uint32_t* LiveBanner = nullptr;  // The current frame, during Render only.
		int LiveBannerStride = 0;
		std::vector<uint32_t> Frame;
		int BannerWidth = 0;
		int BannerHeight = 0;
		int BannerLeft = 0;
		int BannerTop = 0;

		std::vector<float> FeatherColumns;
		std::vector<float> FeatherRows;

		std::vector<Tile> SourceTiles;
		std::vector<Tile> TargetTiles;
		std::vector<int> TargetSource;      // Per banner tile: the desktop tile matched to it.
		std::vector<Flight> Flights;        // One per desktop tile; drifting ones first.
		std::vector<float> SourceLiftTime;  // Per desktop tile: when it leaves its place.
	};
}
