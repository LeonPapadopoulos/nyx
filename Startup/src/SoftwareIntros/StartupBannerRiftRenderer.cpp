#include "StartupBannerRiftRenderer.h"

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <thread>

#if defined(_M_X64) || defined(__SSE2__)
	#define NYX_RIFT_SSE2 1
	#include <emmintrin.h>
#endif

namespace
{
	using Vec2 = Nyx::StartupBannerRiftRenderer::Vec2;
	using Color = Nyx::StartupBannerRiftRenderer::Color;
	using Renderer = Nyx::StartupBannerRiftRenderer;

	constexpr float Pi = 3.14159265f;
	constexpr float WebStartSeconds = 0.64f;
	constexpr float WebSpeed = 1700.0f; // Logical pixels per second.
	constexpr float CrackSpeed = 2600.0f;
	constexpr float PulseSpeed = 2300.0f;
	constexpr float RevealSeconds = 2.62f;
	constexpr float ShardLifeSeconds = 0.95f;
	constexpr float FeatherWidth = 90.0f;
	constexpr float FieldStep = 4.0f; // Lattice spacing for smooth noise fields.
	constexpr Vec2 Focus{ Renderer::FocusX, Renderer::FocusY };

	// The highlights accelerate towards the shatter, like a building charge.
	constexpr float PulseTimes[] = { 0.98f, 1.20f, 1.38f, 1.52f, 1.63f, 1.72f };

	// Palette: storm steel for the sky, glacial cyan for the seams, molten amber behind.
	constexpr Color Ice{ 0.42f, 0.74f, 1.0f };
	constexpr Color IceWhite{ 0.80f, 0.93f, 1.0f };
	constexpr Color Gold{ 1.0f, 0.78f, 0.46f };
	constexpr Color Ember{ 1.0f, 0.50f, 0.16f };
	constexpr Color ImpactWhite{ 1.0f, 0.92f, 0.80f };

	struct Stop
	{
		float At;
		Color Value;
	};

	constexpr Stop SkyStops[] = {
		{ 0.00f, { 3, 6, 12 } },
		{ 0.18f, { 11, 19, 34 } },
		{ 0.38f, { 28, 46, 73 } },
		{ 0.60f, { 63, 93, 127 } },
		{ 0.80f, { 122, 153, 183 } },
		{ 1.00f, { 200, 217, 235 } }
	};

	constexpr Stop FireStops[] = {
		{ 0.00f, { 4, 1, 1 } },
		{ 0.22f, { 40, 9, 4 } },
		{ 0.42f, { 122, 37, 9 } },
		{ 0.60f, { 208, 94, 26 } },
		{ 0.76f, { 250, 160, 70 } },
		{ 0.89f, { 255, 216, 150 } },
		{ 1.00f, { 255, 248, 232 } }
	};

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

	float Lattice(int x, int y, unsigned int seed)
	{
		return Noise(static_cast<unsigned int>(x) * 73856093u ^ static_cast<unsigned int>(y) * 19349663u ^ seed * 83492791u);
	}

	float ValueNoise(float x, float y, unsigned int seed)
	{
		const float fx = std::floor(x), fy = std::floor(y);
		const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
		const float tx = x - fx, ty = y - fy;
		const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty);
		const float a = Lattice(ix, iy, seed), b = Lattice(ix + 1, iy, seed);
		const float c = Lattice(ix, iy + 1, seed), d = Lattice(ix + 1, iy + 1, seed);
		const float top = a + (b - a) * sx;
		return top + ((c + (d - c) * sx) - top) * sy;
	}

	float Fbm(float x, float y, unsigned int seed, int octaves)
	{
		float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
		for (int octave = 0; octave < octaves; ++octave)
		{
			sum += ValueNoise(x, y, seed + octave * 101u) * amplitude;
			total += amplitude;
			x = x * 2.03f + 17.1f;
			y = y * 2.03f + 9.7f;
			amplitude *= 0.5f;
		}
		return sum / total;
	}

	float ValueNoise3(float x, float y, float z, unsigned int seed)
	{
		const float fx = std::floor(x), fy = std::floor(y), fz = std::floor(z);
		const int ix = static_cast<int>(fx), iy = static_cast<int>(fy), iz = static_cast<int>(fz);
		const float tx = x - fx, ty = y - fy, tz = z - fz;
		const float sx = tx * tx * (3.0f - 2.0f * tx), sy = ty * ty * (3.0f - 2.0f * ty), sz = tz * tz * (3.0f - 2.0f * tz);
		auto corner = [&](int dx, int dy, int dz)
		{
			return Lattice(ix + dx, iy + dy, seed ^ static_cast<unsigned int>(iz + dz) * 2654435761u);
		};
		auto plane = [&](int dz)
		{
			const float a = corner(0, 0, dz), b = corner(1, 0, dz), c = corner(0, 1, dz), d = corner(1, 1, dz);
			const float top = a + (b - a) * sx;
			return top + ((c + (d - c) * sx) - top) * sy;
		};
		const float near = plane(0);
		return near + (plane(1) - near) * sz;
	}

	// Sharp creases (1 - |2n - 1|) read as fractured crystal rather than cloud.
	float RidgedFbm3(float x, float y, float z, unsigned int seed, int octaves)
	{
		float sum = 0.0f, amplitude = 0.5f, total = 0.0f;
		for (int octave = 0; octave < octaves; ++octave)
		{
			const float ridge = 1.0f - std::abs(2.0f * ValueNoise3(x, y, z, seed + octave * 131u) - 1.0f);
			sum += ridge * ridge * amplitude;
			total += amplitude;
			x = x * 2.1f + 3.7f;
			y = y * 2.1f + 1.3f;
			z = z * 2.1f + 5.1f;
			amplitude *= 0.55f;
		}
		return sum / total;
	}

	// Smooth fields are evaluated on a coarse logical lattice and sampled bilinearly;
	// identical to the eye, and a fraction of the cost at high DPI.
	class CoarseField
	{
	public:
		template<typename Function, typename ForRows>
		CoarseField(Function&& function, ForRows&& forRows, float step = FieldStep)
			: Step(step)
		{
			Columns = static_cast<int>(Renderer::Width / Step) + 2;
			Rows = static_cast<int>(Renderer::Height / Step) + 2;
			Values.resize(static_cast<size_t>(Columns) * Rows);
			forRows(Rows, [&](int firstRow, int endRow)
				{
					for (int y = firstRow; y < endRow; ++y)
					{
						for (int x = 0; x < Columns; ++x)
						{
							Values[static_cast<size_t>(y) * Columns + x] = function(x * Step, y * Step);
						}
					}
				});
		}

		float At(float u, float v) const
		{
			const float fx = std::clamp(u / Step, 0.0f, Columns - 1.001f);
			const float fy = std::clamp(v / Step, 0.0f, Rows - 1.001f);
			const int x = static_cast<int>(fx), y = static_cast<int>(fy);
			const float tx = fx - x, ty = fy - y;
			const float* row = Values.data() + static_cast<size_t>(y) * Columns + x;
			const float top = row[0] + (row[1] - row[0]) * tx;
			const float bottom = row[Columns] + (row[Columns + 1] - row[Columns]) * tx;
			return top + (bottom - top) * ty;
		}

	private:
		float Step = FieldStep;
		int Columns = 0;
		int Rows = 0;
		std::vector<float> Values;
	};

	template<size_t Count>
	Color Ramp(const Stop (&stops)[Count], float t)
	{
		t = Saturate(t);
		for (size_t index = 1; index < Count; ++index)
		{
			if (t <= stops[index].At)
			{
				const Stop& a = stops[index - 1];
				const Stop& b = stops[index];
				const float k = (t - a.At) / (b.At - a.At);
				return { a.Value.R + (b.Value.R - a.Value.R) * k, a.Value.G + (b.Value.G - a.Value.G) * k,
					a.Value.B + (b.Value.B - a.Value.B) * k };
			}
		}
		return stops[Count - 1].Value;
	}

	Color operator*(Color color, float k)
	{
		return { color.R * k, color.G * k, color.B * k };
	}

	Color Mix(Color a, Color b, float k)
	{
		return { a.R + (b.R - a.R) * k, a.G + (b.G - a.G) * k, a.B + (b.B - a.B) * k };
	}

	Vec2 Lerp(Vec2 a, Vec2 b, float k)
	{
		return { a.X + (b.X - a.X) * k, a.Y + (b.Y - a.Y) * k };
	}

	float Length(Vec2 v)
	{
		return std::sqrt(v.X * v.X + v.Y * v.Y);
	}

	float DistanceToFocus(Vec2 p)
	{
		return Length({ p.X - Focus.X, p.Y - Focus.Y });
	}

	inline uint32_t Pack(float r, float g, float b, float a)
	{
		auto byte = [](float value)
		{
			return static_cast<uint32_t>(value <= 0.0f ? 0.0f : (value >= 255.0f ? 255.0f : value + 0.5f));
		};
		return byte(b) | byte(g) << 8 | byte(r) << 16 | byte(a) << 24;
	}

	// The shattering zone is an inverted dome around the focus: wide across the top,
	// tapering beneath it, like a sky tearing open from above.
	float ZoneDistance(Vec2 p)
	{
		constexpr float centerY = 110.0f;
		const float taper = SmoothStep((p.Y - centerY) / 250.0f);
		const float radiusX = 440.0f * (1.0f - 0.74f * taper);
		const float dx = (p.X - Focus.X) / radiusX;
		const float dy = (p.Y - centerY) / 235.0f;
		return std::sqrt(dx * dx + dy * dy);
	}

	// Calls plot(x, y, distance) for pixels within reach of a segment in pixel space;
	// callers turn the distance into one or more coverages. Work is proportional
	// to length x width rather than to the bounding box.
	template<typename Plot>
	void RasterizeSegment(float ax, float ay, float bx, float by, float halfWidth, int width, int height, Plot&& plot)
	{
		const float dx = bx - ax, dy = by - ay;
		const float lengthSquared = dx * dx + dy * dy;
		const float inverseLength = lengthSquared > 0.0f ? 1.0f / lengthSquared : 0.0f;
		const float reach = halfWidth + 1.0f;
		const float limit = (halfWidth + 0.5f) * (halfWidth + 0.5f);
		auto visit = [&](int x, int y)
		{
			const float px = x + 0.5f, py = y + 0.5f;
			const float t = Saturate(((px - ax) * dx + (py - ay) * dy) * inverseLength);
			const float ex = ax + dx * t - px, ey = ay + dy * t - py;
			const float squared = ex * ex + ey * ey;
			if (squared < limit)
			{
				plot(x, y, std::sqrt(squared));
			}
		};
		if (std::abs(dx) >= std::abs(dy))
		{
			const int x0 = std::max(0, static_cast<int>(std::floor(std::min(ax, bx) - reach)));
			const int x1 = std::min(width - 1, static_cast<int>(std::ceil(std::max(ax, bx) + reach)));
			const float slope = dx != 0.0f ? dy / dx : 0.0f;
			const float spread = reach * std::sqrt(1.0f + slope * slope) + 1.0f;
			for (int x = x0; x <= x1; ++x)
			{
				const float t = std::clamp(dx != 0.0f ? (x + 0.5f - ax) / dx : 0.0f, 0.0f, 1.0f);
				const float cy = ay + dy * t;
				const int y0 = std::max(0, static_cast<int>(std::floor(cy - spread)));
				const int y1 = std::min(height - 1, static_cast<int>(std::ceil(cy + spread)));
				for (int y = y0; y <= y1; ++y)
				{
					visit(x, y);
				}
			}
		}
		else
		{
			const int y0 = std::max(0, static_cast<int>(std::floor(std::min(ay, by) - reach)));
			const int y1 = std::min(height - 1, static_cast<int>(std::ceil(std::max(ay, by) + reach)));
			const float slope = dy != 0.0f ? dx / dy : 0.0f;
			const float spread = reach * std::sqrt(1.0f + slope * slope) + 1.0f;
			for (int y = y0; y <= y1; ++y)
			{
				const float t = std::clamp(dy != 0.0f ? (y + 0.5f - ay) / dy : 0.0f, 0.0f, 1.0f);
				const float cx = ax + dx * t;
				const int x0 = std::max(0, static_cast<int>(std::floor(cx - spread)));
				const int x1 = std::min(width - 1, static_cast<int>(std::ceil(cx + spread)));
				for (int x = x0; x <= x1; ++x)
				{
					visit(x, y);
				}
			}
		}
	}

	// Additive light on a premultiplied pixel; alpha grows so light can spill
	// past the feathered edge onto the desktop.
	inline void AddLight(uint32_t& pixel, int r, int g, int b)
	{
		r += static_cast<int>(pixel >> 16 & 255);
		g += static_cast<int>(pixel >> 8 & 255);
		b += static_cast<int>(pixel & 255);
		r = r > 255 ? 255 : r;
		g = g > 255 ? 255 : g;
		b = b > 255 ? 255 : b;
		int a = static_cast<int>(pixel >> 24);
		a = a > r ? a : r;
		a = a > g ? a : g;
		a = a > b ? a : b;
		pixel = static_cast<uint32_t>(b) | static_cast<uint32_t>(g) << 8 | static_cast<uint32_t>(r) << 16 | static_cast<uint32_t>(a) << 24;
	}

	// Fills a polygon by pixel centres; shared edges never gap or overlap.
	template<typename Plot>
	void FillPolygon(const std::vector<Vec2>& polygon, int width, int height, Plot&& plot)
	{
		float top = 1.0e9f, bottom = -1.0e9f;
		for (const Vec2& p : polygon)
		{
			top = std::min(top, p.Y);
			bottom = std::max(bottom, p.Y);
		}
		const int y0 = std::max(0, static_cast<int>(std::ceil(top - 0.5f)));
		const int y1 = std::min(height - 1, static_cast<int>(std::ceil(bottom - 0.5f)) - 1);
		std::vector<float> crossings;
		for (int y = y0; y <= y1; ++y)
		{
			const float center = y + 0.5f;
			crossings.clear();
			for (size_t index = 0; index < polygon.size(); ++index)
			{
				const Vec2 a = polygon[index], b = polygon[(index + 1) % polygon.size()];
				if ((a.Y <= center && center < b.Y) || (b.Y <= center && center < a.Y))
				{
					crossings.push_back(a.X + (center - a.Y) * (b.X - a.X) / (b.Y - a.Y));
				}
			}
			std::sort(crossings.begin(), crossings.end());
			for (size_t index = 0; index + 1 < crossings.size(); index += 2)
			{
				const int x0 = std::max(0, static_cast<int>(std::ceil(crossings[index] - 0.5f)));
				const int x1 = std::min(width, static_cast<int>(std::ceil(crossings[index + 1] - 0.5f)));
				for (int x = x0; x < x1; ++x)
				{
					plot(x, y);
				}
			}
		}
	}

	// Separable running-sum box blur on interleaved RGB floats.
	void BoxBlur(std::vector<float>& image, std::vector<float>& scratch, int width, int height, int radius)
	{
		if (radius <= 0 || width <= 0 || height <= 0)
		{
			return;
		}
		scratch.resize(image.size());
		float* data = image.data();
		float* temp = scratch.data();
		const float norm = 1.0f / (2 * radius + 1);
		for (int y = 0; y < height; ++y)
		{
			const float* row = data + static_cast<size_t>(y) * width * 3;
			float* out = temp + static_cast<size_t>(y) * width * 3;
			for (int channel = 0; channel < 3; ++channel)
			{
				float sum = 0.0f;
				for (int x = -radius; x <= radius; ++x)
				{
					sum += row[std::clamp(x, 0, width - 1) * 3 + channel];
				}
				for (int x = 0; x < width; ++x)
				{
					out[x * 3 + channel] = sum * norm;
					sum += row[std::min(x + radius + 1, width - 1) * 3 + channel] - row[std::max(x - radius, 0) * 3 + channel];
				}
			}
		}
		// Vertical pass walks whole rows, keeping one running sum per column value.
		const size_t stride = static_cast<size_t>(width) * 3;
		std::vector<float> sums(stride, 0.0f);
		for (int y = -radius; y <= radius; ++y)
		{
			const float* row = temp + std::clamp(y, 0, height - 1) * stride;
			for (size_t index = 0; index < stride; ++index)
			{
				sums[index] += row[index];
			}
		}
		for (int y = 0; y < height; ++y)
		{
			float* out = data + y * stride;
			const float* entering = temp + std::min(y + radius + 1, height - 1) * stride;
			const float* leaving = temp + std::max(y - radius, 0) * stride;
			for (size_t index = 0; index < stride; ++index)
			{
				out[index] = sums[index] * norm;
				sums[index] += entering[index] - leaving[index];
			}
		}
	}

	// Soft shoulder: identity below the knee, then rolls off towards 255, so intense
	// light turns white-hot instead of clipping flat.
	constexpr float ToneKnee = 170.0f;
	constexpr float ToneRoom = 255.0f - ToneKnee;

	float ToneMap(float value)
	{
		const float over = std::max(0.0f, value - ToneKnee);
		return std::min(value, ToneKnee) + ToneRoom * over / (over + ToneRoom);
	}

	// Lights, exposes, feathers and tone-maps one row: surface * exposure * feather
	// + bloom + flash * feather. Alpha covers the feathered surface and any light
	// spilling past it, keeping the result validly premultiplied.
	void CompositeRow(uint32_t* out, const float* top, const float* bottom, float blend, const float* featherColumns,
		float rowFeather, float exposure, Color flash, const uint8_t* toneTable, int width)
	{
#if defined(NYX_RIFT_SSE2)
		(void)toneTable;
		// One pixel per vector: lanes are B, G, R, A, exactly the pixel's byte order.
		const __m128i zero = _mm_setzero_si128();
		const __m128 knee = _mm_set1_ps(ToneKnee), room = _mm_set1_ps(ToneRoom);
		const __m128 flashColor = _mm_set_ps(0.0f, flash.R, flash.G, flash.B);
		const __m128 colorMask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
		const __m128 alphaMask = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
		const __m128 weight = _mm_set1_ps(blend);
		for (int x = 0; x < width; ++x)
		{
			const float feather = featherColumns[x] * rowFeather;
			const __m128i packed = _mm_cvtsi32_si128(static_cast<int>(out[x]));
			const __m128 surface = _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(packed, zero), zero));
			const __m128 a = _mm_loadu_ps(top + x * 4), b = _mm_loadu_ps(bottom + x * 4);
			const __m128 light = _mm_add_ps(a, _mm_mul_ps(_mm_sub_ps(b, a), weight));
			const __m128 lit = _mm_add_ps(light, _mm_mul_ps(flashColor, _mm_set1_ps(feather)));
			const __m128 color = _mm_add_ps(_mm_mul_ps(surface, _mm_set1_ps(feather * exposure)), lit);
			// Soft shoulder: values above the knee approach knee + room but never reach it.
			const __m128 over = _mm_max_ps(_mm_sub_ps(color, knee), _mm_setzero_ps());
			const __m128 shoulder = _mm_div_ps(_mm_mul_ps(room, over), _mm_add_ps(over, room));
			const __m128 toned = _mm_and_ps(_mm_add_ps(_mm_min_ps(color, knee), shoulder), colorMask);
			__m128 peak = _mm_max_ps(toned, _mm_shuffle_ps(toned, toned, _MM_SHUFFLE(2, 1, 0, 3)));
			peak = _mm_max_ps(peak, _mm_shuffle_ps(peak, peak, _MM_SHUFFLE(1, 0, 3, 2)));
			peak = _mm_max_ps(peak, _mm_set1_ps(std::min(255.0f, feather * 255.0f)));
			const __m128i ints = _mm_cvtps_epi32(_mm_or_ps(toned, _mm_and_ps(peak, alphaMask)));
			out[x] = static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_packus_epi16(_mm_packs_epi32(ints, ints), zero)));
		}
#else
		const int toneLimit = 2047;
		for (int x = 0; x < width; ++x)
		{
			const float feather = featherColumns[x] * rowFeather;
			const float lit = feather * exposure;
			const uint32_t pixel = out[x];
			const float* a = top + x * 4;
			const float* b = bottom + x * 4;
			const int blue = static_cast<int>((pixel & 255) * lit + a[0] + (b[0] - a[0]) * blend + flash.B * feather);
			const int green = static_cast<int>((pixel >> 8 & 255) * lit + a[1] + (b[1] - a[1]) * blend + flash.G * feather);
			const int red = static_cast<int>((pixel >> 16 & 255) * lit + a[2] + (b[2] - a[2]) * blend + flash.R * feather);
			const uint32_t tb = toneTable[std::min(blue, toneLimit)];
			const uint32_t tg = toneTable[std::min(green, toneLimit)];
			const uint32_t tr = toneTable[std::min(red, toneLimit)];
			const uint32_t ta = std::max({ static_cast<uint32_t>(std::min(255.0f, feather * 255.0f + 0.5f)), tr, tg, tb });
			out[x] = tb | tg << 8 | tr << 16 | ta << 24;
		}
#endif
	}

	void PolygonCentroid(const std::vector<Vec2>& polygon, Vec2& centroid)
	{
		float area = 0.0f, cx = 0.0f, cy = 0.0f;
		for (size_t index = 0; index < polygon.size(); ++index)
		{
			const Vec2 a = polygon[index], b = polygon[(index + 1) % polygon.size()];
			const float cross = a.X * b.Y - b.X * a.Y;
			area += cross;
			cx += (a.X + b.X) * cross;
			cy += (a.Y + b.Y) * cross;
		}
		area *= 0.5f;
		if (std::abs(area) > 1.0e-3f)
		{
			centroid = { cx / (6.0f * area), cy / (6.0f * area) };
		}
	}

	float AngleBetween(float from, float to)
	{
		float difference = std::fmod(to - from, 2.0f * Pi);
		return difference < 0.0f ? difference + 2.0f * Pi : difference;
	}
}

namespace Nyx
{
	// A small persistent pool for the full-frame passes. Capped well below the core
	// count: the editor is initializing Vulkan on other threads at the same time.
	class StartupBannerRiftRenderer::Workers
	{
	public:
		explicit Workers(int helpers)
		{
			for (int index = 0; index < helpers; ++index)
			{
				// Fewer helpers is fine; a failed thread must never take the editor down.
				try
				{
					Threads.emplace_back([this, index]
						{
							Loop(index + 1);
						});
				}
				catch (...)
				{
					break;
				}
			}
		}

		~Workers()
		{
			{
				std::lock_guard lock(Mutex);
				bStop = true;
			}
			Wake.notify_all();
			for (std::thread& thread : Threads)
			{
				thread.join();
			}
		}

		void Run(int rows, const std::function<void(int, int)>& job)
		{
			if (Threads.empty() || rows < 64)
			{
				job(0, rows);
				return;
			}
			{
				std::lock_guard lock(Mutex);
				Job = &job;
				Rows = rows;
				Remaining = static_cast<int>(Threads.size());
				++Generation;
			}
			Wake.notify_all();
			Slice(0, rows, job);
			std::unique_lock lock(Mutex);
			Done.wait(lock, [this]
				{
					return Remaining == 0;
				});
			Job = nullptr;
		}

	private:
		void Slice(int index, int rows, const std::function<void(int, int)>& job) const
		{
			const int count = static_cast<int>(Threads.size()) + 1;
			const int begin = rows * index / count, end = rows * (index + 1) / count;
			if (begin < end)
			{
				job(begin, end);
			}
		}

		void Loop(int index)
		{
			unsigned long long seen = 0;
			while (true)
			{
				const std::function<void(int, int)>* job = nullptr;
				int rows = 0;
				{
					std::unique_lock lock(Mutex);
					Wake.wait(lock, [&]
						{
							return bStop || Generation != seen;
						});
					if (bStop)
					{
						return;
					}
					seen = Generation;
					job = Job;
					rows = Rows;
				}
				Slice(index, rows, *job);
				{
					std::lock_guard lock(Mutex);
					if (--Remaining == 0)
					{
						Done.notify_one();
					}
				}
			}
		}

		std::vector<std::thread> Threads;
		std::mutex Mutex;
		std::condition_variable Wake;
		std::condition_variable Done;
		const std::function<void(int, int)>* Job = nullptr;
		unsigned long long Generation = 0;
		int Rows = 0;
		int Remaining = 0;
		bool bStop = false;
	};

	StartupBannerRiftRenderer::StartupBannerRiftRenderer() = default;
	StartupBannerRiftRenderer::~StartupBannerRiftRenderer() = default;

	void StartupBannerRiftRenderer::ForRows(int rows, const std::function<void(int, int)>& job)
	{
		if (!Pool)
		{
#ifdef NYX_RIFT_THREADS
			const int threads = NYX_RIFT_THREADS;
#else
			const int cores = static_cast<int>(std::thread::hardware_concurrency());
			const int threads = cores >= 4 ? std::min(4, cores / 2) : 1;
#endif
			Pool = std::make_unique<Workers>(threads - 1);
		}
		Pool->Run(rows, job);
	}

	void StartupBannerRiftRenderer::Prepare(const SoftwareIntroInputs& inputs)
	{
		PixelWidth = std::max(1, inputs.Width);
		PixelHeight = std::max(1, inputs.Height);
		Scale = PixelWidth / Width;
		const size_t pixelCount = static_cast<size_t>(PixelWidth) * PixelHeight;
		Frame.assign(pixelCount, 0u);

		// Feather only the outer boundary of the canvas so it melts into the desktop.
		FeatherColumns.resize(PixelWidth);
		FeatherRows.resize(PixelHeight);
		for (int x = 0; x < PixelWidth; ++x)
		{
			const float logical = (x + 0.5f) / Scale;
			FeatherColumns[x] = SmoothStep(std::min(logical, Width - logical) / FeatherWidth);
		}
		for (int y = 0; y < PixelHeight; ++y)
		{
			const float logical = (y + 0.5f) / Scale;
			FeatherRows[y] = SmoothStep(std::min(logical, Height - logical) / FeatherWidth);
		}

		// Soft shoulder: intense light rolls off towards white instead of clipping flat.
		ToneCurve.resize(2048);
		for (size_t value = 0; value < ToneCurve.size(); ++value)
		{
			ToneCurve[value] = static_cast<uint8_t>(ToneMap(static_cast<float>(value)) + 0.5f);
		}

		PrepareSky(inputs.Desktop, inputs.DesktopStride);
		PrepareWeb();
		PrepareCellMap();
		PrepareCracks();
		PrepareRift();

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

		EmissionWidth = std::max(2, (PixelWidth + 3) / 4);
		EmissionHeight = std::max(2, (PixelHeight + 3) / 4);
		HaloWidth = (EmissionWidth + 1) / 2;
		HaloHeight = (EmissionHeight + 1) / 2;
		Emission.assign(static_cast<size_t>(EmissionWidth) * EmissionHeight * 3, 0.0f);
		Halo.assign(static_cast<size_t>(HaloWidth) * HaloHeight * 3, 0.0f);
		UpsampledRows.assign(static_cast<size_t>(PixelWidth) * 32, 0.0f);
		// Column taps for the bloom upsample: offset of the left emission texel (in
		// values) and its 8-bit blend weight. The right texel is clamped by padding.
		UpsampleColumns.resize(PixelWidth);
		UpsampleWeights.resize(PixelWidth);
		for (int x = 0; x < PixelWidth; ++x)
		{
			const float ex = std::clamp((x + 0.5f) * 0.25f - 0.5f, 0.0f, EmissionWidth - 1.0f);
			const int left = std::min(static_cast<int>(ex), std::max(0, EmissionWidth - 2));
			UpsampleColumns[x] = left * 3;
			UpsampleWeights[x] = std::clamp(ex - left, 0.0f, 1.0f);
		}
	}

	void StartupBannerRiftRenderer::Release()
	{
		Desktop = {};
		Sky = {};
		Rift = {};
		CellMap = {};
		Cells = {};
		Edges = {};
		Cracks = {};
		Tendrils = {};
	}

	bool StartupBannerRiftRenderer::IsOpen(int cell, float time) const
	{
		return cell >= 0 && cell < static_cast<int>(Cells.size()) && time >= Cells[cell].BreakTime;
	}

	void StartupBannerRiftRenderer::PrepareSky(const uint32_t* desktop, int desktopStride)
	{
		const size_t pixelCount = static_cast<size_t>(PixelWidth) * PixelHeight;
		bHasDesktop = desktop != nullptr;
		Desktop.clear();
		if (bHasDesktop)
		{
			Desktop.resize(pixelCount);
			for (int y = 0; y < PixelHeight; ++y)
			{
				std::copy_n(desktop + static_cast<size_t>(y) * desktopStride, PixelWidth,
					Desktop.data() + static_cast<size_t>(y) * PixelWidth);
			}
		}
		const auto runRows = [this](int rows, const std::function<void(int, int)>& job)
		{
			ForRows(rows, job);
		};
		// Broad clouds carry the lighting; the desktop is crushed into a dark, narrow
		// range beneath them so it reads as structure under a storm, not as a UI.
		const auto sampleCloudLight = [](float u, float v)
		{
			const float clouds = Fbm(u * 0.0042f, v * 0.0085f, 11u, 5);
			const float nx = (u - Focus.X) / 560.0f, ny = (v - 300.0f) / 470.0f;
			const float vignette = 1.0f - 0.55f * std::min(1.0f, nx * nx * 0.8f + ny * ny);
			return (0.50f + 0.95f * clouds) * vignette;
		};
		const CoarseField cloudLight(sampleCloudLight, runRows);
		const auto sampleCloudGlow = [](float u, float v)
		{
			const float clouds = Fbm(u * 0.0042f, v * 0.0085f, 11u, 5);
			const float wisps = Fbm(u * 0.012f + 3.1f, v * 0.021f, 23u, 4);
			const float skyLight = std::exp(-DistanceToFocus({ u, v }) / 360.0f);
			return (wisps - 0.5f) * 0.12f + skyLight * 0.26f * clouds;
		};
		const CoarseField cloudGlow(sampleCloudGlow, runRows);
		Sky.resize(pixelCount);
		uint32_t* sky = Sky.data();
		const uint32_t* source = bHasDesktop ? Desktop.data() : nullptr;
		ForRows(PixelHeight, [&](int firstRow, int endRow)
			{
				for (int y = firstRow; y < endRow; ++y)
				{
					const float v = (y + 0.5f) / Scale;
					for (int x = 0; x < PixelWidth; ++x)
					{
						const float u = (x + 0.5f) / Scale;
						const size_t index = static_cast<size_t>(y) * PixelWidth + x;
						float luminance = 0.30f;
						if (source)
						{
							const uint32_t pixel = source[index];
							luminance = (0.2126f * (pixel >> 16 & 255) + 0.7152f * (pixel >> 8 & 255) + 0.0722f * (pixel & 255)) / 255.0f;
						}
						const float level = (0.08f + luminance * 0.34f) * cloudLight.At(u, v) + cloudGlow.At(u, v);
						const Color color = Ramp(SkyStops, level);
						sky[index] = Pack(color.R, color.G, color.B, 255.0f);
					}
				}
			});
	}

	void StartupBannerRiftRenderer::PrepareWeb()
	{
		// Real glass breaks into radial cracks joined by concentric chords. Spokes
		// start at the focus and split as the sectors widen, so cells stay similar in
		// size; every cell is a quad (or a triangle at the centre) between two spokes.
		constexpr float rings[] = { 0, 12, 26, 43, 65, 92, 124, 162, 207, 259, 320, 392, 476, 575, 700, 850, 1020 };
		constexpr int ringCount = static_cast<int>(sizeof(rings) / sizeof(rings[0]));
		struct Spoke
		{
			float Angle = 0.0f;
			int StartRing = 0;
			Vec2 Points[ringCount];
		};
		std::vector<Spoke> spokes;
		constexpr int initialSpokes = 9;
		const float offset = Noise(4242u) * 2.0f * Pi;
		for (int index = 0; index < initialSpokes; ++index)
		{
			Spoke spoke;
			spoke.Angle = std::fmod(offset + (index + (Noise(index * 3u + 77u) - 0.5f) * 0.55f) * 2.0f * Pi / initialSpokes, 2.0f * Pi);
			spoke.Points[0] = Focus;
			spokes.push_back(spoke);
		}
		auto activeAt = [&](int band)
		{
			std::vector<int> active;
			for (int index = 0; index < static_cast<int>(spokes.size()); ++index)
			{
				if (spokes[index].StartRing <= band)
				{
					active.push_back(index);
				}
			}
			std::sort(active.begin(), active.end(), [&](int a, int b)
				{
					return spokes[a].Angle < spokes[b].Angle;
				});
			return active;
		};
		for (int ring = 1; ring < ringCount; ++ring)
		{
			std::vector<int> active = activeAt(ring - 1);
			for (const int index : active)
			{
				Spoke& spoke = spokes[index];
				const unsigned int key = static_cast<unsigned int>(index) * 131u + ring * 7u;
				const float angle = spoke.Angle + (Noise(key) - 0.5f) * 0.12f;
				const float radius = rings[ring] * (1.0f + (Noise(key + 1u) - 0.5f) * 0.26f);
				spoke.Points[ring] = { Focus.X + std::cos(angle) * radius, Focus.Y + std::sin(angle) * radius * 0.9f };
			}
			if (ring == ringCount - 1)
			{
				break;
			}
			for (size_t index = 0; index < active.size(); ++index)
			{
				const Spoke& a = spokes[active[index]];
				const Spoke& b = spokes[active[(index + 1) % active.size()]];
				const float span = AngleBetween(a.Angle, b.Angle);
				if (span * rings[ring] > 56.0f)
				{
					const unsigned int key = static_cast<unsigned int>(spokes.size()) * 37u + ring;
					const float split = 0.36f + Noise(key) * 0.28f;
					Spoke added;
					added.Angle = std::fmod(a.Angle + span * split, 2.0f * Pi);
					added.StartRing = ring;
					added.Points[ring] = Lerp(a.Points[ring], b.Points[ring], split);
					spokes.push_back(added);
				}
			}
		}

		Cells.clear();
		Edges.clear();
		// cellOf[band][spoke]: the cell whose left (lower-angle) boundary is that spoke;
		// for a spoke born on the next ring it names the cell it splits from above.
		std::vector<std::vector<int>> cellOf(ringCount, std::vector<int>(spokes.size(), -1));
		std::vector<std::vector<int>> ownerBelow(ringCount, std::vector<int>(spokes.size(), -1));
		unsigned int serial = 0;
		for (int band = 0; band + 1 < ringCount; ++band)
		{
			const std::vector<int> active = activeAt(band);
			for (size_t index = 0; index < active.size(); ++index)
			{
				const int a = active[index];
				const int b = active[(index + 1) % active.size()];
				Cell cell;
				cell.Outline.push_back(spokes[a].Points[band]);
				if (band > 0)
				{
					cell.Outline.push_back(spokes[b].Points[band]);
				}
				cell.Outline.push_back(spokes[b].Points[band + 1]);
				// Spokes born on the outer ring between a and b, in descending angle.
				std::vector<int> born;
				const float span = AngleBetween(spokes[a].Angle, spokes[b].Angle);
				for (int other = 0; other < static_cast<int>(spokes.size()); ++other)
				{
					if (spokes[other].StartRing == band + 1 && AngleBetween(spokes[a].Angle, spokes[other].Angle) < span)
					{
						born.push_back(other);
					}
				}
				std::sort(born.begin(), born.end(), [&](int p, int q)
					{
						return AngleBetween(spokes[a].Angle, spokes[p].Angle) > AngleBetween(spokes[a].Angle, spokes[q].Angle);
					});
				for (const int spoke : born)
				{
					cell.Outline.push_back(spokes[spoke].Points[band + 1]);
				}
				cell.Outline.push_back(spokes[a].Points[band + 1]);
				PolygonCentroid(cell.Outline, cell.Centroid);
				const float zone = ZoneDistance(cell.Centroid);
				const int id = static_cast<int>(Cells.size());
				cellOf[band][a] = id;
				ownerBelow[band][a] = id;
				for (const int spoke : born)
				{
					ownerBelow[band][spoke] = id;
				}
				const unsigned int key = ++serial * 13u + 1000u;
				cell.bBreakable = zone <= 1.0f;
				if (cell.bBreakable)
				{
					// The breach cascades outward from the impact in a fraction of a second.
					cell.BreakTime = ImpactSeconds + 0.012f + DistanceToFocus(cell.Centroid) / 600.0f * 0.24f + Noise(key) * 0.03f;
					cell.Spin = (Noise(key + 1u) - 0.5f) * 5.0f;
					cell.TiltAngle = Noise(key + 2u) * Pi;
					cell.TiltRate = 1.5f + Noise(key + 3u) * 4.5f;
					cell.Launch = 420.0f + Noise(key + 4u) * 620.0f;
					cell.Approach = 0.45f + Noise(key + 5u) * 1.35f;
				}
				// Cells just outside the breach still crack, but never open.
				if (zone > 1.3f)
				{
					cell.Outline.clear();
				}
				Cells.push_back(std::move(cell));
			}
		}

		auto addEdge = [&](Vec2 a, Vec2 b, int first, int second, bool bChord)
		{
			if (first < 0 || second < 0 || Length({ b.X - a.X, b.Y - a.Y }) < 1.0f)
			{
				return;
			}
			if (Cells[first].Outline.empty() && Cells[second].Outline.empty())
			{
				return;
			}
			Edge edge;
			const bool bAFirst = DistanceToFocus(a) <= DistanceToFocus(b);
			edge.Near = bAFirst ? a : b;
			edge.Far = bAFirst ? b : a;
			edge.First = first;
			edge.Second = second;
			edge.Distance = DistanceToFocus(edge.Near);
			edge.BirthTime = WebStartSeconds + edge.Distance / WebSpeed;
			edge.bHidden = bChord && edge.Distance > 40.0f && Noise(static_cast<unsigned int>(Edges.size()) * 29u + 71u) < 0.17f;
			Edges.push_back(edge);
		};
		for (int band = 0; band + 1 < ringCount; ++band)
		{
			const std::vector<int> active = activeAt(band);
			for (size_t index = 0; index < active.size(); ++index)
			{
				const int spoke = active[index];
				const int previous = active[(index + active.size() - 1) % active.size()];
				// Radial crack between the cells on either side of this spoke.
				addEdge(spokes[spoke].Points[band], spokes[spoke].Points[band + 1], cellOf[band][previous], cellOf[band][spoke], false);
				// Concentric chord between this band's cell and the one inside it.
				if (band > 0)
				{
					const int next = active[(index + 1) % active.size()];
					addEdge(spokes[spoke].Points[band], spokes[next].Points[band], cellOf[band][spoke], ownerBelow[band - 1][spoke], true);
				}
			}
		}
		for (Cell& cell : Cells)
		{
			if (cell.Outline.empty())
			{
				cell.bBreakable = false;
			}
		}
	}

	void StartupBannerRiftRenderer::PrepareCellMap()
	{
		CellMap.assign(static_cast<size_t>(PixelWidth) * PixelHeight, NoCell);
		RowSpans.assign(static_cast<size_t>(PixelHeight) * 2, 0);
		for (int y = 0; y < PixelHeight; ++y)
		{
			RowSpans[y * 2] = PixelWidth;
			RowSpans[y * 2 + 1] = -1;
		}
		uint16_t* map = CellMap.data();
		std::vector<Vec2> scaled;
		for (size_t index = 0; index < Cells.size(); ++index)
		{
			if (!Cells[index].bBreakable)
			{
				continue;
			}
			scaled.clear();
			for (const Vec2& point : Cells[index].Outline)
			{
				scaled.push_back({ point.X * Scale, point.Y * Scale });
			}
			FillPolygon(scaled, PixelWidth, PixelHeight, [&](int x, int y)
				{
					map[static_cast<size_t>(y) * PixelWidth + x] = static_cast<uint16_t>(index);
					RowSpans[y * 2] = std::min(RowSpans[y * 2], x);
					RowSpans[y * 2 + 1] = std::max(RowSpans[y * 2 + 1], x);
				});
		}
	}

	void StartupBannerRiftRenderer::PrepareCracks()
	{
		Cracks.clear();
		unsigned int serial = 500;
		// Long fractures shoot out from the web across the rest of the sky.
		auto grow = [&](auto&& self, Vec2 start, float heading, float startTime, float maxLength,
						float width, float intensity, int depth) -> void
		{
			Crack crack;
			crack.StartTime = startTime;
			crack.Width = width;
			crack.Intensity = intensity;
			crack.Phase = Noise(++serial) * 0.06f;
			crack.Points.push_back(start);
			crack.Lengths.push_back(0.0f);
			float direction = heading, length = 0.0f;
			Vec2 p = start;
			struct Pending
			{
				Vec2 At;
				float Heading;
				float Time;
			};
			std::vector<Pending> branches;
			while (length < maxLength)
			{
				const float step = 9.0f + Noise(++serial) * 7.0f;
				// Mostly straight with sudden kinks, like stressed glass rather than lightning.
				const float kink = Noise(++serial) < 0.12f ? (Noise(++serial) - 0.5f) * 0.9f : (Noise(++serial) - 0.5f) * 0.16f;
				direction += kink + (heading - direction) * 0.10f;
				p = { p.X + std::cos(direction) * step, p.Y + std::sin(direction) * step };
				length += step;
				crack.Points.push_back(p);
				crack.Lengths.push_back(length);
				if (p.X < -70.0f || p.X > Width + 70.0f || p.Y < -70.0f || p.Y > Height + 70.0f)
				{
					break;
				}
				if (depth < 2 && length > 50.0f && Noise(++serial) < 0.035f)
				{
					const float side = Noise(++serial) < 0.5f ? -1.0f : 1.0f;
					branches.push_back({ p, direction + side * (0.30f + Noise(++serial) * 0.45f), startTime + length / CrackSpeed });
				}
			}
			Cracks.push_back(std::move(crack));
			for (const Pending& branch : branches)
			{
				self(self, branch.At, branch.Heading, branch.Time, 80.0f + Noise(++serial) * 260.0f,
					width * 0.62f, intensity * 0.75f, depth + 1);
			}
		};
		constexpr float headings[] = { -14, 4, 17, 31, 47, 66, 90, 112, 131, 148, 163, 177, 194, -38, -142 };
		unsigned int index = 0;
		for (const float degrees : headings)
		{
			const float heading = (degrees + (Noise(index * 7u + 3u) - 0.5f) * 8.0f) * Pi / 180.0f;
			// Begin where the web thins out, so the long cracks read as its continuation.
			float radius = 20.0f;
			while (radius < 420.0f && ZoneDistance({ Focus.X + std::cos(heading) * radius, Focus.Y + std::sin(heading) * radius }) < 0.78f)
			{
				radius += 6.0f;
			}
			const Vec2 start{ Focus.X + std::cos(heading) * radius, Focus.Y + std::sin(heading) * radius };
			const float startTime = WebStartSeconds + radius / WebSpeed + Noise(index * 7u + 5u) * 0.05f;
			grow(grow, start, heading, startTime, 1300.0f, 1.2f + Noise(index * 7u + 6u) * 0.6f, 1.0f, 0);
			++index;
		}

		Tendrils.clear();
		constexpr float sources[] = { 200, 224, 252, 288, 316, 340, 22, 46, 72, 108, 134, 158 };
		index = 0;
		for (const float degrees : sources)
		{
			const float angle = degrees * Pi / 180.0f;
			const float radius = 180.0f + Noise(index * 11u + 40u) * 170.0f;
			Tendril tendril;
			tendril.Start = { Focus.X + std::cos(angle) * radius, Focus.Y + std::sin(angle) * radius * 0.8f };
			const Vec2 middle{ (tendril.Start.X + Focus.X) * 0.5f, (tendril.Start.Y + Focus.Y) * 0.5f };
			const float bend = (Noise(index * 11u + 41u) - 0.5f) * 190.0f;
			tendril.Control = { middle.X - std::sin(angle) * bend, middle.Y + std::cos(angle) * bend };
			tendril.Phase = Noise(index * 11u + 42u);
			tendril.Width = 0.8f + Noise(index * 11u + 43u) * 0.8f;
			Tendrils.push_back(tendril);
			++index;
		}
	}

	void StartupBannerRiftRenderer::PrepareRift()
	{
		const auto runRows = [this](int rows, const std::function<void(int, int)>& job)
		{
			ForRows(rows, job);
		};
		const auto sampleHeat = [](float u, float v)
		{
			const float dx = u - Focus.X, dy = v - Focus.Y;
			const float distance = std::sqrt(dx * dx + dy * dy) + 1.0e-3f;
			const float warpX = Fbm(u * 0.004f, v * 0.004f, 51u, 3);
			const float warpY = Fbm(u * 0.004f + 5.2f, v * 0.004f + 1.3f, 52u, 3);
			const float smoke = Fbm(u * 0.0065f + warpX * 1.8f, v * 0.0065f + warpY * 1.8f, 53u, 5);
			const float rays = Fbm(dx / distance * 4.0f + 7.0f, dy / distance * 4.0f + 7.0f + distance * 0.0025f, 54u, 3);
			const float core = std::exp(-distance / 150.0f);
			return 0.14f + core * 0.90f + (smoke - 0.45f) * 0.62f + (rays - 0.5f) * 0.42f * (1.0f - core);
		};
		const CoarseField heatField(sampleHeat, runRows);
		const auto sampleSoot = [](float u, float v)
		{
			const float smoke = Fbm(u * 0.0058f + 2.0f, v * 0.0058f - 4.0f, 56u, 4);
			return 0.40f + 0.60f * SmoothStep((smoke - 0.30f) / 0.40f);
		};
		const CoarseField sootField(sampleSoot, runRows);
		const auto sampleCrystal = [](float u, float v)
		{
			return SmoothStep((Fbm(u * 0.011f, v * 0.011f, 55u, 3) - 0.36f) / 0.22f);
		};
		const CoarseField crystalField(sampleCrystal, runRows);

		// Crystalline splinters catch the fire. Ridged noise is sampled with the
		// direction to the focus and the distance from it as separate axes, so the
		// creases stretch outward like shards bursting from the breach. Using the
		// direction vector (not an angle) keeps the field free of a wrap-around seam.
		const auto sampleSplinters = [](float u, float v)
		{
			const float dx = u - Focus.X, dy = v - Focus.Y;
			const float distance = std::sqrt(dx * dx + dy * dy) + 1.0e-3f;
			return RidgedFbm3(dx / distance * 9.0f, dy / distance * 9.0f, distance * 0.011f, 57u, 4);
		};
		const CoarseField splinters(sampleSplinters, runRows, 1.5f);
		const auto sampleFacets = [](float u, float v)
		{
			const float dx = u - Focus.X, dy = v - Focus.Y;
			const float distance = std::sqrt(dx * dx + dy * dy) + 1.0e-3f;
			return ValueNoise3(dx / distance * 5.0f + 2.0f, dy / distance * 5.0f, distance * 0.006f, 58u);
		};
		const CoarseField facets(sampleFacets, runRows, 3.0f);
		Rift.resize(static_cast<size_t>(PixelWidth) * PixelHeight);
		uint32_t* rift = Rift.data();
		ForRows(PixelHeight, [&](int firstRow, int endRow)
			{
				for (int y = firstRow; y < endRow; ++y)
				{
					const float v = (y + 0.5f) / Scale;
					const float canopy = 0.55f + 0.45f * SmoothStep((v - Focus.Y + 190.0f) / 210.0f);
					for (int x = 0; x < PixelWidth; ++x)
					{
						const float u = (x + 0.5f) / Scale;
						const float heat = heatField.At(u, v);
						Color color = Ramp(FireStops, heat);
						// Crystal is densest towards the torn border, as in a broken geode.
						const float border = SmoothStep((ZoneDistance({ u, v }) - 0.40f) / 0.45f);
						const float crystals = std::min(1.0f, crystalField.At(u, v) * (0.45f + 0.9f * border));
						const float ridge = splinters.At(u, v);
						// Broad facets: gold planes of varying brightness.
						const float shade = 0.25f + 0.75f * facets.At(u, v);
						const Color gold = Color{ 250.0f, 186.0f, 106.0f } * (shade * ridge * (0.35f + std::min(1.0f, heat) * 0.9f));
						color = Mix(color, gold, crystals * 0.6f);
						// Crease highlights: the sharp ridges glint white-gold.
						const float crease = ridge * ridge * ridge * ridge;
						const float glint = crease * crystals * (0.25f + std::min(1.2f, heat) * 1.1f);
						color = { color.R + 255.0f * glint, color.G + 226.0f * glint, color.B + 172.0f * glint };
						// Dark rolling smoke between the bright structures gives the breach depth.
						color = color * (sootField.At(u, v) * canopy);
						rift[static_cast<size_t>(y) * PixelWidth + x] = Pack(color.R, color.G, color.B, 255.0f);
					}
				}
			});
	}

	void StartupBannerRiftRenderer::AddLine(Vec2 a, Vec2 b, float width, Color color)
	{
		color = color * Fade;
		if (color.R + color.G + color.B < 0.5f)
		{
			return;
		}
		const float s = Scale;
		const float halfWidth = std::max(0.35f, width * 0.5f * s);
		uint32_t* frame = Frame.data();
		const int stride = PixelWidth;
		RasterizeSegment((a.X + ShakeX) * s, (a.Y + ShakeY) * s, (b.X + ShakeX) * s, (b.Y + ShakeY) * s,
			halfWidth, PixelWidth, PixelHeight, [&](int x, int y, float distance)
			{
				const float coverage = Saturate(halfWidth + 0.5f - distance);
				AddLight(frame[static_cast<size_t>(y) * stride + x], static_cast<int>(color.R * coverage),
					static_cast<int>(color.G * coverage), static_cast<int>(color.B * coverage));
			});
	}

	void StartupBannerRiftRenderer::StrokeCrack(Vec2 a, Vec2 b, float seamWidth, float darkness, float coreWidth, Color core)
	{
		core = core * Fade;
		const float s = Scale;
		const float seamHalf = std::max(0.5f, seamWidth * 0.5f * s);
		const float coreHalf = std::max(0.35f, coreWidth * 0.5f * s);
		uint32_t* frame = Frame.data();
		const int stride = PixelWidth;
		RasterizeSegment((a.X + ShakeX) * s, (a.Y + ShakeY) * s, (b.X + ShakeX) * s, (b.Y + ShakeY) * s,
			std::max(seamHalf, coreHalf), PixelWidth, PixelHeight, [&](int x, int y, float distance)
			{
				uint32_t& pixel = frame[static_cast<size_t>(y) * stride + x];
				// The physical gap darkens the surface; the glow sits inside it.
				const int keep = 256 - static_cast<int>(darkness * Saturate(seamHalf + 0.5f - distance) * 256.0f);
				const uint32_t r = (pixel >> 16 & 255) * keep >> 8, g = (pixel >> 8 & 255) * keep >> 8, bl = (pixel & 255) * keep >> 8;
				pixel = (pixel & 0xff000000u) | r << 16 | g << 8 | bl;
				const float coverage = Saturate(coreHalf + 0.5f - distance);
				if (coverage > 0.0f)
				{
					AddLight(pixel, static_cast<int>(core.R * coverage), static_cast<int>(core.G * coverage), static_cast<int>(core.B * coverage));
				}
			});
	}

	void StartupBannerRiftRenderer::EmitLine(Vec2 a, Vec2 b, float width, Color color)
	{
		color = color * Fade;
		const float s = Scale * 0.25f;
		const float halfWidth = std::max(0.5f, width * 0.5f * s);
		float* emission = Emission.data();
		const int stride = EmissionWidth;
		RasterizeSegment((a.X + ShakeX) * s, (a.Y + ShakeY) * s, (b.X + ShakeX) * s, (b.Y + ShakeY) * s,
			halfWidth, EmissionWidth, EmissionHeight, [&](int x, int y, float distance)
			{
				const float coverage = Saturate(halfWidth + 0.5f - distance);
				float* pixel = emission + (static_cast<size_t>(y) * stride + x) * 3;
				pixel[0] += color.R * coverage;
				pixel[1] += color.G * coverage;
				pixel[2] += color.B * coverage;
			});
	}

	void StartupBannerRiftRenderer::EmitBlob(Vec2 center, float radius, Color color)
	{
		color = color * Fade;
		const float s = Scale * 0.25f;
		const float cx = (center.X + ShakeX) * s, cy = (center.Y + ShakeY) * s, r = std::max(1.0f, radius * s);
		const int x0 = std::max(0, static_cast<int>(cx - r)), x1 = std::min(EmissionWidth - 1, static_cast<int>(cx + r) + 1);
		const int y0 = std::max(0, static_cast<int>(cy - r)), y1 = std::min(EmissionHeight - 1, static_cast<int>(cy + r) + 1);
		const float inverse = 1.0f / (r * r);
		float* emission = Emission.data();
		for (int y = y0; y <= y1; ++y)
		{
			for (int x = x0; x <= x1; ++x)
			{
				const float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
				const float falloff = 1.0f - (dx * dx + dy * dy) * inverse;
				if (falloff <= 0.0f)
				{
					continue;
				}
				// Sharper core with a long tail reads as a light source, not a disc.
				const float k = falloff * falloff * falloff;
				float* pixel = emission + (static_cast<size_t>(y) * EmissionWidth + x) * 3;
				pixel[0] += color.R * k;
				pixel[1] += color.G * k;
				pixel[2] += color.B * k;
			}
		}
	}

	void StartupBannerRiftRenderer::EmitRing(Vec2 center, float radius, float squash, float width, Color color)
	{
		const int segments = std::clamp(static_cast<int>(radius * 0.25f), 24, 160);
		Vec2 previous{ center.X + radius, center.Y };
		for (int index = 1; index <= segments; ++index)
		{
			const float angle = index * 2.0f * Pi / segments;
			const Vec2 next{ center.X + std::cos(angle) * radius, center.Y + std::sin(angle) * radius * squash };
			EmitLine(previous, next, width, color);
			previous = next;
		}
	}

	void StartupBannerRiftRenderer::DrawBase(float time)
	{
		// Writes the opaque, unlit surface. Exposure and the feathered edge are
		// applied later in ApplyBloom, which touches every pixel anyway; most rows
		// here are therefore a plain shifted copy of the pre-graded sky.
		const float grade = bHasDesktop ? SmoothStep(time / 0.55f) : 1.0f;
		const float impactAge = time - ImpactSeconds;
		const float riftGain = impactAge < 0.0f
			? 0.0f
			: (1.0f + 0.25f * std::exp(-impactAge * 2.4f)) * (0.94f + 0.06f * std::sin(time * 11.0f));
		const int shiftX = static_cast<int>(std::lround(ShakeX * Scale));
		const int shiftY = static_cast<int>(std::lround(ShakeY * Scale));
		const int driftX = static_cast<int>(std::lround(std::sin(time * 0.9f) * 6.0f * Scale));
		const int driftY = static_cast<int>(std::lround(-std::max(0.0f, impactAge) * 12.0f * Scale));
		const bool bRipple = impactAge >= 0.0f && impactAge < 0.5f;
		const float rippleRadius = (40.0f + impactAge * 1450.0f) * Scale;
		const float rippleWidth = 40.0f * Scale;
		const float rippleAmplitude = 16.0f * Scale * (1.0f - impactAge / 0.5f);
		const float focusX = (Focus.X + ShakeX) * Scale, focusY = (Focus.Y + ShakeY) * Scale;

		// Per cell: 0 while intact, otherwise the brightness of its patch of breach.
		// Each cell flares as it opens, so the breach visibly spreads outward.
		bool bAnyOpen = false;
		CellGains.assign(65536, 0);
		for (size_t index = 0; index < Cells.size(); ++index)
		{
			if (Cells[index].bBreakable && time >= Cells[index].BreakTime)
			{
				const float gain = (1.0f + 0.35f * std::exp(-(time - Cells[index].BreakTime) * 14.0f)) * riftGain;
				CellGains[index] = std::max(1, static_cast<int>(gain * 256.0f));
				bAnyOpen = true;
			}
		}
		const int fixedGrade = static_cast<int>(grade * 256.0f + 0.5f);
		const int* gain = CellGains.data();
		const uint16_t* cells = CellMap.data();
		const uint32_t* sky = Sky.data();
		const uint32_t* desktop = bHasDesktop && fixedGrade < 256 ? Desktop.data() : nullptr;
		const uint32_t* rift = Rift.data();
		const int width = PixelWidth, height = PixelHeight;
		uint32_t* const frame = Frame.data();
		const int* const spans = RowSpans.data();
		ForRows(height, [&](int firstRow, int endRow)
			{
				const int* const cellGain = gain;
				const uint16_t* const cellMap = cells;
				const int offsetX = shiftX, offsetY = shiftY, riftX = driftX;
				for (int y = firstRow; y < endRow; ++y)
				{
					uint32_t* const out = frame + static_cast<size_t>(y) * width;
					const int sy = std::clamp(y - offsetY, 0, height - 1);
					const uint32_t* skyRow = sky + static_cast<size_t>(sy) * width;
					const uint32_t* riftRow = rift + static_cast<size_t>(std::clamp(y + driftY, 0, height - 1)) * width;
					const bool bBreachRow = bAnyOpen && spans[sy * 2] <= spans[sy * 2 + 1];
					if (!desktop && !bRipple)
					{
						// Fast path: shifted copy, clamped at the edges.
						const int begin = std::clamp(offsetX, 0, width), end = std::clamp(width + offsetX, 0, width);
						std::fill(out, out + begin, skyRow[0]);
						std::copy(skyRow + (begin - offsetX), skyRow + (end - offsetX), out + begin);
						std::fill(out + end, out + width, skyRow[width - 1]);
						if (bBreachRow)
						{
							const uint16_t* const cellRow = cellMap + static_cast<size_t>(sy) * width;
							const int x0 = std::max(0, spans[sy * 2] + offsetX), x1 = std::min(width - 1, spans[sy * 2 + 1] + offsetX);
							for (int x = x0; x <= x1; ++x)
							{
								const int open = cellGain[cellRow[x - offsetX]];
								if (open)
								{
									const uint32_t pixel = riftRow[std::clamp(x + riftX, 0, width - 1)];
									const int r = static_cast<int>(pixel >> 16 & 255) * open >> 8;
									const int g = static_cast<int>(pixel >> 8 & 255) * open >> 8;
									const int b = static_cast<int>(pixel & 255) * open >> 8;
									out[x] = static_cast<uint32_t>(std::min(b, 255)) | static_cast<uint32_t>(std::min(g, 255)) << 8 |
										static_cast<uint32_t>(std::min(r, 255)) << 16 | 0xff000000u;
								}
							}
						}
						continue;
					}
					for (int x = 0; x < width; ++x)
					{
						int sx = x - shiftX;
						int rowSource = sy;
						if (bRipple)
						{
							const float dx = x - focusX, dy = (y - focusY) * 1.6f;
							const float distance = std::sqrt(dx * dx + dy * dy) + 1.0e-3f;
							const float band = (distance - rippleRadius) / rippleWidth;
							if (band > -3.0f && band < 3.0f)
							{
								const float push = rippleAmplitude * std::exp(-band * band) * band;
								sx -= static_cast<int>(dx / distance * push);
								rowSource = std::clamp(rowSource - static_cast<int>(dy / distance * push / 1.6f), 0, height - 1);
							}
						}
						sx = sx < 0 ? 0 : (sx >= width ? width - 1 : sx);
						const size_t source = static_cast<size_t>(rowSource) * width + sx;
						const int open = gain[cells[source]];
						int r, g, b;
						if (open)
						{
							const uint32_t pixel = riftRow[std::clamp(x + driftX, 0, width - 1)];
							r = static_cast<int>(pixel >> 16 & 255) * open >> 8;
							g = static_cast<int>(pixel >> 8 & 255) * open >> 8;
							b = static_cast<int>(pixel & 255) * open >> 8;
						}
						else
						{
							const uint32_t color = sky[source];
							r = static_cast<int>(color >> 16 & 255);
							g = static_cast<int>(color >> 8 & 255);
							b = static_cast<int>(color & 255);
							if (desktop)
							{
								const uint32_t original = desktop[source];
								const int ro = static_cast<int>(original >> 16 & 255), go = static_cast<int>(original >> 8 & 255), bo = static_cast<int>(original & 255);
								r = ro + ((r - ro) * fixedGrade >> 8);
								g = go + ((g - go) * fixedGrade >> 8);
								b = bo + ((b - bo) * fixedGrade >> 8);
							}
						}
						out[x] = static_cast<uint32_t>(std::min(b, 255)) | static_cast<uint32_t>(std::min(g, 255)) << 8 |
							static_cast<uint32_t>(std::min(r, 255)) << 16 | 0xff000000u;
					}
				}
			});
	}

	void StartupBannerRiftRenderer::DrawShards(float time)
	{
		std::vector<int> order;
		for (size_t index = 0; index < Cells.size(); ++index)
		{
			const Cell& cell = Cells[index];
			if (cell.bBreakable && time >= cell.BreakTime && time < cell.BreakTime + ShardLifeSeconds)
			{
				order.push_back(static_cast<int>(index));
			}
		}
		// Earlier breaks are closer to the camera by now: draw them last.
		std::sort(order.begin(), order.end(), [&](int a, int b)
			{
				return Cells[a].BreakTime > Cells[b].BreakTime;
			});

		const uint16_t* cells = CellMap.data();
		const uint32_t* sky = Sky.data();
		uint32_t* frame = Frame.data();
		std::vector<Vec2> outline;
		for (const int index : order)
		{
			const Cell& cell = Cells[index];
			const float age = time - cell.BreakTime;
			Vec2 direction{ cell.Centroid.X - Focus.X, cell.Centroid.Y - Focus.Y };
			const float distance = Length(direction);
			direction = distance > 1.0f ? Vec2{ direction.X / distance, direction.Y / distance } : Vec2{ 0.0f, -1.0f };
			// A violent kick that bleeds off, plus a slow fall and a rush toward camera.
			constexpr float drag = 3.4f;
			const float travel = cell.Launch / drag * (1.0f - std::exp(-age * drag));
			const Vec2 center{ cell.Centroid.X + direction.X * travel + ShakeX,
				cell.Centroid.Y + direction.Y * travel + 150.0f * age * age + ShakeY };
			const float scale = 1.0f + cell.Approach * age * (1.0f + age);
			const float roll = cell.Spin * age;
			const float tilt = std::cos(std::min(age * cell.TiltRate, 1.45f));
			const float ux = std::cos(cell.TiltAngle), uy = std::sin(cell.TiltAngle);
			// M = scale * Rotation(roll) * Squash(axis u, tilt): weak perspective.
			const float s00 = 1.0f + (tilt - 1.0f) * ux * ux, s01 = (tilt - 1.0f) * ux * uy, s11 = 1.0f + (tilt - 1.0f) * uy * uy;
			const float cr = std::cos(roll), sr = std::sin(roll);
			const float m00 = scale * (cr * s00 - sr * s01), m01 = scale * (cr * s01 - sr * s11);
			const float m10 = scale * (sr * s00 + cr * s01), m11 = scale * (sr * s01 + cr * s11);
			const float determinant = m00 * m11 - m01 * m10;
			if (std::abs(determinant) < 1.0e-3f)
			{
				continue;
			}
			const float i00 = m11 / determinant, i01 = -m01 / determinant, i10 = -m10 / determinant, i11 = m00 / determinant;

			const float opacity = 1.0f - SmoothStep((age - 0.36f) / 0.52f);
			const float warm = Saturate(age * 3.5f);
			// Facets flash as they turn through the light of the breach.
			const float phase = std::fmod(age * cell.TiltRate * 0.8f, Pi) - 0.9f;
			const float glint = std::exp(-phase * phase * 22.0f);
			float minX = 1.0e9f, minY = 1.0e9f, maxX = -1.0e9f, maxY = -1.0e9f;
			outline.clear();
			for (const Vec2& point : cell.Outline)
			{
				const float x = point.X - cell.Centroid.X, y = point.Y - cell.Centroid.Y;
				const Vec2 projected{ center.X + m00 * x + m01 * y, center.Y + m10 * x + m11 * y };
				outline.push_back(projected);
				minX = std::min(minX, projected.X);
				minY = std::min(minY, projected.Y);
				maxX = std::max(maxX, projected.X);
				maxY = std::max(maxY, projected.Y);
			}
			const int x0 = std::max(0, static_cast<int>(minX * Scale)), x1 = std::min(PixelWidth - 1, static_cast<int>(maxX * Scale) + 1);
			const int y0 = std::max(0, static_cast<int>(minY * Scale)), y1 = std::min(PixelHeight - 1, static_cast<int>(maxY * Scale) + 1);
			// Faces turn towards the fire: cold steel warming to bronze, darker when edge-on.
			const float faceLight = (0.30f + 0.35f * tilt) * (1.0f - 0.4f * warm);
			const float fireR = 34.0f * warm + 120.0f * glint, fireG = 14.0f * warm + 84.0f * glint, fireB = 4.0f * warm + 40.0f * glint;
			for (int y = y0; y <= y1; ++y)
			{
				const float py = (y + 0.5f) / Scale - center.Y;
				const float alphaRow = opacity;
				uint32_t* out = frame + static_cast<size_t>(y) * PixelWidth;
				for (int x = x0; x <= x1; ++x)
				{
					const float px = (x + 0.5f) / Scale - center.X;
					const int sx = static_cast<int>((cell.Centroid.X + i00 * px + i01 * py) * Scale);
					const int sy = static_cast<int>((cell.Centroid.Y + i10 * px + i11 * py) * Scale);
					if (sx < 0 || sy < 0 || sx >= PixelWidth || sy >= PixelHeight)
					{
						continue;
					}
					const size_t source = static_cast<size_t>(sy) * PixelWidth + sx;
					if (cells[source] != index)
					{
						continue;
					}
					const uint32_t color = sky[source];
					const float r = (color >> 16 & 255), g = (color >> 8 & 255), b = (color & 255);
					const float luminance = (0.3f * r + 0.6f * g + 0.1f * b) / 255.0f;
					const float lit = 0.5f + luminance;
					const float alpha = alphaRow;
					const float keep = 1.0f - alpha;
					const uint32_t pixel = out[x];
					out[x] = Pack((pixel >> 16 & 255) * keep + std::min(255.0f, r * faceLight + fireR * lit) * alpha,
						(pixel >> 8 & 255) * keep + std::min(255.0f, g * faceLight + fireG * lit) * alpha,
						(pixel & 255) * keep + std::min(255.0f, b * faceLight + fireB * lit) * alpha, 255.0f);
				}
			}
			// Gold rims: the cut edges of the sky catch the fire behind them.
			const float rim = opacity * (0.6f + glint * 1.0f);
			const float savedX = ShakeX, savedY = ShakeY;
			ShakeX = ShakeY = 0.0f; // Outline is already in screen space.
			for (size_t point = 0; point < outline.size(); ++point)
			{
				const Vec2 a = outline[point], b = outline[(point + 1) % outline.size()];
				AddLine(a, b, 0.55f + 0.15f * scale, Color{ 255.0f, 200.0f, 130.0f } * (0.8f * rim));
				EmitLine(a, b, 1.0f, Gold * (0.32f * rim));
			}
			ShakeX = savedX;
			ShakeY = savedY;
		}
	}

	void StartupBannerRiftRenderer::DrawWeb(float time)
	{
		const float tension = SmoothStep((time - 1.0f) / 0.8f);
		const float inhale = 1.0f - 0.55f * SmoothStep((time - 1.62f) / 0.20f);
		for (const Edge& edge : Edges)
		{
			if (time < edge.BirthTime)
			{
				continue;
			}
			const bool bFirstOpen = IsOpen(edge.First, time) && Cells[edge.First].bBreakable;
			const bool bSecondOpen = IsOpen(edge.Second, time) && Cells[edge.Second].bBreakable;
			if (bFirstOpen && bSecondOpen)
			{
				continue;
			}
			const float grow = Saturate((time - edge.BirthTime) / 0.04f);
			const Vec2 end = Lerp(edge.Near, edge.Far, grow);
			if (bFirstOpen || bSecondOpen)
			{
				// The torn border of the breach burns white-gold, then settles to embers.
				const float firstBreak = bFirstOpen ? Cells[edge.First].BreakTime : 0.0f;
				const float secondBreak = bSecondOpen ? Cells[edge.Second].BreakTime : 0.0f;
				const float opened = time - std::max(firstBreak, secondBreak);
				const float heat = 0.5f + 1.1f * std::exp(-opened * 4.0f);
				AddLine(edge.Near, edge.Far, 1.3f, Color{ 255.0f, 196.0f, 120.0f } * heat);
				EmitLine(edge.Near, edge.Far, 1.4f, Mix(Ember, ImpactWhite, Saturate(std::exp(-opened * 5.0f))) * (0.8f * heat));
				continue;
			}
			if (edge.bHidden)
			{
				continue;
			}
			const float middle = edge.Distance + 0.5f * Length({ edge.Far.X - edge.Near.X, edge.Far.Y - edge.Near.Y });
			float wave = 0.0f;
			for (const float pulse : PulseTimes)
			{
				if (time >= pulse && time < ImpactSeconds)
				{
					const float band = (middle - (time - pulse) * 1500.0f) / 40.0f;
					wave += std::exp(-band * band);
				}
			}
			float glow = (0.32f + 0.25f * tension + wave * 1.25f) * inhale;
			if (time >= ImpactSeconds)
			{
				glow *= 0.45f;
			}
			// Birth flash: each fracture appears white-hot before cooling.
			glow += 1.3f * std::exp(-(time - edge.BirthTime) * 16.0f);
			StrokeCrack(edge.Near, end, 2.2f, 0.55f, 0.75f, Color{ 150.0f, 210.0f, 255.0f } * glow);
			EmitLine(edge.Near, end, 1.0f, Mix(Ice, IceWhite, Saturate(wave)) * (glow * 0.42f));
		}
	}

	void StartupBannerRiftRenderer::DrawCracks(float time)
	{
		const float tension = SmoothStep((time - 1.0f) / 0.8f);
		const float inhale = 1.0f - 0.55f * SmoothStep((time - 1.62f) / 0.20f);
		const float impactAge = time - ImpactSeconds;
		const float cooling = impactAge < 0.0f ? 1.0f : 0.35f + 0.65f * std::exp(-impactAge * 1.6f);
		for (const Crack& crack : Cracks)
		{
			const float elapsed = time - crack.StartTime;
			if (elapsed < 0.0f || crack.Points.size() < 2)
			{
				continue;
			}
			const float grown = elapsed * CrackSpeed;
			for (size_t index = 0; index + 1 < crack.Points.size(); ++index)
			{
				const float start = crack.Lengths[index];
				if (start >= grown)
				{
					break;
				}
				const float stop = crack.Lengths[index + 1];
				const float portion = Saturate((grown - start) / std::max(0.001f, stop - start));
				const Vec2 a = crack.Points[index];
				const Vec2 b = Lerp(a, crack.Points[index + 1], portion);
				if (impactAge >= 0.0f)
				{
					const int mx = static_cast<int>((a.X + b.X) * 0.5f * Scale), my = static_cast<int>((a.Y + b.Y) * 0.5f * Scale);
					if (mx >= 0 && my >= 0 && mx < PixelWidth && my < PixelHeight)
					{
						const uint16_t cell = CellMap[static_cast<size_t>(my) * PixelWidth + mx];
						if (cell != NoCell && IsOpen(cell, time))
						{
							continue;
						}
					}
				}
				// Light runs outward along the fractures in accelerating pulses.
				const float along = (start + stop) * 0.5f;
				float pulse = 0.0f;
				for (const float pulseTime : PulseTimes)
				{
					const float behind = (time - pulseTime - crack.Phase) * PulseSpeed - along;
					if (behind >= 0.0f && behind < 170.0f && time < ImpactSeconds + 0.05f)
					{
						pulse = std::max(pulse, std::exp(-behind / 50.0f));
					}
				}
				// The freshly torn section just behind the tip is still white-hot.
				const float fresh = elapsed < 0.7f ? std::exp(-std::max(0.0f, grown - along) / 70.0f) : 0.0f;
				const float glow = ((0.38f + 0.22f * tension) * inhale * cooling + pulse * 1.7f + fresh * 1.4f) * crack.Intensity;
				StrokeCrack(a, b, 2.0f * crack.Width, 0.55f, 0.7f * crack.Width, Color{ 150.0f, 212.0f, 255.0f } * glow);
				EmitLine(a, b, 1.0f, Mix(Ice, IceWhite, Saturate(pulse + fresh)) * (glow * 0.40f * crack.Width));
			}
			// A spark leads each fracture as it races outward.
			if (grown < crack.Lengths.back())
			{
				size_t index = 0;
				while (index + 2 < crack.Lengths.size() && crack.Lengths[index + 1] < grown)
				{
					++index;
				}
				const float span = std::max(0.001f, crack.Lengths[index + 1] - crack.Lengths[index]);
				const Vec2 tip = Lerp(crack.Points[index], crack.Points[index + 1], Saturate((grown - crack.Lengths[index]) / span));
				EmitBlob(tip, 9.0f, IceWhite * (0.9f * crack.Intensity));
				AddLine({ tip.X - 0.5f, tip.Y }, { tip.X + 0.5f, tip.Y }, 2.6f, Color{ 230.0f, 245.0f, 255.0f } * crack.Intensity);
			}
		}
	}

	void StartupBannerRiftRenderer::DrawTendrils(float time)
	{
		if (time < 0.06f || time > IgnitionSeconds + 0.04f)
		{
			return;
		}
		const float charge = SmoothStep((time - 0.06f) / (IgnitionSeconds - 0.12f));
		constexpr int samples = 32;
		for (const Tendril& tendril : Tendrils)
		{
			Vec2 previous = tendril.Start;
			for (int index = 1; index <= samples; ++index)
			{
				const float t = index / static_cast<float>(samples);
				const float inverse = 1.0f - t;
				const Vec2 point{ inverse * inverse * tendril.Start.X + 2.0f * inverse * t * tendril.Control.X + t * t * Focus.X,
					inverse * inverse * tendril.Start.Y + 2.0f * inverse * t * tendril.Control.Y + t * t * Focus.Y };
				// Energy streams inward: bright packets flow along each filament.
				const float flow = t * 2.2f - time * 3.4f + tendril.Phase;
				const float packet = std::pow(0.5f + 0.5f * std::cos((flow - std::floor(flow)) * 2.0f * Pi), 5.0f);
				const float glow = charge * (0.35f + 1.5f * packet) * (0.2f + 0.8f * t) * (0.75f + 0.45f * charge);
				AddLine(previous, point, tendril.Width * (0.5f + 0.7f * t), Color{ 150.0f, 210.0f, 255.0f } * glow);
				EmitLine(previous, point, 1.0f, Ice * (glow * 0.75f));
				previous = point;
			}
		}
	}

	void StartupBannerRiftRenderer::DrawEmbers(float time)
	{
		const float age = time - ImpactSeconds;
		if (age < 0.0f)
		{
			return;
		}
		for (unsigned int index = 0; index < 280; ++index)
		{
			const float life = 0.55f + Noise(index * 9u + 1u) * 1.1f;
			const float local = age - Noise(index * 9u + 5u) * 0.10f;
			if (local <= 0.0f || local > life)
			{
				continue;
			}
			// Most embers spray up and out of the tear, following its shape.
			const float angle = -Pi * 0.5f + (Noise(index * 9u + 2u) - 0.5f) * 2.0f * Pi * 0.62f;
			const float speed = 240.0f + Noise(index * 9u + 3u) * Noise(index * 9u + 4u) * 1350.0f;
			constexpr float drag = 2.3f;
			const Vec2 direction{ std::cos(angle), std::sin(angle) * 0.8f };
			const float travel = speed / drag * (1.0f - std::exp(-local * drag)) + 25.0f;
			const float velocity = speed * std::exp(-local * drag);
			const Vec2 position{ Focus.X + direction.X * travel, Focus.Y + direction.Y * travel + 80.0f * local * local };
			const Vec2 tail{ position.X - direction.X * velocity * 0.03f, position.Y - (direction.Y * velocity + 160.0f * local) * 0.03f };
			const float fade = std::pow(1.0f - local / life, 1.5f);
			const float heat = Noise(index * 9u + 6u);
			const Color color = Mix(Color{ 255.0f, 140.0f, 50.0f }, Color{ 255.0f, 232.0f, 196.0f }, heat * fade);
			AddLine(tail, position, 0.8f + Noise(index * 9u + 7u) * 1.3f, color * fade);
			EmitLine(tail, position, 1.0f, Mix(Ember, Gold, heat) * (fade * 0.55f));
		}
	}

	void StartupBannerRiftRenderer::DrawLightSources(float time)
	{
		const float charge = SmoothStep((time - 0.06f) / (IgnitionSeconds - 0.12f));
		const float ignitionAge = time - IgnitionSeconds;
		const float impactAge = time - ImpactSeconds;
		if (ignitionAge < 0.0f)
		{
			// Gathering point: tight and flickering as the charge builds.
			const float flicker = 0.85f + 0.15f * std::sin(time * 63.0f) * std::sin(time * 41.0f);
			EmitBlob(Focus, 30.0f + 50.0f * charge, IceWhite * ((0.25f + 1.6f * charge * charge) * flicker));
			AddLine({ Focus.X - 0.5f, Focus.Y }, { Focus.X + 0.5f, Focus.Y }, 1.5f + 4.0f * charge, Color{ 230.0f, 245.0f, 255.0f } * (0.4f + charge));
		}
		else if (impactAge < 0.0f)
		{
			// Ignition, then a held, trembling charge at the heart of the web.
			const float flash = std::exp(-ignitionAge * 8.0f);
			EmitBlob(Focus, 90.0f + 220.0f * EaseOutCubic(ignitionAge / 0.35f), IceWhite * (2.2f * flash));
			const float tension = SmoothStep((time - 1.0f) / 0.8f);
			const float inhale = 1.0f - 0.65f * SmoothStep((time - 1.62f) / 0.20f);
			const float throb = 0.75f + 0.25f * std::sin(time * 38.0f);
			EmitBlob(Focus, 55.0f + 35.0f * tension, IceWhite * ((0.55f + 1.1f * tension) * throb * inhale));
			AddLine({ Focus.X - 0.5f, Focus.Y }, { Focus.X + 0.5f, Focus.Y }, 3.0f + 3.0f * tension,
				Color{ 230.0f, 245.0f, 255.0f } * (inhale * (0.6f + 0.4f * throb)));
		}
		else
		{
			// The blow: a blinding pinpoint, a wide warm bloom and a starburst of rays.
			const float blast = std::exp(-impactAge * 7.0f);
			EmitBlob(Focus, 70.0f + 60.0f * EaseOutCubic(impactAge / 0.3f), ImpactWhite * (3.6f * blast));
			EmitBlob(Focus, 260.0f + 260.0f * EaseOutCubic(impactAge / 0.4f), Mix(Gold, ImpactWhite, blast) * (0.75f * blast));
			if (impactAge < 0.22f)
			{
				const float rays = 1.0f - impactAge / 0.22f;
				for (unsigned int index = 0; index < 44; ++index)
				{
					const float angle = Noise(index * 5u + 3001u) * 2.0f * Pi;
					const float reach = (160.0f + Noise(index * 5u + 3002u) * 420.0f) * EaseOutCubic(impactAge / 0.08f + 0.2f);
					const Vec2 end{ Focus.X + std::cos(angle) * reach, Focus.Y + std::sin(angle) * reach * 0.8f };
					EmitLine(Focus, end, 1.0f + Noise(index * 5u + 3003u) * 2.0f, ImpactWhite * (1.6f * rays * rays));
				}
			}
			if (impactAge < 0.55f)
			{
				const float fade = 1.0f - impactAge / 0.55f;
				EmitRing(Focus, 40.0f + impactAge * 1450.0f, 0.62f, 6.0f, Mix(Ember, ImpactWhite, fade) * (1.1f * fade));
			}
			// The breach keeps breathing light into the sky around it.
			const float burn = (1.0f + 0.12f * std::sin(time * 9.0f)) * Saturate(impactAge * 8.0f);
			EmitBlob(Focus, 330.0f, Ember * (0.32f * burn));
			EmitBlob(Focus, 100.0f, Gold * (0.42f * burn));
		}
		// The banner rises out of a soft, warm glow during the reveal.
		const float reveal = SmoothStep((time - RevealSeconds) / 0.30f);
		if (reveal > 0.0f)
		{
			const Vec2 center{ (BannerLeft + BannerWidth * 0.5f) / Scale - ShakeX, (BannerTop + BannerHeight * 0.5f) / Scale - ShakeY };
			EmitBlob(center, 430.0f, Color{ 0.62f, 0.44f, 0.40f } * (0.55f * reveal));
		}
	}

	void StartupBannerRiftRenderer::ApplyBloom(float time)
	{
		// Wide halo at eighth resolution, folded back into the quarter-resolution glow.
		std::fill(Halo.begin(), Halo.end(), 0.0f);
		const float* emission = Emission.data();
		float* halo = Halo.data();
		for (int y = 0; y < EmissionHeight; ++y)
		{
			for (int x = 0; x < EmissionWidth; ++x)
			{
				const float* source = emission + (static_cast<size_t>(y) * EmissionWidth + x) * 3;
				float* target = halo + (static_cast<size_t>(y / 2) * HaloWidth + x / 2) * 3;
				target[0] += source[0] * 0.25f;
				target[1] += source[1] * 0.25f;
				target[2] += source[2] * 0.25f;
			}
		}
		BoxBlur(Emission, BlurScratch, EmissionWidth, EmissionHeight, 1);
		BoxBlur(Emission, BlurScratch, EmissionWidth, EmissionHeight, 1);
		BoxBlur(Halo, BlurScratch, HaloWidth, HaloHeight, 4);
		BoxBlur(Halo, BlurScratch, HaloWidth, HaloHeight, 4);
		BoxBlur(Halo, BlurScratch, HaloWidth, HaloHeight, 4);
		float* glow = Emission.data();
		for (int y = 0; y < EmissionHeight; ++y)
		{
			const float hy = std::clamp((y + 0.5f) * 0.5f - 0.5f, 0.0f, HaloHeight - 1.0f);
			const int y0 = static_cast<int>(hy), y1 = std::min(y0 + 1, HaloHeight - 1);
			const float fy = hy - y0;
			for (int x = 0; x < EmissionWidth; ++x)
			{
				const float hx = std::clamp((x + 0.5f) * 0.5f - 0.5f, 0.0f, HaloWidth - 1.0f);
				const int x0 = static_cast<int>(hx), x1 = std::min(x0 + 1, HaloWidth - 1);
				const float fx = hx - x0;
				float* target = glow + (static_cast<size_t>(y) * EmissionWidth + x) * 3;
				for (int channel = 0; channel < 3; ++channel)
				{
					const float top = halo[(static_cast<size_t>(y0) * HaloWidth + x0) * 3 + channel] * (1.0f - fx) +
						halo[(static_cast<size_t>(y0) * HaloWidth + x1) * 3 + channel] * fx;
					const float bottom = halo[(static_cast<size_t>(y1) * HaloWidth + x0) * 3 + channel] * (1.0f - fx) +
						halo[(static_cast<size_t>(y1) * HaloWidth + x1) * 3 + channel] * fx;
					target[channel] = target[channel] * 0.9f + (top + (bottom - top) * fy) * 1.2f;
				}
			}
		}

		// Full-frame flashes at ignition and impact give the beats a physical punch.
		const float ignitionAge = time - IgnitionSeconds;
		const float impactAge = time - ImpactSeconds;
		Color flash{};
		if (ignitionAge >= 0.0f && impactAge < 0.0f)
		{
			flash = IceWhite * (45.0f * std::exp(-ignitionAge * 18.0f));
		}
		if (impactAge >= 0.0f)
		{
			flash = Color{ 1.0f, 0.82f, 0.58f } * (95.0f * std::exp(-impactAge * 24.0f));
		}
		flash = flash * Fade;

		// The world dims for a breath before the blow, then is lit by the breach.
		float exposure = 1.0f - 0.38f * SmoothStep((time - 1.60f) / 0.22f);
		if (impactAge >= 0.0f)
		{
			exposure = 1.0f + 0.22f * std::exp(-impactAge * 2.2f);
		}
		if (ignitionAge >= 0.0f && impactAge < 0.0f)
		{
			exposure += 0.30f * std::exp(-ignitionAge * 9.0f);
		}

		// Upsample each emission row horizontally once into B,G,R,0 floats (pixel
		// lane order), then blend the two neighbouring rows per pixel.
		auto upsampleRow = [&](int row, float* target)
		{
			const float* source = glow + static_cast<size_t>(row) * EmissionWidth * 3;
			const int* index = UpsampleColumns.data();
			const float* weight = UpsampleWeights.data();
			for (int x = 0; x < PixelWidth; ++x)
			{
				const float* a = source + index[x];
				const float k = weight[x];
				target[x * 4] = (a[2] + (a[5] - a[2]) * k) * 255.0f;
				target[x * 4 + 1] = (a[1] + (a[4] - a[1]) * k) * 255.0f;
				target[x * 4 + 2] = (a[0] + (a[3] - a[0]) * k) * 255.0f;
				target[x * 4 + 3] = 0.0f;
			}
		};
		const size_t rowFloats = static_cast<size_t>(PixelWidth) * 4;
		UpsampledRows.resize(rowFloats * 8);
		ForRows(PixelHeight, [&](int firstRow, int endRow)
			{
				// Each slice owns a pair of cached rows; there are at most four slices.
				const size_t slot = std::min<size_t>(static_cast<size_t>(firstRow) * 4 / std::max(1, PixelHeight), 3);
				float* upper = UpsampledRows.data() + rowFloats * 2 * slot;
				float* lower = upper + rowFloats;
				int upperRow = -1, lowerRow = -1;
				for (int y = firstRow; y < endRow; ++y)
				{
					const float ey = std::clamp((y + 0.5f) * 0.25f - 0.5f, 0.0f, EmissionHeight - 1.001f);
					const int row0 = static_cast<int>(ey);
					const int row1 = std::min(row0 + 1, EmissionHeight - 1);
					if (row0 != upperRow)
					{
						if (row0 == lowerRow)
						{
							std::swap(upper, lower);
						}
						else
						{
							upsampleRow(row0, upper);
						}
						upsampleRow(row1, lower);
						upperRow = row0;
						lowerRow = row1;
					}
					CompositeRow(Frame.data() + static_cast<size_t>(y) * PixelWidth, upper, lower, ey - row0,
						FeatherColumns.data(), FeatherRows[y] * Fade, exposure, flash, ToneCurve.data(), PixelWidth);
				}
			});
	}

	void StartupBannerRiftRenderer::DrawBanner(float opacity)
	{
		if (opacity <= 0.0f)
		{
			return;
		}
		for (int y = 0; y < BannerHeight; ++y)
		{
			const int ty = BannerTop + y;
			if (ty < 0 || ty >= PixelHeight)
			{
				continue;
			}
			const uint32_t* source = Banner.data() + static_cast<size_t>(y) * BannerWidth;
			uint32_t* target = Frame.data() + static_cast<size_t>(ty) * PixelWidth;
			for (int x = 0; x < BannerWidth; ++x)
			{
				const int tx = BannerLeft + x;
				if (tx < 0 || tx >= PixelWidth)
				{
					continue;
				}
				const uint32_t s = source[x], d = target[tx];
				if (opacity >= 1.0f && (s >> 24) == 255)
				{
					target[tx] = s;
					continue;
				}
				const float keep = 1.0f - (s >> 24) * opacity / 255.0f;
				target[tx] = Pack((d >> 16 & 255) * keep + (s >> 16 & 255) * opacity, (d >> 8 & 255) * keep + (s >> 8 & 255) * opacity,
					(d & 255) * keep + (s & 255) * opacity, (d >> 24) * keep + (s >> 24) * opacity);
			}
		}
	}

	const uint32_t* StartupBannerRiftRenderer::Render(float time)
	{
		if (time >= DurationSeconds || Sky.empty())
		{
			std::fill(Frame.begin(), Frame.end(), 0u);
			DrawBanner(1.0f);
			return Frame.data();
		}

		// Everything except the banner dissolves during the reveal.
		Fade = 1.0f - SmoothStep((time - RevealSeconds) / 0.74f);

		// A low rumble builds with the charge; the impact lands as a hard, decaying jolt.
		const float tension = SmoothStep((time - 1.05f) / 0.75f) * (1.0f - SmoothStep((time - 1.64f) / 0.12f));
		ShakeX = tension * 1.6f * (std::sin(time * 71.0f) + 0.6f * std::sin(time * 113.0f));
		ShakeY = tension * 1.2f * (std::sin(time * 89.0f + 1.3f) + 0.5f * std::sin(time * 131.0f));
		const float impactAge = time - ImpactSeconds;
		if (impactAge >= 0.0f)
		{
			const float jolt = 13.0f * std::exp(-impactAge * 7.5f);
			ShakeX += jolt * std::sin(impactAge * 83.0f);
			ShakeY += jolt * 0.8f * std::cos(impactAge * 67.0f);
		}
		const float ignitionAge = time - IgnitionSeconds;
		if (ignitionAge >= 0.0f && ignitionAge < 0.25f)
		{
			const float kick = 4.0f * std::exp(-ignitionAge * 14.0f);
			ShakeX += kick * std::sin(ignitionAge * 97.0f);
			ShakeY += kick * std::cos(ignitionAge * 79.0f);
		}

		std::fill(Emission.begin(), Emission.end(), 0.0f);
		DrawBase(time);
		DrawTendrils(time);
		DrawWeb(time);
		DrawCracks(time);
		DrawShards(time);
		DrawEmbers(time);
		DrawLightSources(time);
		ApplyBloom(time);
		DrawBanner(SmoothStep((time - 2.66f) / 0.58f));
		return Frame.data();
	}
}
