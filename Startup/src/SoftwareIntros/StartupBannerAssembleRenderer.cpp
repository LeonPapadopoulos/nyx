#include "StartupBannerAssembleRenderer.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
	// The banner is always 640 logical pixels wide; this sets the DPI scale.
	constexpr float BannerLogicalWidth = 640.0f;

	// Timeline, in seconds.
	constexpr float LiftStart = 0.20f;     // First tiles leave the desktop.
	constexpr float LiftSpread = 0.90f;    // Departures are staggered over this window.
	constexpr float FlightSeconds = 1.10f; // Time each tile spends in the air.
	constexpr float SettleStart = 2.40f;   // The exact banner fades in over the landed tiles.
	constexpr float SettleSeconds = 0.70f;
	constexpr float BackdropFadeStart = 2.30f; // The dark void gently gives way to the desktop.
	constexpr float BackdropFadeSeconds = 1.00f;

	// Look.
	constexpr float TileLogicalSize = 6.0f;
	constexpr float BackdropOpacity = 0.85f;
	constexpr uint32_t BackdropColor = 0xff08'0a10u; // Opaque ARGB: R, G, B = 8, 10, 16, a near-black blue.

	float Saturate(float value)
	{
		return std::clamp(value, 0.0f, 1.0f);
	}

	float SmoothStep(float value)
	{
		const float t = Saturate(value);
		return t * t * (3.0f - 2.0f * t);
	}

	// Stable pseudo-random values in [0, 1], so every run looks the same.
	float Noise(unsigned int seed)
	{
		seed = (seed ^ 61u) ^ (seed >> 16);
		seed *= 9u;
		seed ^= seed >> 4;
		seed *= 0x27d4eb2du;
		seed ^= seed >> 15;
		return (seed & 65535u) / 65535.0f;
	}

	// Pixels are packed as 0xAARRGGBB; shift selects the channel (0 = blue ... 24 = alpha).
	uint32_t Channel(uint32_t pixel, int shift)
	{
		return pixel >> shift & 255u;
	}

	// Mixes two pixels channel by channel; amount runs from 0 (all a) to 256 (all b).
	uint32_t LerpPixel(uint32_t a, uint32_t b, uint32_t amount)
	{
		uint32_t result = 0;
		for (int shift = 0; shift < 32; shift += 8)
		{
			result |= (Channel(a, shift) * (256u - amount) + Channel(b, shift) * amount) >> 8 << shift;
		}
		return result;
	}

	// Scales every channel of a premultiplied pixel, which scales its opacity (0 to 256).
	uint32_t ScalePixel(uint32_t pixel, uint32_t amount)
	{
		uint32_t result = 0;
		for (int shift = 0; shift < 32; shift += 8)
		{
			result |= Channel(pixel, shift) * amount >> 8 << shift;
		}
		return result;
	}

	// Draws a premultiplied pixel over another at the given opacity (0 to 256).
	// For premultiplied pixels no channel of the sum can exceed 255.
	uint32_t BlendOver(uint32_t destination, uint32_t source, uint32_t opacity)
	{
		const uint32_t covered = Channel(source, 24) * opacity >> 8;
		return ScalePixel(source, opacity) + ScalePixel(destination, 256u - covered);
	}

	float Luminance(uint32_t pixel)
	{
		return 0.2126f * Channel(pixel, 16) + 0.7152f * Channel(pixel, 8) + 0.0722f * Channel(pixel, 0);
	}
}

namespace Nyx
{
	void StartupBannerAssembleRenderer::Prepare(const SoftwareIntroInputs& inputs)
	{
		PixelWidth = std::max(1, inputs.Width);
		PixelHeight = std::max(1, inputs.Height);
		Scale = std::max(1, inputs.BannerWidth) / BannerLogicalWidth;
		CanvasWidth = PixelWidth / Scale;
		CanvasHeight = PixelHeight / Scale;
		TileSize = std::max(2, static_cast<int>(std::lround(TileLogicalSize * Scale)));
		Frame.assign(static_cast<size_t>(PixelWidth) * PixelHeight, 0u);

		BannerWidth = inputs.BannerWidth;
		BannerHeight = inputs.BannerHeight;
		BannerLeft = static_cast<int>(inputs.BannerX * Scale);
		BannerTop = static_cast<int>(inputs.BannerY * Scale);
		Banner.resize(static_cast<size_t>(BannerWidth) * BannerHeight);
		for (int y = 0; y < BannerHeight; ++y)
		{
			std::copy_n(inputs.Banner + static_cast<size_t>(y) * inputs.BannerStride, BannerWidth,
				Banner.data() + static_cast<size_t>(y) * BannerWidth);
		}

		// The surface fades out towards its edges so it never shows a hard border.
		FeatherColumns.resize(PixelWidth);
		for (int x = 0; x < PixelWidth; ++x)
		{
			const float logical = (x + 0.5f) / Scale;
			FeatherColumns[x] = SmoothStep(std::min(logical, CanvasWidth - logical) / Settings.FeatherWidth);
		}
		FeatherRows.resize(PixelHeight);
		for (int y = 0; y < PixelHeight; ++y)
		{
			const float logical = (y + 0.5f) / Scale;
			FeatherRows[y] = SmoothStep(std::min(logical, CanvasHeight - logical) / Settings.FeatherWidth);
		}

		PrepareDesktop(inputs);
		PrepareTiles();
		MatchTiles();
		PlanFlights();
	}

	void StartupBannerAssembleRenderer::Release()
	{
		Desktop = {};
		SourceTiles = {};
		TargetTiles = {};
		TargetSource = {};
		Flights = {};
		SourceLiftTime = {};
	}

	void StartupBannerAssembleRenderer::PrepareDesktop(const SoftwareIntroInputs& inputs)
	{
		Desktop.resize(static_cast<size_t>(PixelWidth) * PixelHeight);
		if (inputs.Desktop)
		{
			for (int y = 0; y < PixelHeight; ++y)
			{
				std::copy_n(inputs.Desktop + static_cast<size_t>(y) * inputs.DesktopStride, PixelWidth,
					Desktop.data() + static_cast<size_t>(y) * PixelWidth);
			}
			return;
		}
		// Fallback: a dark blue gradient with per-tile variation, so the brightness
		// matching still has a range of tiles to choose from.
		for (int y = 0; y < PixelHeight; ++y)
		{
			for (int x = 0; x < PixelWidth; ++x)
			{
				const unsigned int tile = static_cast<unsigned int>((y / TileSize) * 4096 + x / TileSize);
				const float light = (0.35f + 0.65f * (1.0f - static_cast<float>(y) / PixelHeight)) * (0.6f + 0.8f * Noise(tile));
				const auto channel = [&](float base)
				{
					return static_cast<uint32_t>(std::min(255.0f, base * light));
				};
				Desktop[static_cast<size_t>(y) * PixelWidth + x] = channel(70.0f) | channel(42.0f) << 8 | channel(28.0f) << 16 | 0xff000000u;
			}
		}
	}

	void StartupBannerAssembleRenderer::PrepareTiles()
	{
		// Cuts an image into a grid of tiles and measures each tile's average brightness.
		auto cut = [this](const uint32_t* pixels, int stride, int width, int height, int offsetX, int offsetY)
		{
			std::vector<Tile> tiles;
			for (int y = 0; y < height; y += TileSize)
			{
				for (int x = 0; x < width; x += TileSize)
				{
					Tile tile;
					tile.X = offsetX + x;
					tile.Y = offsetY + y;
					tile.Width = std::min(TileSize, width - x);
					tile.Height = std::min(TileSize, height - y);
					float sum = 0.0f;
					for (int row = 0; row < tile.Height; ++row)
					{
						for (int column = 0; column < tile.Width; ++column)
						{
							sum += Luminance(pixels[static_cast<size_t>(y + row) * stride + x + column]);
						}
					}
					tile.Luminance = sum / (tile.Width * tile.Height);
					tiles.push_back(tile);
				}
			}
			return tiles;
		};
		SourceTiles = cut(Desktop.data(), PixelWidth, PixelWidth, PixelHeight, 0, 0);
		TargetTiles = cut(Banner.data(), BannerWidth, BannerWidth, BannerHeight, BannerLeft, BannerTop);
	}

	void StartupBannerAssembleRenderer::MatchTiles()
	{
		// Rank both sets by brightness and pair them rank for rank: the darkest
		// banner tile receives a dark desktop tile, the brightest a bright one.
		// When the desktop has more tiles than the banner, evenly spaced ranks are
		// used, so every brightness band of the desktop contributes.
		auto rankByLuminance = [](const std::vector<Tile>& tiles, auto&& isEligible)
		{
			std::vector<int> order;
			for (int index = 0; index < static_cast<int>(tiles.size()); ++index)
			{
				if (isEligible(tiles[index]))
				{
					order.push_back(index);
				}
			}
			std::stable_sort(order.begin(), order.end(), [&](int a, int b)
				{
					return tiles[a].Luminance < tiles[b].Luminance;
				});
			return order;
		};
		// Only full-size desktop tiles may build the banner: a narrower tile from the
		// desktop's edge would leave a gap in the banner tile it fills. They drift away.
		const std::vector<int> sources = rankByLuminance(SourceTiles,
			[this](const Tile& tile)
			{
				return tile.Width == TileSize && tile.Height == TileSize;
			});
		const std::vector<int> targets = rankByLuminance(TargetTiles, [](const Tile&)
			{
				return true;
			});
		TargetSource.assign(TargetTiles.size(), -1);
		if (sources.empty())
		{
			return;
		}
		const size_t sourceCount = sources.size(), targetCount = targets.size();
		for (size_t rank = 0; rank < targetCount; ++rank)
		{
			const size_t sourceRank = std::min(sourceCount - 1, (2 * rank + 1) * sourceCount / (2 * targetCount));
			TargetSource[targets[rank]] = sources[sourceRank];
		}
	}

	void StartupBannerAssembleRenderer::PlanFlights()
	{
		Flights.clear();
		// Every desktop tile gets exactly one flight; it leaves its place when that flight starts.
		SourceLiftTime.assign(SourceTiles.size(), std::numeric_limits<float>::max());
		std::vector<bool> bTravelsToBanner(SourceTiles.size(), false);
		for (const int source : TargetSource)
		{
			if (source >= 0)
			{
				bTravelsToBanner[source] = true;
			}
		}

		// Tiles that are not needed drift outward from the canvas centre and fade.
		const float centerX = PixelWidth * 0.5f, centerY = PixelHeight * 0.5f;
		for (size_t index = 0; index < SourceTiles.size(); ++index)
		{
			if (bTravelsToBanner[index])
			{
				continue;
			}
			const Tile& tile = SourceTiles[index];
			const unsigned int key = static_cast<unsigned int>(index) * 7u;
			Flight flight;
			flight.Source = static_cast<int>(index);
			flight.StartTime = LiftStart + Noise(key) * LiftSpread;
			flight.Duration = FlightSeconds * (0.7f + 0.6f * Noise(key + 1u));
			flight.StartX = static_cast<float>(tile.X);
			flight.StartY = static_cast<float>(tile.Y);
			const float dx = tile.X - centerX, dy = tile.Y - centerY;
			const float length = std::max(1.0f, std::sqrt(dx * dx + dy * dy));
			const float reach = (150.0f + 250.0f * Noise(key + 2u)) * Scale;
			flight.EndX = flight.StartX + dx / length * reach;
			flight.EndY = flight.StartY + dy / length * reach;
			flight.ControlX = (flight.StartX + flight.EndX) * 0.5f;
			flight.ControlY = (flight.StartY + flight.EndY) * 0.5f;
			SourceLiftTime[index] = flight.StartTime;
			Flights.push_back(flight);
		}

		// Matched tiles fly to their banner tile along a gentle arc. The banner builds
		// from its centre outward: central tiles depart first.
		const float bannerCenterX = BannerLeft + BannerWidth * 0.5f, bannerCenterY = BannerTop + BannerHeight * 0.5f;
		const float bannerRadius = std::max(1.0f, std::hypot(static_cast<float>(BannerWidth), static_cast<float>(BannerHeight)) * 0.5f);
		for (size_t target = 0; target < TargetTiles.size(); ++target)
		{
			const int source = TargetSource[target];
			if (source < 0)
			{
				continue;
			}
			const Tile& from = SourceTiles[source];
			const Tile& to = TargetTiles[target];
			const unsigned int key = static_cast<unsigned int>(target) * 13u + 100000u;
			Flight flight;
			flight.Source = source;
			flight.Target = static_cast<int>(target);
			const float fromCenter = std::hypot(to.X - bannerCenterX, to.Y - bannerCenterY);
			flight.StartTime = LiftStart + LiftSpread * Saturate(0.8f * fromCenter / bannerRadius + 0.2f * Noise(key));
			flight.Duration = FlightSeconds;
			flight.StartX = static_cast<float>(from.X);
			flight.StartY = static_cast<float>(from.Y);
			flight.EndX = static_cast<float>(to.X);
			flight.EndY = static_cast<float>(to.Y);
			// The arc bends sideways by up to a third of the distance travelled.
			const float bend = (Noise(key + 1u) - 0.5f) * 0.66f;
			flight.ControlX = (flight.StartX + flight.EndX) * 0.5f - (flight.EndY - flight.StartY) * bend;
			flight.ControlY = (flight.StartY + flight.EndY) * 0.5f + (flight.EndX - flight.StartX) * bend;
			SourceLiftTime[source] = std::min(SourceLiftTime[source], flight.StartTime);
			Flights.push_back(flight);
		}
	}

	void StartupBannerAssembleRenderer::DrawBackdrop(float time)
	{
		// A dark void opens behind the departing tiles, then gives way to the banner.
		const float opacity = BackdropOpacity * SmoothStep((time - LiftStart + 0.05f) / 0.6f) *
			(1.0f - SmoothStep((time - BackdropFadeStart) / BackdropFadeSeconds));
		for (int y = 0; y < PixelHeight; ++y)
		{
			uint32_t* row = Frame.data() + static_cast<size_t>(y) * PixelWidth;
			const float rowOpacity = opacity * FeatherRows[y];
			// Away from the feathered edges every pixel in the row is the same.
			const uint32_t interior = ScalePixel(BackdropColor, static_cast<uint32_t>(rowOpacity * 256.0f));
			for (int x = 0; x < PixelWidth; ++x)
			{
				const float feather = FeatherColumns[x];
				row[x] = feather >= 1.0f
					? interior
					: ScalePixel(BackdropColor, static_cast<uint32_t>(rowOpacity * feather * 256.0f));
			}
		}
	}

	void StartupBannerAssembleRenderer::DrawRestingTiles(float time)
	{
		// Desktop tiles that have not yet lifted off are copied in place, opaque.
		for (size_t index = 0; index < SourceTiles.size(); ++index)
		{
			if (time >= SourceLiftTime[index])
			{
				continue;
			}
			const Tile& tile = SourceTiles[index];
			for (int row = 0; row < tile.Height; ++row)
			{
				const size_t offset = static_cast<size_t>(tile.Y + row) * PixelWidth + tile.X;
				std::copy_n(Desktop.data() + offset, tile.Width, Frame.data() + offset);
			}
		}
	}

	void StartupBannerAssembleRenderer::DrawFlight(const Flight& flight, float time)
	{
		const float progress = Saturate((time - flight.StartTime) / flight.Duration);
		const float t = SmoothStep(progress);
		const float inverse = 1.0f - t;
		// Position along the quadratic curve Start -> Control -> End.
		const int x = static_cast<int>(std::lround(inverse * inverse * flight.StartX + 2.0f * inverse * t * flight.ControlX + t * t * flight.EndX));
		const int y = static_cast<int>(std::lround(inverse * inverse * flight.StartY + 2.0f * inverse * t * flight.ControlY + t * t * flight.EndY));

		const Tile& source = SourceTiles[flight.Source];
		const bool bToBanner = flight.Target >= 0;
		// Banner-bound tiles take on their banner colour over the last part of the
		// flight; tiles drifting away fade out quickly as they go.
		const uint32_t colorMix = bToBanner ? static_cast<uint32_t>(SmoothStep((progress - 0.55f) / 0.45f) * 256.0f) : 0u;
		const float remaining = 1.0f - progress;
		float visibility = bToBanner ? 1.0f : remaining * remaining;

		// Tiles fade into the soft edge of the window just like the backdrop, so none
		// is ever cut off by the window border. Near the edge a faded tile reveals the
		// real desktop behind the window, which looks the same as the tile's origin.
		int width = source.Width, height = source.Height;
		if (!bToBanner || Settings.bFeatherBannerTiles)
		{
			const int centerX = std::clamp(x + width / 2, 0, PixelWidth - 1);
			const int centerY = std::clamp(y + height / 2, 0, PixelHeight - 1);
			visibility *= FeatherColumns[centerX] * FeatherRows[centerY];
		}
		const uint32_t opacity = static_cast<uint32_t>(visibility * 256.0f);
		if (opacity == 0)
		{
			return;
		}
		const Tile* target = bToBanner ? &TargetTiles[flight.Target] : nullptr;
		if (target)
		{
			width = std::min(width, target->Width);
			height = std::min(height, target->Height);
		}
		// Only the columns that land inside the frame are drawn.
		const int firstColumn = std::max(0, -x), endColumn = std::min(width, PixelWidth - x);
		for (int row = 0; row < height; ++row)
		{
			const int frameY = y + row;
			if (frameY < 0 || frameY >= PixelHeight || firstColumn >= endColumn)
			{
				continue;
			}
			const uint32_t* desktopRow = Desktop.data() + static_cast<size_t>(source.Y + row) * PixelWidth + source.X;
			const uint32_t* bannerRow = target ? BannerRow(target->Y - BannerTop + row) + (target->X - BannerLeft) : nullptr;
			uint32_t* frameRow = Frame.data() + static_cast<size_t>(frameY) * PixelWidth + x;

			// Common cases first: an opaque tile that is purely desktop or purely banner.
			const uint32_t* pureRow = !bannerRow || colorMix == 0u ? desktopRow : (colorMix >= 256u ? bannerRow : nullptr);
			if (pureRow && opacity >= 256u)
			{
				std::copy(pureRow + firstColumn, pureRow + endColumn, frameRow + firstColumn);
				continue;
			}
			for (int column = firstColumn; column < endColumn; ++column)
			{
				const uint32_t color = pureRow ? pureRow[column] : LerpPixel(desktopRow[column], bannerRow[column], colorMix);
				frameRow[column] = opacity >= 256u ? color : BlendOver(frameRow[column], color, opacity);
			}
		}
	}

	void StartupBannerAssembleRenderer::DrawFlights(float time)
	{
		// Drifting tiles were planned first, so banner-bound tiles are drawn on top.
		for (const Flight& flight : Flights)
		{
			if (time >= flight.StartTime)
			{
				DrawFlight(flight, time);
			}
		}
	}

	void StartupBannerAssembleRenderer::DrawBanner(float opacity)
	{
		if (opacity <= 0.0f)
		{
			return;
		}
		const uint32_t amount = static_cast<uint32_t>(Saturate(opacity) * 256.0f);
		for (int y = 0; y < BannerHeight; ++y)
		{
			const int frameY = BannerTop + y;
			if (frameY < 0 || frameY >= PixelHeight)
			{
				continue;
			}
			const uint32_t* source = BannerRow(y);
			uint32_t* destination = Frame.data() + static_cast<size_t>(frameY) * PixelWidth;
			for (int x = 0; x < BannerWidth; ++x)
			{
				const int frameX = BannerLeft + x;
				if (frameX >= 0 && frameX < PixelWidth)
				{
					destination[frameX] = BlendOver(destination[frameX], source[x], amount);
				}
			}
		}
	}

	void StartupBannerAssembleRenderer::SetLiveBanner(const uint32_t* pixels, int stride)
	{
		LiveBanner = pixels;
		LiveBannerStride = stride;
	}

	const uint32_t* StartupBannerAssembleRenderer::BannerRow(int y) const
	{
		if (LiveBanner)
		{
			return LiveBanner + static_cast<size_t>(y) * LiveBannerStride;
		}
		return Banner.data() + static_cast<size_t>(y) * BannerWidth;
	}

	const uint32_t* StartupBannerAssembleRenderer::Render(float time)
	{
		if (time >= DurationSeconds || Desktop.empty())
		{
			std::fill(Frame.begin(), Frame.end(), 0u);
			DrawBanner(1.0f);
		}
		else
		{
			DrawBackdrop(time);
			DrawRestingTiles(time);
			DrawFlights(time);
			// Settle: the exact banner fades in over the landed tiles, covering any
			// rounding at tile edges, so the hand-over to the live banner is seamless.
			DrawBanner(SmoothStep((time - SettleStart) / SettleSeconds));
		}
		// The live frame is only valid during this call.
		LiveBanner = nullptr;
		return Frame.data();
	}
}
