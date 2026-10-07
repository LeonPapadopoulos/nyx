#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include "StartupBannerFracture.h"
#include "StartupBannerIntro.h"

#include <algorithm>
#include <cmath>

namespace
{
	using PolygonPoints = std::vector<Gdiplus::PointF>;
	constexpr float CenterX = 520.0f;
	constexpr float CenterY = 240.0f;
	constexpr float CollapseSeconds = 1.15f;
	constexpr float FallSeconds = 1.10f;
	constexpr float Pi = 3.14159265f;

	float Clamp(float value)
	{
		return std::clamp(value, 0.0f, 1.0f);
	}

	float Smooth(float value)
	{
		const float t = Clamp(value);
		return t * t * (3.0f - 2.0f * t);
	}

	float Noise(unsigned int seed)
	{
		seed = (seed ^ 61u) ^ (seed >> 16);
		seed *= 9u;
		seed ^= seed >> 4;
		seed *= 0x27d4eb2du;
		seed ^= seed >> 15;
		return (seed & 65535u) / 65535.0f;
	}

	float Distance(Gdiplus::PointF point)
	{
		return std::hypot(point.X - CenterX, point.Y - CenterY);
	}

	// Voronoi cells share their boundaries: the cracks and screenshot shards
	// therefore describe exactly the same broken surface, without a regular grid.
	PolygonPoints ClipCell(const PolygonPoints& polygon, Gdiplus::PointF seed, Gdiplus::PointF other)
	{
		const Gdiplus::PointF normal(other.X - seed.X, other.Y - seed.Y);
		const Gdiplus::PointF midpoint((seed.X + other.X) * 0.5f, (seed.Y + other.Y) * 0.5f);
		auto distance = [&](Gdiplus::PointF point)
		{
			return (point.X - midpoint.X) * normal.X + (point.Y - midpoint.Y) * normal.Y;
		};
		PolygonPoints result;
		for (size_t index = 0; index < polygon.size(); ++index)
		{
			const auto a = polygon[index];
			const auto b = polygon[(index + 1) % polygon.size()];
			const float da = distance(a), db = distance(b);
			if (da <= 0.0f)
			{
				result.push_back(a);
			}
			if ((da <= 0.0f) != (db <= 0.0f))
			{
				const float t = da / (da - db);
				result.emplace_back(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
			}
		}
		return result;
	}

	void FadeImage(Gdiplus::Graphics& canvas, Gdiplus::Bitmap& image, int x, int y, float opacity)
	{
		if (opacity <= 0.0f)
		{
			return;
		}
		Gdiplus::ColorMatrix matrix = {
			1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0,
			0, 0, 0, Clamp(opacity), 0, 0, 0, 0, 0, 1
		};
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&matrix);
		const int width = image.GetWidth(), height = image.GetHeight();
		canvas.DrawImage(&image, Gdiplus::Rect(x, y, width, height), 0, 0, width, height, Gdiplus::UnitPixel, &attributes);
	}
}

namespace Nyx
{
	void StartupBannerFracture::Prepare(int width, int height, Gdiplus::Bitmap& banner, Gdiplus::Bitmap* desktop)
	{
		Scale = width / StartupBannerIntro::Width;
		SurfaceHeight = height / Scale;
		Frame = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
		Gdiplus::Bitmap fallback(width, height, PixelFormat32bppPARGB);
		if (!desktop)
		{
			Gdiplus::Graphics canvas(&fallback);
			canvas.Clear(Gdiplus::Color(255, 27, 32, 43));
			canvas.DrawImage(&banner, (width - static_cast<int>(banner.GetWidth())) / 2,
				(height - static_cast<int>(banner.GetHeight())) / 2, static_cast<int>(banner.GetWidth()), static_cast<int>(banner.GetHeight()));
		}
		PrepareShards(desktop ? *desktop : fallback);
	}

	void StartupBannerFracture::PrepareShards(Gdiplus::Bitmap& source)
	{
		Shards.clear();
		Cracks.clear();
		const float farthestCorner = std::hypot(CenterX, std::max(CenterY, SurfaceHeight - CenterY));
		std::vector<Gdiplus::PointF> seeds;
		for (unsigned int row = 0; row < 6; ++row)
		{
			for (unsigned int column = 0; column < 9; ++column)
			{
				const unsigned int index = row * 9 + column;
				seeds.emplace_back((column + 0.12f + Noise(index * 2 + 8) * 0.76f) * StartupBannerIntro::Width / 9,
					(row + 0.12f + Noise(index * 2 + 9) * 0.76f) * SurfaceHeight / 6);
			}
		}
		for (size_t index = 0; index < seeds.size(); ++index)
		{
			PolygonPoints polygon = { { 0, 0 }, { StartupBannerIntro::Width, 0 },
				{ StartupBannerIntro::Width, SurfaceHeight }, { 0, SurfaceHeight } };
			for (size_t other = 0; other < seeds.size() && !polygon.empty(); ++other)
			{
				if (other != index)
				{
					polygon = ClipCell(polygon, seeds[index], seeds[other]);
				}
			}
			if (polygon.size() < 3)
			{
				continue;
			}
			// Matching deterministic bends on each shared edge create jagged cracks
			// without separating the textures before the surface actually breaks.
			PolygonPoints jagged;
			for (size_t point = 0; point < polygon.size(); ++point)
			{
				const auto a = polygon[point], b = polygon[(point + 1) % polygon.size()];
				jagged.push_back(a);
				const float length = std::hypot(b.X - a.X, b.Y - a.Y);
				if (length < 12.0f || (a.X == b.X && (a.X == 0 || a.X == StartupBannerIntro::Width)) ||
					(a.Y == b.Y && (a.Y == 0 || a.Y == SurfaceHeight)))
				{
					continue;
				}
				const float direction = a.X < b.X || (a.X == b.X && a.Y < b.Y) ? 1.0f : -1.0f;
				const auto seed = static_cast<unsigned int>(std::round((a.X + b.X) * 0.5f)) * 733u +
					static_cast<unsigned int>(std::round((a.Y + b.Y) * 0.5f));
				const float bend = (Noise(seed) - 0.5f) * std::min(12.0f, length * 0.15f) * direction;
				jagged.emplace_back((a.X + b.X) * 0.5f - (b.Y - a.Y) / length * bend,
					(a.Y + b.Y) * 0.5f + (b.X - a.X) / length * bend);
			}
			polygon = std::move(jagged);
			Shard shard;
			shard.Center = seeds[index];
			shard.BreakTime = CollapseSeconds + Distance(shard.Center) / farthestCorner * 0.58f + Noise(static_cast<unsigned int>(index) + 800) * 0.07f;
			shard.Spin = (Noise(static_cast<unsigned int>(index) + 900) - 0.5f) * 3.4f;
			float left = StartupBannerIntro::Width, top = SurfaceHeight, right = 0, bottom = 0;
			for (size_t point = 0; point < polygon.size(); ++point)
			{
				const auto a = polygon[point];
				const auto b = polygon[(point + 1) % polygon.size()];
				left = std::min(left, a.X);
				top = std::min(top, a.Y);
				right = std::max(right, a.X);
				bottom = std::max(bottom, a.Y);
				// Each shared edge is stored in one direction only. Skip the capture border.
				if ((a.Y < b.Y || (a.Y == b.Y && a.X < b.X)) &&
					!(a.X == b.X && (a.X == 0 || a.X == StartupBannerIntro::Width)) &&
					!(a.Y == b.Y && (a.Y == 0 || a.Y == SurfaceHeight)))
				{
					const bool bStartCloser = Distance(a) <= Distance(b);
					Cracks.push_back({ bStartCloser ? a : b, bStartCloser ? b : a,
						0.15f + std::min(Distance(a), Distance(b)) / farthestCorner * 0.32f, shard.BreakTime });
				}
			}
			const int x = static_cast<int>(std::floor(left * Scale));
			const int y = static_cast<int>(std::floor(top * Scale));
			const int width = static_cast<int>(std::ceil(right * Scale)) - x;
			const int height = static_cast<int>(std::ceil(bottom * Scale)) - y;
			shard.Origin = { static_cast<float>(x), static_cast<float>(y) };
			for (auto& point : polygon)
			{
				point = { point.X * Scale - x, point.Y * Scale - y };
			}
			shard.Outline = polygon;
			shard.Image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
			Gdiplus::Graphics canvas(shard.Image.get());
			canvas.Clear(Gdiplus::Color(0, 0, 0, 0));
			Gdiplus::GraphicsPath clip;
			clip.AddPolygon(polygon.data(), static_cast<int>(polygon.size()));
			canvas.SetClip(&clip);
			canvas.DrawImage(&source, Gdiplus::Rect(0, 0, width, height), x, y, width, height, Gdiplus::UnitPixel);
			Shards.push_back(std::move(shard));
		}
		// Earlier breaks sink farther away; draw them behind the remaining surface.
		std::stable_sort(Shards.begin(), Shards.end(), [](const Shard& a, const Shard& b)
			{
				return a.BreakTime < b.BreakTime;
			});
	}

	void StartupBannerFracture::DrawShard(Gdiplus::Graphics& canvas, const Shard& shard, float time)
	{
		const float age = time - shard.BreakTime;
		const int width = shard.Image->GetWidth(), height = shard.Image->GetHeight();
		if (age < 0.0f)
		{
			canvas.DrawImage(shard.Image.get(), static_cast<int>(shard.Origin.X), static_cast<int>(shard.Origin.Y), width, height);
			return;
		}
		if (age >= FallSeconds)
		{
			return;
		}
		const float fall = age / FallSeconds;
		const float depthScale = 1.0f / (1.0f + fall * 3.6f + fall * fall * 3.0f);
		const float angle = shard.Spin * fall;
		const float sine = std::sin(angle), cosine = std::cos(angle);
		const float tilt = std::cos(fall * (1.1f + std::abs(shard.Spin) * 0.25f));
		const Gdiplus::PointF pivot(shard.Center.X * Scale - shard.Origin.X, shard.Center.Y * Scale - shard.Origin.Y);
		auto project = [&](Gdiplus::PointF point)
		{
			const float x = point.X - pivot.X, y = (point.Y - pivot.Y) * tilt;
			return Gdiplus::PointF(CenterX * Scale + ((shard.Center.X - CenterX) * Scale + x * cosine - y * sine) * depthScale,
				CenterY * Scale + ((shard.Center.Y - CenterY + 390.0f * fall * fall) * Scale + x * sine + y * cosine) * depthScale);
		};
		const Gdiplus::PointF corners[] = { project({ 0, 0 }), project({ static_cast<float>(width), 0 }), project({ 0, static_cast<float>(height) }) };
		const float light = 1.0f - Smooth(fall) * 0.90f;
		const float opacity = 1.0f - Smooth((fall - 0.65f) / 0.35f);
		Gdiplus::ColorMatrix matrix = {
			light, 0, 0, 0, 0, 0, light, 0, 0, 0, 0, 0, light, 0, 0,
			0, 0, 0, opacity, 0, 0, 0, 0, 0, 1
		};
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&matrix);
		const auto state = canvas.Save();
		canvas.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		canvas.DrawImage(shard.Image.get(), corners, 3, 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), Gdiplus::UnitPixel, &attributes);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::Pen edge(Gdiplus::Color(static_cast<BYTE>(210 * opacity * (1.0f - fall)), 255, 201, 126), Scale * 1.0f);
		for (size_t index = 0; index < shard.Outline.size(); ++index)
		{
			canvas.DrawLine(&edge, project(shard.Outline[index]), project(shard.Outline[(index + 1) % shard.Outline.size()]));
		}
		canvas.Restore(state);
	}

	void StartupBannerFracture::DrawCracks(Gdiplus::Graphics& canvas, float time)
	{
		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		for (const Crack& crack : Cracks)
		{
			if (time < crack.BirthTime || time > crack.BreakTime + 0.12f)
			{
				continue;
			}
			const float growth = Clamp((time - crack.BirthTime) / 0.07f);
			const float fade = 1.0f - Smooth((time - crack.BreakTime) / 0.12f);
			const auto end = Gdiplus::PointF(crack.Start.X + (crack.End.X - crack.Start.X) * growth,
				crack.Start.Y + (crack.End.Y - crack.Start.Y) * growth);
			Gdiplus::Pen shadow(Gdiplus::Color(static_cast<BYTE>(220 * fade), 4, 8, 18), 3.0f);
			Gdiplus::Pen seam(Gdiplus::Color(static_cast<BYTE>(160 * fade), 137, 191, 221), 0.85f);
			canvas.DrawLine(&shadow, crack.Start, end);
			canvas.DrawLine(&seam, crack.Start, end);
			// A second pulse sweeps out through the solidified fracture network.
			const float pulse = (time - crack.BirthTime - 0.26f) / 0.19f;
			if (pulse >= 0.0f && pulse <= 1.3f)
			{
				const float head = Clamp(pulse), tail = Clamp(pulse - 0.30f);
				const auto a = Gdiplus::PointF(crack.Start.X + (crack.End.X - crack.Start.X) * tail,
					crack.Start.Y + (crack.End.Y - crack.Start.Y) * tail);
				const auto b = Gdiplus::PointF(crack.Start.X + (crack.End.X - crack.Start.X) * head,
					crack.Start.Y + (crack.End.Y - crack.Start.Y) * head);
				Gdiplus::Pen glow(Gdiplus::Color(static_cast<BYTE>(90 * fade), 109, 201, 255), 7.0f);
				Gdiplus::Pen core(Gdiplus::Color(static_cast<BYTE>(255 * fade), 235, 252, 255), 2.0f);
				canvas.DrawLine(&glow, a, b);
				canvas.DrawLine(&core, a, b);
			}
		}
		canvas.Restore(state);
	}

	void StartupBannerFracture::DrawDebris(Gdiplus::Graphics& canvas, float time)
	{
		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		for (unsigned int index = 0; index < 130; ++index)
		{
			const float angle = Noise(index * 4 + 20) * Pi * 2;
			const float radius = 65.0f + Noise(index * 4 + 21) * 440.0f;
			const float age = time - CollapseSeconds - radius / 1000.0f;
			if (age < 0 || age > 1.15f)
			{
				continue;
			}
			const float depth = 1.0f / (1.0f + age * 4.0f);
			const float x = CenterX + std::cos(angle) * radius * depth;
			const float y = CenterY + (std::sin(angle) * radius + age * age * 460.0f) * depth;
			const float size = (4.0f + Noise(index * 4 + 22) * 13.0f) * depth;
			const float turn = angle + age * (Noise(index * 4 + 23) - 0.5f) * 9.0f;
			const Gdiplus::PointF points[] = { { x + std::cos(turn) * size, y + std::sin(turn) * size },
				{ x + std::cos(turn + 2.3f) * size, y + std::sin(turn + 2.3f) * size },
				{ x + std::cos(turn + 4.1f) * size * 0.5f, y + std::sin(turn + 4.1f) * size * 0.5f } };
			const BYTE alpha = static_cast<BYTE>(230 * (1.0f - Smooth(age / 1.15f)));
			Gdiplus::SolidBrush body(Gdiplus::Color(alpha, 63, 70, 86));
			Gdiplus::Pen rim(Gdiplus::Color(alpha, 255, 200, 121), 0.85f);
			canvas.FillPolygon(&body, points, 3);
			canvas.DrawLine(&rim, points[0], points[1]);
		}
		canvas.Restore(state);
	}

	Gdiplus::Bitmap* StartupBannerFracture::Render(float time, Gdiplus::Bitmap& banner)
	{
		Gdiplus::Graphics canvas(Frame.get());
		// Fill the backing pixels directly; avoid blending a screen-sized black bitmap.
		const BYTE darkness = static_cast<BYTE>(255 * (1.0f - Smooth((time - 3.10f) / 0.30f)));
		canvas.Clear(Gdiplus::Color(darkness, 0, 0, 0));
		if (time < StartupBannerIntro::DurationSeconds)
		{
			// An opaque underlay is essential: alpha alone would reveal the unbroken
			// real desktop underneath, destroying the illusion of an open chasm.
			const auto state = canvas.Save();
			const float impactAge = time - CollapseSeconds;
			if (impactAge > 0.0f && impactAge < 0.14f)
			{
				const float kick = 7.0f * std::exp(-impactAge * 18.0f) * Scale;
				canvas.TranslateTransform(std::round(std::sin(impactAge * 95.0f) * kick), std::round(std::cos(impactAge * 77.0f) * kick));
			}
			for (const Shard& shard : Shards)
			{
				DrawShard(canvas, shard, time);
			}
			DrawCracks(canvas, time);
			DrawDebris(canvas, time);
			canvas.Restore(state);
			const float impact = Clamp(1.0f - std::abs(time - CollapseSeconds - 0.025f) / 0.055f);
			if (impact > 0)
			{
				Gdiplus::SolidBrush flash(Gdiplus::Color(static_cast<BYTE>(170 * impact), 225, 244, 255));
				canvas.FillEllipse(&flash, (CenterX - 130) * Scale, (CenterY - 90) * Scale, 260 * Scale, 180 * Scale);
			}
		}
		else
		{
			Shards.clear();
			Cracks.clear();
		}
		FadeImage(canvas, banner, static_cast<int>(Frame->GetWidth() - banner.GetWidth()) / 2,
			static_cast<int>(Frame->GetHeight() - banner.GetHeight()) / 2, Smooth((time - 3.02f) / 0.38f));
		return Frame.get();
	}
}
