#pragma once

#include <windows.h>
#include <gdiplus.h>
#include <memory>
#include <vector>

namespace Nyx
{
	// Separate choreography for the desktop cracking and collapsing into darkness.
	class StartupBannerFracture
	{
	public:
		void Prepare(int width, int height, Gdiplus::Bitmap& banner, Gdiplus::Bitmap* desktop);
		Gdiplus::Bitmap* Render(float elapsedSeconds, Gdiplus::Bitmap& banner);

	private:
		struct Shard
		{
			std::unique_ptr<Gdiplus::Bitmap> Image;
			std::vector<Gdiplus::PointF> Outline;
			Gdiplus::PointF Center;
			Gdiplus::PointF Origin;
			float BreakTime = 0.0f;
			float Spin = 0.0f;
		};

		struct Crack
		{
			Gdiplus::PointF Start;
			Gdiplus::PointF End;
			float BirthTime = 0.0f;
			float BreakTime = 0.0f;
		};

		void PrepareShards(Gdiplus::Bitmap& source);
		void DrawShard(Gdiplus::Graphics& canvas, const Shard& shard, float time);
		void DrawCracks(Gdiplus::Graphics& canvas, float time);
		void DrawDebris(Gdiplus::Graphics& canvas, float time);

		float Scale = 1.0f;
		float SurfaceHeight = 720.0f;
		std::unique_ptr<Gdiplus::Bitmap> Frame;
		std::vector<Shard> Shards;
		std::vector<Crack> Cracks;
	};
}
