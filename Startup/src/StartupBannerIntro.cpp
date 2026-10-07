#ifndef NOMINMAX
	#define NOMINMAX
#endif
#include "StartupBannerIntro.h"
#include "StartupBannerAssembleRenderer.h"
#include "StartupBannerRiftRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace
{
	using FragmentPolygon = std::vector<Gdiplus::PointF>;
	constexpr float Pi = 3.14159265f;
	constexpr float StrikeSeconds = 0.48f;
	constexpr float RecombineStartSeconds = 0.88f;
	constexpr float ImpactX = 520.0f;
	constexpr float ImpactY = 355.0f;

	struct Cut
	{
		Gdiplus::PointF Start;
		Gdiplus::PointF End;
		float Time;
	};

	// The light trails and the polygon cuts share their geometry.
	const std::array<Cut, 7> Cuts = { {
		{ { 42, 594 }, { 997, 118 }, 0.20f },
		{ { 166, 30 }, { 872, 685 }, 0.25f },
		{ { 20, 219 }, { 1014, 468 }, 0.30f },
		{ { 736, 18 }, { 337, 702 }, 0.35f },
		{ { 76, 681 }, { 753, 54 }, 0.40f },
		{ { 8, 422 }, { 1020, 316 }, 0.45f },
		{ { 361, 22 }, { 623, 708 }, 0.50f },
	} };

	float Saturate(float value)
	{
		return std::clamp(value, 0.0f, 1.0f);
	}

	float SmoothStep(float value)
	{
		const float t = Saturate(value);
		return t * t * (3.0f - 2.0f * t);
	}

	float EaseOutCubic(float value)
	{
		const float remaining = 1.0f - Saturate(value);
		return 1.0f - remaining * remaining * remaining;
	}

	// Stable pseudo-random values keep playback and frame review identical.
	float Noise(unsigned int seed)
	{
		seed = (seed ^ 61u) ^ (seed >> 16);
		seed *= 9u;
		seed ^= seed >> 4;
		seed *= 0x27d4eb2du;
		seed ^= seed >> 15;
		return (seed & 65535u) / 65535.0f;
	}

	float SideOfCut(const Gdiplus::PointF& point, const Cut& cut)
	{
		return (cut.End.X - cut.Start.X) * (point.Y - cut.Start.Y) -
			(cut.End.Y - cut.Start.Y) * (point.X - cut.Start.X);
	}

	FragmentPolygon ClipToSide(const FragmentPolygon& polygon, const Cut& cut, float side)
	{
		FragmentPolygon result;
		for (size_t index = 0; index < polygon.size(); ++index)
		{
			const auto& a = polygon[index];
			const auto& b = polygon[(index + 1) % polygon.size()];
			const float distanceA = SideOfCut(a, cut) * side;
			const float distanceB = SideOfCut(b, cut) * side;
			if (distanceA >= 0.0f)
			{
				result.push_back(a);
			}
			if ((distanceA < 0.0f) != (distanceB < 0.0f))
			{
				const float t = distanceA / (distanceA - distanceB);
				result.emplace_back(a.X + (b.X - a.X) * t, a.Y + (b.Y - a.Y) * t);
			}
		}
		return result;
	}

	void DrawImage(Gdiplus::Graphics& canvas, Gdiplus::Bitmap& image,
		int x, int y, float opacity, float scale = 1.0f)
	{
		if (opacity <= 0.0f || scale <= 0.0f)
		{
			return;
		}
		if (opacity >= 1.0f && scale == 1.0f)
		{
			canvas.DrawImage(&image, x, y, static_cast<int>(image.GetWidth()), static_cast<int>(image.GetHeight()));
			return;
		}
		Gdiplus::ColorMatrix matrix = {
			1, 0, 0, 0, 0,
			0, 1, 0, 0, 0,
			0, 0, 1, 0, 0,
			0, 0, 0, Saturate(opacity), 0,
			0, 0, 0, 0, 1
		};
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&matrix);
		const int width = image.GetWidth();
		const int height = image.GetHeight();
		const Gdiplus::Rect destination(x, y, static_cast<int>(width * scale), static_cast<int>(height * scale));
		canvas.DrawImage(&image, destination, 0, 0, width, height, Gdiplus::UnitPixel, &attributes);
	}
}

namespace Nyx
{
	void StartupBannerIntro::Prepare(int width, int height, Gdiplus::Bitmap& banner,
		EStartupBannerMode mode, Gdiplus::Bitmap* desktop)
	{
		Mode = mode;
		if (Mode == EStartupBannerMode::Chasm)
		{
			Fracture = std::make_unique<StartupBannerFracture>();
			Fracture->Prepare(width, height, banner, desktop);
			return;
		}
		Fracture.reset();
		SoftwareIntro.reset();
		bLiveBanner = false;
		BannerOrigin = Mode == EStartupBannerMode::AssembleV2 ? Gdiplus::PointF(0.0f, 0.0f) : Gdiplus::PointF(BannerX, BannerY);
		Scale = width / Width;
		FrameBuffer = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
		// These intros are drawn on the CPU: they need per-pixel work (additive light,
		// thousands of moving tiles) that GDI+ cannot do at frame rate.
		std::unique_ptr<ISoftwareIntro> softwareIntro;
		if (Mode == EStartupBannerMode::Rift)
		{
			softwareIntro = std::make_unique<StartupBannerRiftRenderer>();
		}
		else if (Mode == EStartupBannerMode::Assemble)
		{
			softwareIntro = std::make_unique<StartupBannerAssembleRenderer>(StartupBannerAssembleRenderer::AroundBanner());
		}
		else if (Mode == EStartupBannerMode::AssembleV2)
		{
			softwareIntro = std::make_unique<StartupBannerAssembleRenderer>(StartupBannerAssembleRenderer::BannerOnly());
		}
		if (softwareIntro)
		{
			Fragments.clear();
			FragmentPoses.clear();
			SoftwareIntro = std::make_unique<StartupBannerSoftwareIntro>(std::move(softwareIntro));
			SoftwareIntro->Prepare(width, height, banner, desktop, BannerOrigin.X, BannerOrigin.Y);
			bLiveBanner = SoftwareIntro->UsesLiveBanner();
			return;
		}
		Fragments.clear();
		FragmentPoses.clear();
		Bolts.clear();
		Lightning.reset();
		ImpactBurst.reset();
		PrepareFragments(desktop ? *desktop : banner, desktop != nullptr);
		if (Mode == EStartupBannerMode::Lightning)
		{
			PrepareLightning();
		}
		else
		{
			PrepareImpact();
		}

		// Cache the radial light once; each frame only scales and fades this image.
		const int glowSize = static_cast<int>(860 * Scale);
		Glow = std::make_unique<Gdiplus::Bitmap>(glowSize, glowSize, PixelFormat32bppPARGB);
		Gdiplus::Rect glowBounds(0, 0, glowSize, glowSize);
		Gdiplus::BitmapData glowData{};
		if (Glow->LockBits(&glowBounds, Gdiplus::ImageLockModeWrite, PixelFormat32bppPARGB, &glowData) != Gdiplus::Ok)
		{
			return;
		}
		const float glowCenter = (glowSize - 1) * 0.5f;
		for (int y = 0; y < glowSize; ++y)
		{
			auto* pixels = static_cast<BYTE*>(glowData.Scan0) + y * glowData.Stride;
			for (int x = 0; x < glowSize; ++x)
			{
				const float dx = (x - glowCenter) / glowCenter;
				const float dy = (y - glowCenter) / glowCenter;
				const float falloff = Saturate(1.0f - std::sqrt(dx * dx + dy * dy));
				const BYTE alpha = static_cast<BYTE>(255 * falloff * falloff);
				const bool bIceBlue = Mode == EStartupBannerMode::RealityCut;
				const float heat = falloff * falloff * falloff * falloff;
				pixels[x * 4] = static_cast<BYTE>((bIceBlue ? 255 : 38 + 195 * heat) * alpha / 255);
				pixels[x * 4 + 1] = static_cast<BYTE>((bIceBlue ? 175 + 80 * heat : 157 + 98 * heat) * alpha / 255);
				pixels[x * 4 + 2] = static_cast<BYTE>((bIceBlue ? 65 + 190 * heat : 255) * alpha / 255);
				pixels[x * 4 + 3] = alpha;
			}
		}
		Glow->UnlockBits(&glowData);
	}

	void StartupBannerIntro::PrepareFragments(Gdiplus::Bitmap& source, bool bDesktop)
	{
		const float left = bDesktop ? 0.0f : BannerX;
		const float top = bDesktop ? 0.0f : BannerY;
		const float right = bDesktop ? Width : BannerX + 640.0f;
		const float bottom = bDesktop ? Height : BannerY + 360.0f;
		std::vector<FragmentPolygon> polygons = { { { left, top }, { right, top }, { right, bottom }, { left, bottom } } };
		for (const Cut& cut : Cuts)
		{
			std::vector<FragmentPolygon> split;
			for (const auto& polygon : polygons)
			{
				for (float side : { -1.0f, 1.0f })
				{
					auto piece = ClipToSide(polygon, cut, side);
					if (piece.size() >= 3)
					{
						split.push_back(std::move(piece));
					}
				}
			}
			polygons = std::move(split);
		}

		for (auto& polygon : polygons)
		{
			float minX = Width, minY = Height, maxX = 0.0f, maxY = 0.0f;
			Gdiplus::PointF center{};
			for (const auto& point : polygon)
			{
				minX = std::min(minX, point.X);
				minY = std::min(minY, point.Y);
				maxX = std::max(maxX, point.X);
				maxY = std::max(maxY, point.Y);
				center.X += point.X / static_cast<float>(polygon.size());
				center.Y += point.Y / static_cast<float>(polygon.size());
			}
			const int x = static_cast<int>(std::floor(minX * Scale));
			const int y = static_cast<int>(std::floor(minY * Scale));
			const int width = static_cast<int>(std::ceil(maxX * Scale)) - x;
			const int height = static_cast<int>(std::ceil(maxY * Scale)) - y;
			if (width < 2 || height < 2)
			{
				continue;
			}
			Fragment fragment;
			fragment.Image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
			for (auto& point : polygon)
			{
				point.X = point.X * Scale - x;
				point.Y = point.Y * Scale - y;
			}
			{
				Gdiplus::Graphics graphics(fragment.Image.get());
				graphics.Clear(Gdiplus::Color(0, 0, 0, 0));
				Gdiplus::GraphicsPath clip;
				clip.AddPolygon(polygon.data(), static_cast<int>(polygon.size()));
				graphics.SetClip(&clip);
				graphics.DrawImage(&source, Gdiplus::Rect(0, 0, width, height),
					x - static_cast<int>(left * Scale), y - static_cast<int>(top * Scale),
					width, height, Gdiplus::UnitPixel);
			}
			if (bDesktop)
			{
				// Feather only the capture boundary, preserving sharp fracture edges.
				Gdiplus::Rect bounds(0, 0, width, height);
				Gdiplus::BitmapData data{};
				if (fragment.Image->LockBits(&bounds, Gdiplus::ImageLockModeRead | Gdiplus::ImageLockModeWrite,
						PixelFormat32bppPARGB, &data) == Gdiplus::Ok)
				{
					for (int row = 0; row < height; ++row)
					{
						auto* pixels = static_cast<BYTE*>(data.Scan0) + row * data.Stride;
						for (int column = 0; column < width; ++column)
						{
							const float edge = std::min({ (x + column) / Scale, (y + row) / Scale,
								Width - (x + column) / Scale, Height - (y + row) / Scale });
							const float alpha = SmoothStep(edge / 90.0f);
							for (int channel = 0; channel < 4; ++channel)
							{
								pixels[column * 4 + channel] = static_cast<BYTE>(pixels[column * 4 + channel] * alpha);
							}
						}
					}
					fragment.Image->UnlockBits(&data);
				}
			}
			const auto index = static_cast<unsigned int>(Fragments.size());
			fragment.Destination = Gdiplus::PointF(static_cast<float>(x), static_cast<float>(y));
			fragment.Center = center;
			fragment.Displacement = Gdiplus::PointF(
				(center.X - ImpactX) * 0.7f + (Noise(index * 3) - 0.5f) * 80.0f,
				(center.Y - ImpactY) * 0.7f + (Noise(index * 3 + 1) - 0.5f) * 80.0f);
			fragment.DelaySeconds = Noise(index * 3 + 2) * 0.20f;
			fragment.Outline = std::move(polygon);
			fragment.Tilt = Gdiplus::PointF((Noise(index * 5 + 113) - 0.5f) * 0.48f,
				(Noise(index * 5 + 114) - 0.5f) * 0.58f);
			fragment.Spin = (Noise(index * 5 + 115) - 0.5f) * 0.18f;
			fragment.Depth = (Noise(index * 5 + 116) - 0.5f) * 100.0f;
			Fragments.push_back(std::move(fragment));
		}
		FragmentPoses.reserve(Fragments.size());
	}

	void StartupBannerIntro::PrepareLightning()
	{
		auto addBolt = [this](Gdiplus::PointF start, Gdiplus::PointF end, float width, unsigned int seed)
		{
			Bolt bolt;
			bolt.Width = width;
			const float dx = end.X - start.X;
			const float dy = end.Y - start.Y;
			const float length = std::sqrt(dx * dx + dy * dy);
			constexpr int segments = 24;
			for (int index = 0; index <= segments; ++index)
			{
				const float t = index / static_cast<float>(segments);
				const float jitter = (Noise(seed + index) - 0.5f) * std::sin(t * Pi) * length * 0.19f;
				bolt.Points.emplace_back(start.X + dx * t - dy / length * jitter,
					start.Y + dy * t + dx / length * jitter);
			}
			Bolts.push_back(std::move(bolt));
		};
		addBolt({ 590, 16 }, { ImpactX, ImpactY }, 4.4f, 11);
		addBolt({ ImpactX, ImpactY }, { 493, 701 }, 2.7f, 71);
		for (unsigned int index = 0; index < 14; ++index)
		{
			const auto start = Bolts[0].Points[4 + index];
			const float side = index % 2 == 0 ? -1.0f : 1.0f;
			const Gdiplus::PointF end(start.X + side * (90.0f + Noise(index + 42) * 300.0f),
				start.Y + 65.0f + Noise(index + 97) * 200.0f);
			addBolt(start, end, 0.6f + Noise(index) * 1.1f, 100 + index * 30);
			const auto twigStart = Bolts.back().Points[12];
			addBolt(twigStart, { end.X + side * 40.0f, end.Y - 100.0f }, 0.45f, 600 + index * 30);
		}

		// Rasterize the branched discharge once. Drawing dozens of wide, glowing
		// paths every frame is costly in GDI+, particularly at 200% DPI.
		Lightning = std::make_unique<Gdiplus::Bitmap>(FrameBuffer->GetWidth(), FrameBuffer->GetHeight(), PixelFormat32bppPARGB);
		Gdiplus::Graphics canvas(Lightning.get());
		canvas.Clear(Gdiplus::Color(0, 0, 0, 0));
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		for (const Bolt& bolt : Bolts)
		{
			const float widths[] = { 11.0f, 4.0f, 1.0f, 0.36f };
			const BYTE alphas[] = { 12, 48, 235, 255 };
			for (int layer = 0; layer < 4; ++layer)
			{
				// The innermost layer is the white-hot core; the others are amber glow.
				const Gdiplus::Color color = layer == 3
					? Gdiplus::Color(alphas[layer], 255, 253, 229)
					: Gdiplus::Color(alphas[layer], 255, 162, 35);
				Gdiplus::Pen pen(color, bolt.Width * widths[layer]);
				pen.SetLineJoin(Gdiplus::LineJoinRound);
				canvas.DrawLines(&pen, bolt.Points.data(), static_cast<int>(bolt.Points.size()));
			}
		}
	}

	void StartupBannerIntro::PrepareImpact()
	{
		// Cache the finishing flash at native size. Only its opacity changes during
		// playback, keeping the dense radial streaks out of the per-frame path.
		ImpactBurst = std::make_unique<Gdiplus::Bitmap>(FrameBuffer->GetWidth(), FrameBuffer->GetHeight(), PixelFormat32bppPARGB);
		Gdiplus::Graphics canvas(ImpactBurst.get());
		canvas.Clear(Gdiplus::Color(0, 0, 0, 0));
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		Gdiplus::GraphicsPath halo;
		halo.AddEllipse(ImpactX - 480.0f, ImpactY - 325.0f, 960.0f, 650.0f);
		Gdiplus::PathGradientBrush bloom(&halo);
		bloom.SetCenterPoint(Gdiplus::PointF(ImpactX, ImpactY));
		bloom.SetCenterColor(Gdiplus::Color(255, 246, 252, 255));
		Gdiplus::Color transparent(0, 55, 120, 255);
		int colorCount = 1;
		bloom.SetSurroundColors(&transparent, &colorCount);
		canvas.FillPath(&bloom, &halo);
		for (unsigned int index = 0; index < 85; ++index)
		{
			const float angle = Noise(index * 7 + 211) * Pi * 2.0f;
			const float dx = std::cos(angle), dy = std::sin(angle);
			const float start = 25.0f + Noise(index * 7 + 212) * 100.0f;
			const float reach = 210.0f + Noise(index * 7 + 213) * 330.0f;
			const float width = 2.0f + Noise(index * 7 + 214) * 12.0f;
			const Gdiplus::PointF blade[] = {
				{ ImpactX + dx * start, ImpactY + dy * start },
				{ ImpactX + dx * reach - dy * width, ImpactY + dy * reach + dx * width },
				{ ImpactX + dx * (reach + 95.0f), ImpactY + dy * (reach + 95.0f) }
			};
			const BYTE alpha = static_cast<BYTE>(75 + Noise(index * 7 + 215) * 170);
			const Gdiplus::Color color = index % 3 == 0
				? Gdiplus::Color(alpha, 90, 172, 255)
				: Gdiplus::Color(alpha, 237, 247, 255);
			Gdiplus::SolidBrush streak(color);
			canvas.FillPolygon(&streak, blade, 3);
		}
		Gdiplus::SolidBrush core(Gdiplus::Color(245, 250, 254, 255));
		canvas.FillEllipse(&core, ImpactX - 52.0f, ImpactY - 27.0f, 104.0f, 54.0f);
	}

	void StartupBannerIntro::DrawImpact(Gdiplus::Graphics& canvas, float elapsedSeconds)
	{
		const float age = elapsedSeconds - RealityImpactSeconds;
		if (age < 0.0f || age >= 0.65f)
		{
			return;
		}
		// Hold the hot impact silhouette briefly, then let a narrow pressure ring
		// outrun the fading light. This lands after the desktop has fully recombined.
		const float flash = std::exp(-std::max(0.0f, age - 0.055f) * 15.0f);
		DrawImage(canvas, *ImpactBurst, 0, 0, flash);
		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float radius = 40.0f + EaseOutCubic(age / 0.50f) * 460.0f;
		const float fade = 1.0f - Saturate(age / 0.50f);
		Gdiplus::Pen wave(Gdiplus::Color(static_cast<BYTE>(210 * fade), 195, 228, 255), 1.0f + 4.0f * fade);
		canvas.DrawEllipse(&wave, ImpactX - radius, ImpactY - radius * 0.56f, radius * 2, radius * 1.12f);
		canvas.Restore(state);
	}

	void StartupBannerIntro::DrawGlow(Gdiplus::Graphics& canvas, float radius, float opacity)
	{
		DrawImage(canvas, *Glow, static_cast<int>((ImpactX - 430.0f) * Scale),
			static_cast<int>((ImpactY - 430.0f) * Scale), opacity * radius / 430.0f);
	}

	void StartupBannerIntro::DrawBanner(Gdiplus::Graphics& canvas, Gdiplus::Bitmap& banner, float opacity)
	{
		DrawImage(canvas, banner, static_cast<int>(BannerOrigin.X * Scale), static_cast<int>(BannerOrigin.Y * Scale), opacity);
	}

	void StartupBannerIntro::DrawFragments(Gdiplus::Graphics& canvas, float elapsedSeconds)
	{
		const bool bRealityCut = Mode == EStartupBannerMode::RealityCut;
		FragmentPoses.clear();
		for (const Fragment& fragment : Fragments)
		{
			float dx = 0.0f, dy = 0.0f, opacity = 1.0f;
			if (bRealityCut)
			{
				// Reach exact alignment before the impact; a short still hold makes the
				// finishing blow read separately from the initial cuts.
				const float recombineDuration = RealityRecombinedSeconds - RecombineStartSeconds;
				const float returnToPlace = 1.0f - SmoothStep((elapsedSeconds - RecombineStartSeconds) / recombineDuration);
				for (const Cut& cut : Cuts)
				{
					const float impact = EaseOutCubic((elapsedSeconds - cut.Time) / 0.045f);
					const float direction = SideOfCut(fragment.Center, cut) >= 0.0f ? 1.0f : -1.0f;
					const float vx = cut.End.X - cut.Start.X;
					const float vy = cut.End.Y - cut.Start.Y;
					const float length = std::sqrt(vx * vx + vy * vy);
					dx += -vy / length * direction * 20.0f * impact * returnToPlace;
					dy += vx / length * direction * 20.0f * impact * returnToPlace;
				}
				const float impactAge = elapsedSeconds - RealityImpactSeconds;
				if (impactAge >= 0.0f)
				{
					const float shake = 11.0f * std::exp(-impactAge * 17.0f);
					dx += std::cos(impactAge * 105.0f) * shake;
					dy += std::sin(impactAge * 83.0f) * shake * 0.6f;
				}
				opacity = 1.0f - SmoothStep((impactAge - 0.10f) / 0.40f);
				const float age = std::max(0.0f, elapsedSeconds - Cuts.front().Time - fragment.DelaySeconds);
				const float kick = 1.0f - std::exp(-age * 7.0f) * std::cos(age * 9.0f);
				const float motion = kick * returnToPlace * 1.7f;
				FragmentPoses.push_back({ &fragment, { dx, dy }, motion, fragment.Depth * motion, opacity });
				continue;
			}
			else
			{
				const float assemble = Saturate((elapsedSeconds - 0.82f - fragment.DelaySeconds) / 1.65f);
				const float remaining = 1.0f - EaseOutCubic(assemble);
				dx = fragment.Displacement.X * remaining;
				dy = fragment.Displacement.Y * remaining;
				opacity = SmoothStep(assemble * 2.5f);
			}
			const int x = static_cast<int>(std::round(fragment.Destination.X + dx * Scale));
			const int y = static_cast<int>(std::round(fragment.Destination.Y + dy * Scale));
			DrawImage(canvas, *fragment.Image, x, y, opacity);
		}
		// Far shards are drawn first so crossing pieces retain a coherent sense of depth.
		std::stable_sort(FragmentPoses.begin(), FragmentPoses.end(), [](const FragmentPose& a, const FragmentPose& b)
			{
				return a.Depth < b.Depth;
			});
		for (const FragmentPose& pose : FragmentPoses)
		{
			DrawGlassFragment(canvas, pose);
		}
	}

	void StartupBannerIntro::DrawGlassFragment(Gdiplus::Graphics& canvas, const FragmentPose& pose)
	{
		if (pose.Opacity <= 0.0f)
		{
			return;
		}
		const Fragment& fragment = *pose.Piece;
		if (pose.Motion < 0.001f)
		{
			DrawImage(canvas, *fragment.Image, static_cast<int>(fragment.Destination.X + pose.Offset.X * Scale),
				static_cast<int>(fragment.Destination.Y + pose.Offset.Y * Scale), pose.Opacity);
			return;
		}

		const float pitch = fragment.Tilt.X * pose.Motion;
		const float yaw = fragment.Tilt.Y * pose.Motion;
		const float roll = fragment.Spin * pose.Motion;
		const float sinPitch = std::sin(pitch), cosPitch = std::cos(pitch);
		const float sinYaw = std::sin(yaw), cosYaw = std::cos(yaw);
		const float sinRoll = std::sin(roll), cosRoll = std::cos(roll);
		constexpr float cameraDistance = 1100.0f;
		const float perspective = cameraDistance / (cameraDistance - pose.Depth);
		const Gdiplus::PointF pivot(fragment.Center.X * Scale - fragment.Destination.X,
			fragment.Center.Y * Scale - fragment.Destination.Y);

		// Weak perspective: project the rotated plane using its center depth. This
		// gives foreshortening and parallax with one textured parallelogram per shard.
		const auto project = [&](Gdiplus::PointF point)
		{
			const float x = point.X - pivot.X;
			const float y = point.Y - pivot.Y;
			const float tiltedX = cosYaw * x + sinYaw * sinPitch * y;
			const float tiltedY = cosPitch * y;
			const float centerX = (fragment.Center.X + pose.Offset.X - ImpactX) * Scale;
			const float centerY = (fragment.Center.Y + pose.Offset.Y - ImpactY) * Scale;
			return Gdiplus::PointF(ImpactX * Scale + (centerX + cosRoll * tiltedX - sinRoll * tiltedY) * perspective,
				ImpactY * Scale + (centerY + sinRoll * tiltedX + cosRoll * tiltedY) * perspective);
		};
		const float width = static_cast<float>(fragment.Image->GetWidth());
		const float height = static_cast<float>(fragment.Image->GetHeight());
		const Gdiplus::PointF corners[] = { project({ 0, 0 }), project({ width, 0 }), project({ 0, height }) };
		const float light = 1.0f - std::min(0.42f, std::abs(sinYaw) * 0.48f + std::abs(sinPitch) * 0.24f);
		Gdiplus::ColorMatrix matrix = {
			light, 0, 0, 0, 0,
			0, light, 0, 0, 0,
			0, 0, light, 0, 0,
			0, 0, 0, pose.Opacity, 0,
			0, 0, 0, 0, 1
		};
		Gdiplus::ImageAttributes attributes;
		attributes.SetColorMatrix(&matrix);
		const auto state = canvas.Save();
		// Native-resolution screenshot textures need no expensive reconstruction
		// filter during their brief movement; keep outlines antialiased separately.
		canvas.SetInterpolationMode(Gdiplus::InterpolationModeNearestNeighbor);
		canvas.DrawImage(fragment.Image.get(), corners, 3, 0.0f, 0.0f, width, height, Gdiplus::UnitPixel, &attributes);

		// A narrow shaded edge and a directional glint suggest cut glass thickness.
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		const float edgeOpacity = pose.Opacity * Saturate(pose.Motion);
		for (size_t index = 0; index < fragment.Outline.size(); ++index)
		{
			const auto& a = fragment.Outline[index];
			const auto& b = fragment.Outline[(index + 1) % fragment.Outline.size()];
			const auto screenA = project(a);
			const auto screenB = project(b);
			const float midpointX = (fragment.Destination.X + (a.X + b.X) * 0.5f) / Scale;
			const float midpointY = (fragment.Destination.Y + (a.Y + b.Y) * 0.5f) / Scale;
			const float edgeFade = Saturate(std::min({ midpointX, midpointY, Width - midpointX, Height - midpointY }) / 90.0f);
			const float glint = 0.25f + 0.75f * std::abs(std::sin(roll + std::atan2(b.Y - a.Y, b.X - a.X) - 0.7f));
			Gdiplus::Pen bevel(Gdiplus::Color(static_cast<BYTE>(65 * edgeOpacity * edgeFade), 45, 38, 88), 2.2f * Scale);
			canvas.DrawLine(&bevel, screenA, screenB);
			Gdiplus::Pen rim(Gdiplus::Color(static_cast<BYTE>(150 * glint * edgeOpacity * edgeFade), 209, 222, 255), 0.8f * Scale);
			canvas.DrawLine(&rim, screenA, screenB);
		}
		canvas.Restore(state);
	}

	void StartupBannerIntro::DrawLightning(Gdiplus::Graphics& canvas, float elapsedSeconds)
	{
		const float age = elapsedSeconds - StrikeSeconds;
		const float charge = Saturate(elapsedSeconds / StrikeSeconds);
		const float energy = age < 0.0f
			? 0.10f * charge
			: std::exp(-age * 3.6f) * (0.80f + 0.20f * std::cos(age * 32.0f));
		DrawGlow(canvas, age < 0.0f ? 100.0f : 240.0f + 160.0f * EaseOutCubic(age),
			age < 0.0f ? charge * 0.25f : energy * 3.5f);
		if (age >= 0.0f && energy > 0.005f)
		{
			DrawImage(canvas, *Lightning, 0, 0, Saturate(energy * 1.7f));
		}

		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		if (age >= 0.0f && age < 0.85f)
		{
			const float radius = 30.0f + EaseOutCubic(age / 0.85f) * 490.0f;
			const float alpha = 1.0f - age / 0.85f;
			Gdiplus::Pen halo(Gdiplus::Color(static_cast<BYTE>(100 * alpha), 255, 190, 69), 3.0f * alpha + 0.5f);
			canvas.DrawEllipse(&halo, ImpactX - radius, ImpactY - radius * 0.46f, radius * 2, radius * 0.92f);
			Gdiplus::Pen echo(Gdiplus::Color(static_cast<BYTE>(45 * alpha), 255, 114, 24), 8.0f * alpha);
			canvas.DrawEllipse(&echo, ImpactX - radius * 0.9f, ImpactY - radius * 0.41f, radius * 1.8f, radius * 0.82f);
		}
		if (age < 0.0f)
		{
			const auto& bolt = Bolts.front();
			const int count = std::max(2, static_cast<int>(bolt.Points.size() * charge));
			Gdiplus::Pen pen(Gdiplus::Color(static_cast<BYTE>(80 * charge), 255, 185, 74), 1.2f);
			canvas.DrawLines(&pen, bolt.Points.data(), count);
		}
		canvas.Restore(state);
	}

	void StartupBannerIntro::DrawCuts(Gdiplus::Graphics& canvas, float elapsedSeconds)
	{
		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		for (const Cut& cut : Cuts)
		{
			const float age = elapsedSeconds - cut.Time;
			if (age < 0.0f || elapsedSeconds >= RealityRecombinedSeconds)
			{
				continue;
			}
			const float travel = EaseOutCubic(age / 0.025f);
			const float flare = std::exp(-std::max(0.0f, age - 0.045f) * 15.0f);
			const float afterglow = (1.0f - SmoothStep((elapsedSeconds - 0.65f) / 1.0f)) * 0.28f;
			const auto end = Gdiplus::PointF(cut.Start.X + (cut.End.X - cut.Start.X) * travel,
				cut.Start.Y + (cut.End.Y - cut.Start.Y) * travel);
			const float widths[] = { 35, 13, 3.0f };
			const int alphas[] = { 35, 140, 255 };
			for (int layer = 0; layer < 3; ++layer)
			{
				const BYTE alpha = static_cast<BYTE>(alphas[layer] * Saturate(flare + afterglow));
				const Gdiplus::Color color = layer == 2
					? Gdiplus::Color(alpha, 250, 254, 255)
					: Gdiplus::Color(alpha, 60, 165, 255);
				Gdiplus::Pen pen(color, widths[layer] * (0.25f + 0.75f * flare));

				canvas.DrawLine(&pen, cut.Start, end);
			}
			// Tapered white blades give each cut a two-to-three-frame impact silhouette.
			if (flare > 0.10f)
			{
				const float vx = end.X - cut.Start.X, vy = end.Y - cut.Start.Y;
				const float length = std::max(1.0f, std::sqrt(vx * vx + vy * vy));
				const float bladeWidth = 12.0f * flare;
				const Gdiplus::PointF middle(cut.Start.X + vx * 0.55f, cut.Start.Y + vy * 0.55f);
				const Gdiplus::PointF blade[] = { cut.Start,
					{ middle.X - vy / length * bladeWidth, middle.Y + vx / length * bladeWidth }, end,
					{ middle.X + vy / length * bladeWidth, middle.Y - vx / length * bladeWidth } };
				Gdiplus::SolidBrush white(Gdiplus::Color(static_cast<BYTE>(255 * flare), 250, 254, 255));
				canvas.FillPolygon(&white, blade, 4);
			}
		}
		canvas.Restore(state);
	}

	void StartupBannerIntro::DrawParticles(Gdiplus::Graphics& canvas, float elapsedSeconds)
	{
		const bool bRealityCut = Mode == EStartupBannerMode::RealityCut;
		const float age = elapsedSeconds - (bRealityCut ? RealityImpactSeconds : StrikeSeconds);
		if (age < 0.0f)
		{
			return;
		}
		const auto state = canvas.Save();
		canvas.ScaleTransform(Scale, Scale);
		canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
		for (unsigned int index = 0; index < 75; ++index)
		{
			const float life = bRealityCut ? 1.15f : 1.5f + Noise(index) * 1.1f;
			const float fade = Saturate(1.0f - age / life);
			const float angle = Noise(index * 7 + 9) * Pi * 2.0f;
			const float speed = 90.0f + Noise(index * 7 + 3) * 300.0f;
			const float distance = 30.0f + speed * (1.0f - std::exp(-age * 1.6f));
			const float x = ImpactX + std::cos(angle) * distance;
			const float y = ImpactY + std::sin(angle) * distance * 0.7f + age * age * 16.0f;
			const BYTE alpha = static_cast<BYTE>(200 * fade * fade);
			const auto color = bRealityCut ? Gdiplus::Color(alpha, 190, 195, 255) : Gdiplus::Color(alpha, 255, 183, 73);
			Gdiplus::Pen pen(color, index % 4 == 0 ? 1.8f : 0.8f);
			const float trail = (bRealityCut ? 13.0f : 6.0f) * fade;
			canvas.DrawLine(&pen, x, y, x - std::cos(angle) * trail, y - std::sin(angle) * trail);
		}
		canvas.Restore(state);
	}

	Gdiplus::Bitmap* StartupBannerIntro::Render(float elapsedSeconds, Gdiplus::Bitmap& banner)
	{
		if (Mode == EStartupBannerMode::Chasm)
		{
			return Fracture->Render(elapsedSeconds, banner);
		}
		if (SoftwareIntro && elapsedSeconds < DurationSeconds)
		{
			// Written through LockBits, so no Graphics object may be open on the buffer.
			SoftwareIntro->Render(elapsedSeconds, *FrameBuffer, banner);
			return FrameBuffer.get();
		}
		// The intro's captured pixels and any worker threads are released with it.
		SoftwareIntro.reset();
		Gdiplus::Graphics canvas(FrameBuffer.get());
		canvas.Clear(Gdiplus::Color(0, 0, 0, 0));
		if (elapsedSeconds >= DurationSeconds)
		{
			DrawBanner(canvas, banner, 1.0f);
			// Release the captured pixels as soon as the intro ends. Never save them.
			FragmentPoses.clear();
			Fragments.clear();
			return FrameBuffer.get();
		}
		if (Mode == EStartupBannerMode::RealityCut)
		{
			DrawBanner(canvas, banner, SmoothStep((elapsedSeconds - RealityImpactSeconds - 0.08f) / 0.40f));
			DrawFragments(canvas, elapsedSeconds);
			const float cutBloom = SmoothStep((elapsedSeconds - 0.18f) / 0.06f) *
				(1.0f - SmoothStep((elapsedSeconds - 0.50f) / 0.30f));
			if (cutBloom > 0.0f)
			{
				DrawGlow(canvas, 430.0f, cutBloom);
			}
			DrawCuts(canvas, elapsedSeconds);
			DrawImpact(canvas, elapsedSeconds);
		}
		else
		{
			if (elapsedSeconds < 2.70f)
			{
				DrawFragments(canvas, elapsedSeconds);
			}
			else
			{
				DrawBanner(canvas, banner, 1.0f);
			}
			DrawLightning(canvas, elapsedSeconds);
		}
		DrawParticles(canvas, elapsedSeconds);
		return FrameBuffer.get();
	}
}
