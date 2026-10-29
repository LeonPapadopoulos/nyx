// 3D noise and the smooth minimum (GradientNoise.h, SmoothMinimum.h): the noise is 0 at its lattice
// corners, within its proven bounds of value and slope, about as much above 0 as below, another for
// another seed; eight points at a time give the same bits as one; the values are those written
// down when it was made, so a change to them is seen (golden hashes); fractal and ridged noise stay
// in their ranges and bounds; the smooth minimum is the minimum away from the corner, at most a
// quarter of its radius below it, and no steeper than its inputs. Prints how fast it samples.
#include "GradientNoise.h"
#include "SmoothMinimum.h"
#include "TestSupport.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
	using Nyx::FractalNoise3D;
	using Nyx::Tests::Require;

	// Numbers spread over [0, 1), from integers only, the same everywhere
	struct Sequence
	{
		uint32_t State = 0x12345678u;

		float Next()
		{
			State = State * 1664525u + 1013904223u;
			return static_cast<float>(State >> 8) * (1.0f / 16777216.0f);
		}
	};

	uint64_t HashBits(uint64_t hash, float value)
	{
		uint32_t bits = 0;
		std::memcpy(&bits, &value, sizeof(bits));
		for (int byte = 0; byte < 4; ++byte)
		{
			hash ^= (bits >> (byte * 8)) & 0xFFu;
			hash *= 0x100000001B3ull;
		}
		return hash;
	}

	bool SameBits(float a, float b)
	{
		return std::memcmp(&a, &b, sizeof(float)) == 0;
	}

	FractalNoise3D MakeFractal()
	{
		FractalNoise3D noise;
		noise.Seed = 7;
		noise.Octaves = 6;
		noise.Frequency = 0.05f;
		noise.Lacunarity = 2.03f;
		noise.Gain = 0.5f;
		return noise;
	}

	// Points over a wide range, negative ones and far ones too, to sample at
	void MakePoints(size_t count, std::vector<float>& x, std::vector<float>& y, std::vector<float>& z)
	{
		Sequence sequence;
		x.resize(count);
		y.resize(count);
		z.resize(count);
		for (size_t i = 0; i < count; ++i)
		{
			const float scale = i % 3 == 0 ? 20000.0f : (i % 3 == 1 ? 300.0f : 8.0f);
			x[i] = (sequence.Next() - 0.5f) * scale;
			y[i] = (sequence.Next() - 0.5f) * scale;
			z[i] = (sequence.Next() - 0.5f) * scale;
		}
	}

	// 0 at every lattice corner, within the proven bounds everywhere, as much above 0 as below,
	// another for another seed; prints the largest value and slope measured
	void TestNoiseShape()
	{
		for (int corner = -5; corner <= 5; ++corner)
		{
			Require(Nyx::SampleGradientNoise3D(float(corner), float(corner * 3), float(-corner * 7), 1) == 0.0f,
				"The noise should be 0 at a lattice corner");
		}

		Sequence sequence;
		float largest = 0.0f;
		float steepest = 0.0f;
		double sum = 0.0;
		int differ = 0;
		constexpr int Count = 200000;
		constexpr float Step = 1.0f / 1024.0f;
		for (int i = 0; i < Count; ++i)
		{
			const float x = (sequence.Next() - 0.5f) * 512.0f;
			const float y = (sequence.Next() - 0.5f) * 512.0f;
			const float z = (sequence.Next() - 0.5f) * 512.0f;
			const float value = Nyx::SampleGradientNoise3D(x, y, z, 3);
			largest = std::max(largest, std::fabs(value));
			sum += value;
			const glm::vec3 slope((Nyx::SampleGradientNoise3D(x + Step, y, z, 3) - value) / Step,
				(Nyx::SampleGradientNoise3D(x, y + Step, z, 3) - value) / Step, (Nyx::SampleGradientNoise3D(x, y, z + Step, 3) - value) / Step);
			steepest = std::max(steepest, glm::length(slope));
			if (Nyx::SampleGradientNoise3D(x, y, z, 4) != value)
			{
				++differ;
			}
		}

		Require(largest <= Nyx::GradientNoise3DMaxValue, "The noise should stay within its proven bound");
		Require(largest > 0.5f, "The noise should reach well away from 0");
		Require(steepest <= Nyx::GradientNoise3DMaxSlope, "The noise should be no steeper than its proven bound");
		Require(std::fabs(sum / Count) < 0.01, "The noise should be as much above 0 as below");
		Require(differ > Count * 9 / 10, "Another seed should give another noise");
		std::cout << "3D noise: largest " << largest << " (bound " << Nyx::GradientNoise3DMaxValue << "), steepest " << steepest << " a unit (bound "
				  << Nyx::GradientNoise3DMaxSlope << ")\n";
	}

	// Eight at a time, the same bits as one at a time: noise, fractal and ridged, a count that isn't
	// a multiple of eight
	void TestBatchSameBits()
	{
		std::vector<float> x;
		std::vector<float> y;
		std::vector<float> z;
		constexpr size_t Count = 4099;
		MakePoints(Count, x, y, z);
		const FractalNoise3D fractal = MakeFractal();

		std::vector<float> noise(Count);
		std::vector<float> fbm(Count);
		std::vector<float> ridged(Count);
		Nyx::SampleGradientNoise3DBatch(x.data(), y.data(), z.data(), Count, 11, noise.data());
		Nyx::SampleFractalNoise3DBatch(fractal, x.data(), y.data(), z.data(), Count, fbm.data());
		Nyx::SampleRidgedNoise3DBatch(fractal, x.data(), y.data(), z.data(), Count, ridged.data());

		for (size_t i = 0; i < Count; ++i)
		{
			const glm::vec3 point(x[i], y[i], z[i]);
			Require(SameBits(noise[i], Nyx::SampleGradientNoise3D(x[i], y[i], z[i], 11)),
				"Point " + std::to_string(i) + ": the noise in a batch should be the same bits as alone");
			Require(SameBits(fbm[i], Nyx::SampleFractalNoise3D(fractal, point)),
				"Point " + std::to_string(i) + ": the fractal noise in a batch should be the same bits as alone");
			Require(SameBits(ridged[i], Nyx::SampleRidgedNoise3D(fractal, point)),
				"Point " + std::to_string(i) + ": the ridged noise in a batch should be the same bits as alone");
		}
		std::cout << "Batches: " << (Nyx::IsNoiseBatchSimd() ? "eight points at a time (AVX2)" : "one point at a time") << ", the same bits\n";
	}

	// The values as they were when the noise was written: a hash of the bits of a grid of samples.
	// If this fails, the noise changed, and every world made from it with it: on purpose only.
	void TestGoldenValues()
	{
		const FractalNoise3D fractal = MakeFractal();
		uint64_t noiseHash = 0xCBF29CE484222325ull;
		uint64_t fractalHash = noiseHash;
		uint64_t ridgedHash = noiseHash;
		for (int i = 0; i < 24; ++i)
		{
			for (int j = 0; j < 24; ++j)
			{
				for (int k = 0; k < 24; ++k)
				{
					const glm::vec3 point(float(i) * 3.37f - 40.0f, float(j) * 5.11f - 61.0f, float(k) * 7.93f + 13.0f);
					noiseHash = HashBits(noiseHash, Nyx::SampleGradientNoise3D(point.x * 0.31f, point.y * 0.31f, point.z * 0.31f, 5));
					fractalHash = HashBits(fractalHash, Nyx::SampleFractalNoise3D(fractal, point));
					ridgedHash = HashBits(ridgedHash, Nyx::SampleRidgedNoise3D(fractal, point));
				}
			}
		}

		constexpr uint64_t NoiseGolden = 0x156442d3c2ef910cull;
		constexpr uint64_t FractalGolden = 0x088cdd180b191868ull;
		constexpr uint64_t RidgedGolden = 0x6e7a6c364a0f1109ull;
		std::cout << std::hex << "Golden hashes: noise 0x" << noiseHash << ", fractal 0x" << fractalHash << ", ridged 0x" << ridgedHash << std::dec << "\n";
		Require(noiseHash == NoiseGolden, "The 3D noise's values changed");
		Require(fractalHash == FractalGolden, "The fractal noise's values changed");
		Require(ridgedHash == RidgedGolden, "The ridged noise's values changed");
	}

	// Fractal noise within the bound of one octave, ridged from 0 to 1, both no steeper than their
	// bounds; one octave of fractal noise is the noise at its frequency
	void TestFractalAndRidged()
	{
		const FractalNoise3D fractal = MakeFractal();
		const float fractalSlope = Nyx::GetFractalNoise3DMaxSlope(fractal);
		const float ridgedSlope = Nyx::GetRidgedNoise3DMaxSlope(fractal);
		Sequence sequence;
		float largest = 0.0f;
		float ridgedLowest = 1.0f;
		float ridgedHighest = 0.0f;
		float steepest = 0.0f;
		float ridgedSteepest = 0.0f;
		constexpr float Step = 1.0f / 256.0f;
		for (int i = 0; i < 50000; ++i)
		{
			const glm::vec3 point((sequence.Next() - 0.5f) * 2000.0f, (sequence.Next() - 0.5f) * 2000.0f, (sequence.Next() - 0.5f) * 2000.0f);
			const float value = Nyx::SampleFractalNoise3D(fractal, point);
			const float ridged = Nyx::SampleRidgedNoise3D(fractal, point);
			largest = std::max(largest, std::fabs(value));
			ridgedLowest = std::min(ridgedLowest, ridged);
			ridgedHighest = std::max(ridgedHighest, ridged);
			const glm::vec3 along = glm::normalize(glm::vec3(sequence.Next() - 0.5f, sequence.Next() - 0.5f, sequence.Next() - 0.5f) + 1e-3f);
			steepest = std::max(steepest, std::fabs(Nyx::SampleFractalNoise3D(fractal, point + along * Step) - value) / Step);
			ridgedSteepest = std::max(ridgedSteepest, std::fabs(Nyx::SampleRidgedNoise3D(fractal, point + along * Step) - ridged) / Step);
		}
		Require(largest <= Nyx::GradientNoise3DMaxValue, "Fractal noise should stay within one octave's bound");
		Require(ridgedLowest >= 0.0f && ridgedHighest <= 1.0f, "Ridged noise should stay within 0 to 1");
		Require(ridgedHighest > 0.6f, "Ridged noise should make crests");
		Require(steepest <= fractalSlope, "Fractal noise should be no steeper than its bound");
		Require(ridgedSteepest <= ridgedSlope, "Ridged noise should be no steeper than its bound");
		std::cout << "Fractal noise: largest " << largest << ", steepest " << steepest << " a metre (bound " << fractalSlope << "); ridged "
				  << ridgedLowest << " to " << ridgedHighest << ", steepest " << ridgedSteepest << " (bound " << ridgedSlope << ")\n";

		FractalNoise3D single;
		single.Seed = 9;
		single.Octaves = 1;
		single.Frequency = 0.25f;
		const glm::vec3 point(3.3f, -7.1f, 12.9f);
		Require(SameBits(Nyx::SampleFractalNoise3D(single, point), Nyx::SampleGradientNoise3D(point.x * 0.25f, point.y * 0.25f, point.z * 0.25f, 9)),
			"One octave of fractal noise should be the noise at its frequency");
	}

	// The minimum away from the corner, below it by at most a quarter of the radius near it, the
	// same either way round, and never steeper than its inputs
	void TestSmoothMinimum()
	{
		Require(Nyx::SmoothMinimum(1.0f, 3.0f, 1.5f) == 1.0f, "Values a radius apart should give the minimum");
		Require(Nyx::SmoothMinimum(1.0f, 3.0f, 0.0f) == 1.0f, "A radius of 0 should give the minimum");
		Require(Nyx::SmoothMinimum(2.0f, 2.0f, 1.0f) == 2.0f - 0.25f, "Equal values should be a quarter of the radius below");
		Require(Nyx::SmoothMaximum(2.0f, 2.0f, 1.0f) == 2.0f + 0.25f, "The smooth maximum should be above by as much");
		Require(Nyx::SmoothMinimumMaxOffset(2.0f) == 0.5f, "The offset should be a quarter of the radius");

		Sequence sequence;
		for (int i = 0; i < 100000; ++i)
		{
			const float a = (sequence.Next() - 0.5f) * 10.0f;
			const float b = (sequence.Next() - 0.5f) * 10.0f;
			const float radius = sequence.Next() * 4.0f;
			const float blended = Nyx::SmoothMinimum(a, b, radius);
			Require(blended <= std::min(a, b) && blended >= std::min(a, b) - Nyx::SmoothMinimumMaxOffset(radius) * 1.0001f,
				"The smooth minimum should be at most a quarter of its radius below the minimum");
			Require(SameBits(blended, Nyx::SmoothMinimum(b, a, radius)), "The smooth minimum shouldn't depend on the order");

			const float da = (sequence.Next() - 0.5f) * 0.01f;
			const float db = (sequence.Next() - 0.5f) * 0.01f;
			const float change = std::fabs(Nyx::SmoothMinimum(a + da, b + db, radius) - blended);
			Require(change <= std::max(std::fabs(da), std::fabs(db)) * 1.001f + 1e-6f, "The smooth minimum should change no faster than its inputs");
		}
	}

	// Not a test: how long a sample takes, one at a time and in batches
	void PrintTimings()
	{
		std::vector<float> x;
		std::vector<float> y;
		std::vector<float> z;
		constexpr size_t Count = 35 * 35 * 35;
		MakePoints(Count, x, y, z);
		std::vector<float> out(Count);
		const FractalNoise3D fractal = MakeFractal();

		auto time = [](auto&& work)
		{
			const auto start = std::chrono::steady_clock::now();
			work();
			return std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - start).count() / double(Count);
		};

		float sink = 0.0f;
		const double single = time([&] {
			for (size_t i = 0; i < Count; ++i)
			{
				out[i] = Nyx::SampleGradientNoise3D(x[i], y[i], z[i], 1);
			}
		});
		sink += out[Count / 2];
		const double batch = time([&] { Nyx::SampleGradientNoise3DBatch(x.data(), y.data(), z.data(), Count, 1, out.data()); });
		sink += out[Count / 3];
		const double fractalSingle = time([&] {
			for (size_t i = 0; i < Count; ++i)
			{
				out[i] = Nyx::SampleFractalNoise3D(fractal, glm::vec3(x[i], y[i], z[i]));
			}
		});
		sink += out[Count / 4];
		const double fractalBatch = time([&] { Nyx::SampleFractalNoise3DBatch(fractal, x.data(), y.data(), z.data(), Count, out.data()); });
		sink += out[Count / 5];
		std::cout << "A chunk's 42875 samples: noise " << single << " ns a sample alone, " << batch << " in batches; 6 octaves " << fractalSingle
				  << " ns alone, " << fractalBatch << " in batches (" << sink * 0.0f << ")\n";
	}
}

int main()
{
	try
	{
		TestNoiseShape();
		TestBatchSameBits();
		TestGoldenValues();
		TestFractalAndRidged();
		TestSmoothMinimum();
		PrintTimings();
	}
	catch (const std::exception& error)
	{
		std::cerr << "Gradient noise test failed: " << error.what() << "\n";
		return 1;
	}

	std::cout << "All gradient noise tests passed.\n";
	return 0;
}
