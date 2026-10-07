#pragma once

#include "SoftwareIntro.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace Nyx
{
	// Software renderer for the "rift" startup intro: the desktop sinks into a
	// storm-lit sky, energy gathers, cracks race across it, light runs through the
	// fracture network and the surface shatters into a molten breach before the
	// banner emerges.
	//
	// Deliberately free of Windows and GDI+ types. GDI+ cannot blend additively, and
	// additive light is what makes glowing seams, bloom and fire read as light rather
	// than paint. Working on raw premultiplied pixels also keeps the effect testable
	// and deterministic on any platform. Built optimized in every configuration (see
	// this folder's CMakeLists.txt), so hot loops use raw pointers, not checked iterators.
	class StartupBannerRiftRenderer final : public ISoftwareIntro
	{
	public:
		StartupBannerRiftRenderer();
		~StartupBannerRiftRenderer() override;
		StartupBannerRiftRenderer(const StartupBannerRiftRenderer&) = delete;
		StartupBannerRiftRenderer& operator=(const StartupBannerRiftRenderer&) = delete;

		// Logical canvas, shared with StartupBannerIntro.
		static constexpr float Width = 1040.0f;
		static constexpr float Height = 720.0f;
		static constexpr float DurationSeconds = 3.4f;
		static constexpr float IgnitionSeconds = 0.62f;
		static constexpr float ImpactSeconds = 1.84f;
		static constexpr float FocusX = 520.0f;
		static constexpr float FocusY = 200.0f;

		// Without a captured desktop, a storm sky is synthesized.
		void Prepare(const SoftwareIntroInputs& inputs) override;
		const uint32_t* Render(float elapsedSeconds) override;
		int GetWidth() const override { return PixelWidth; }
		int GetHeight() const override { return PixelHeight; }
		void Release() override;

		struct Vec2
		{
			float X = 0.0f;
			float Y = 0.0f;
		};

		struct Color
		{
			float R = 0.0f;
			float G = 0.0f;
			float B = 0.0f;
		};

	private:
		static constexpr uint16_t NoCell = 0xffff;

		struct Cell
		{
			std::vector<Vec2> Outline;
			Vec2 Centroid;
			bool bBreakable = false;
			float BreakTime = 1.0e9f;
			float Spin = 0.0f;
			float TiltAngle = 0.0f;
			float TiltRate = 0.0f;
			float Launch = 0.0f;
			float Approach = 0.0f;
		};

		struct Edge
		{
			Vec2 Near;
			Vec2 Far;
			int First = -1;
			int Second = -1;
			float BirthTime = 0.0f;
			float Distance = 0.0f;
			bool bHidden = false; // Some chords never show; real webs are irregular.
		};

		struct Crack
		{
			std::vector<Vec2> Points;
			std::vector<float> Lengths;
			float StartTime = 0.0f;
			float Width = 1.0f;
			float Intensity = 1.0f;
			float Phase = 0.0f;
		};

		struct Tendril
		{
			Vec2 Start;
			Vec2 Control;
			float Phase = 0.0f;
			float Width = 1.0f;
		};

		void PrepareSky(const uint32_t* desktop, int desktopStride);
		void PrepareWeb();
		void PrepareCellMap();
		void PrepareCracks();
		void PrepareRift();

		void DrawBase(float time);
		void DrawShards(float time);
		void DrawWeb(float time);
		void DrawCracks(float time);
		void DrawTendrils(float time);
		void DrawEmbers(float time);
		void DrawLightSources(float time);
		void ApplyBloom(float time);
		void DrawBanner(float opacity);

		bool IsOpen(int cell, float time) const;

		// Splits rows across the worker pool; job(firstRow, endRow).
		void ForRows(int rows, const std::function<void(int, int)>& job);

		// Logical-space drawing helpers; sky-attached geometry follows the shake.
		// A crack is a dark seam with a glowing core, rasterized in a single pass.
		void StrokeCrack(Vec2 a, Vec2 b, float seamWidth, float darkness, float coreWidth, Color core);
		void AddLine(Vec2 a, Vec2 b, float width, Color color);
		void EmitLine(Vec2 a, Vec2 b, float width, Color color);
		void EmitBlob(Vec2 center, float radius, Color color);
		void EmitRing(Vec2 center, float radius, float squash, float width, Color color);

	private:
		int PixelWidth = 0;
		int PixelHeight = 0;
		float Scale = 1.0f;

		std::vector<uint32_t> Desktop;
		std::vector<uint32_t> Sky;
		std::vector<uint32_t> Rift;
		std::vector<uint32_t> Frame;
		std::vector<uint16_t> CellMap;
		std::vector<float> FeatherColumns;
		std::vector<float> FeatherRows;
		std::vector<int> CellGains;           // 8.8 fixed point per cell, 0 while intact.
		std::vector<int> RowSpans;            // Per row: first and last pixel that can open.
		std::vector<uint8_t> ToneCurve;
		bool bHasDesktop = false;

		std::vector<Cell> Cells;
		std::vector<Edge> Edges;
		std::vector<Crack> Cracks;
		std::vector<Tendril> Tendrils;

		std::vector<uint32_t> Banner;
		int BannerWidth = 0;
		int BannerHeight = 0;
		int BannerLeft = 0;
		int BannerTop = 0;

		// Two-level bloom: quarter resolution for tight glow, eighth for wide halos.
		int EmissionWidth = 0;
		int EmissionHeight = 0;
		int HaloWidth = 0;
		int HaloHeight = 0;
		std::vector<float> Emission;
		std::vector<float> Halo;
		std::vector<float> BlurScratch;
		std::vector<float> UpsampledRows;
		std::vector<int> UpsampleColumns;
		std::vector<float> UpsampleWeights;

		class Workers;
		std::unique_ptr<Workers> Pool;

		// Per-frame state.
		float ShakeX = 0.0f;
		float ShakeY = 0.0f;
		float Fade = 1.0f;
	};
}
