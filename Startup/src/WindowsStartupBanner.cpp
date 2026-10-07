#include "WindowsStartupBanner.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>
#include "StartupBannerIntro.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef NYX_STARTUP_PREVIEW
#include <iostream>
#include <fstream>
#endif

namespace
{
	// Intros that start from a snapshot of the desktop behind the banner window.
	bool UsesDesktopCapture(Nyx::EStartupBannerMode mode)
	{
		return mode == Nyx::EStartupBannerMode::RealityCut || mode == Nyx::EStartupBannerMode::Chasm ||
			mode == Nyx::EStartupBannerMode::Rift || mode == Nyx::EStartupBannerMode::Assemble ||
			mode == Nyx::EStartupBannerMode::AssembleV2;
	}

	// Intros played entirely within the banner's own rectangle; the others use a
	// larger canvas with the banner inside it.
	bool IsBannerSizedIntro(Nyx::EStartupBannerMode mode)
	{
		return mode == Nyx::EStartupBannerMode::AssembleV2;
	}

	constexpr float BannerWidth = 640.0f;
	constexpr float BannerHeight = 360.0f;
	constexpr float ArtworkHeight = 220.0f;
	constexpr float TileWidth = 196.0f;
	constexpr float TileHeight = 106.0f;
	constexpr float TileColumnSpacing = 202.0f;
	constexpr float TileRowSpacing = 112.0f;
	constexpr float ArtworkScrollSpeed = 13.0f;
	constexpr size_t MaxArtworkImages = 8;
	constexpr float FadeInSeconds = 0.24f;
	constexpr float FadeOutSeconds = 0.18f;
	constexpr int TargetFramesPerSecond = 60;
	constexpr auto FramePeriod = std::chrono::nanoseconds(1'000'000'000 / TargetFramesPerSecond);
	constexpr const wchar_t* BannerWindowClassName = L"NyxStartupBanner";

	// Size of the intro surface, in physical pixels, for a banner of the given size.
	Gdiplus::Size IntroCanvasSize(Nyx::EStartupBannerMode mode, int bannerWidth, int bannerHeight)
	{
		if (IsBannerSizedIntro(mode))
		{
			return Gdiplus::Size(bannerWidth, bannerHeight);
		}
		return Gdiplus::Size(static_cast<int>(Nyx::StartupBannerIntro::Width * bannerWidth / BannerWidth),
			static_cast<int>(Nyx::StartupBannerIntro::Height * bannerHeight / BannerHeight));
	}

#ifdef NYX_STARTUP_PREVIEW
	// Diagnostics never capture the user's screen. A patterned demo workspace makes
	// offsets legible and gives exports repeatable, non-private source imagery.
	std::unique_ptr<Gdiplus::Bitmap> MakeDemoDesktop(int width, int height)
	{
		auto image = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
		Gdiplus::Graphics canvas(image.get());
		canvas.ScaleTransform(width / Nyx::StartupBannerIntro::Width, height / Nyx::StartupBannerIntro::Height);
		canvas.Clear(Gdiplus::Color(255, 25, 35, 52));
		Gdiplus::Pen grid(Gdiplus::Color(255, 38, 51, 72));
		for (int x = 0; x < 1040; x += 40)
		{
			canvas.DrawLine(&grid, x, 0, x, 720);
		}
		for (int y = 0; y < 720; y += 40)
		{
			canvas.DrawLine(&grid, 0, y, 1040, y);
		}
		Gdiplus::SolidBrush panel(Gdiplus::Color(255, 53, 64, 83));
		Gdiplus::SolidBrush sidebar(Gdiplus::Color(255, 38, 46, 62));
		Gdiplus::SolidBrush ink(Gdiplus::Color(255, 179, 193, 216));
		canvas.FillRectangle(&panel, 115, 120, 810, 480);
		canvas.FillRectangle(&sidebar, 115, 120, 170, 480);
		Gdiplus::Font font(L"Segoe UI", 19, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
		canvas.DrawString(L"DEMO WORKSPACE", -1, &font, Gdiplus::PointF(320, 150), &ink);
		for (int index = 0; index < 7; ++index)
		{
			canvas.FillRectangle(&ink, 140, 178 + index * 46, 110, 3);
			canvas.FillRectangle(&ink, 320, 238 + index * 42, 180 + (index * 83 % 350), 5);
		}
		return image;
	}
#endif

	// UpdateLayeredWindow reads premultiplied pixels from a Windows DIB. Keep the
	// DIB and its memory DC alive across frames instead of allocating per frame.
	class LayeredFramePresenter
	{
	public:
		~LayeredFramePresenter()
		{
			if (PreviousBitmap)
			{
				SelectObject(MemoryDC, PreviousBitmap);
			}
			if (Bitmap)
			{
				DeleteObject(Bitmap);
			}
			if (MemoryDC)
			{
				DeleteDC(MemoryDC);
			}
		}

		LayeredFramePresenter() = default;
		LayeredFramePresenter(const LayeredFramePresenter&) = delete;
		LayeredFramePresenter& operator=(const LayeredFramePresenter&) = delete;

		void Initialize(int width, int height)
		{
			Size = { width, height };
			MemoryDC = CreateCompatibleDC(nullptr);
			BITMAPINFO info{};
			info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
			info.bmiHeader.biWidth = width;
			info.bmiHeader.biHeight = -height; // Top-down, matching the rendered bitmap.
			info.bmiHeader.biPlanes = 1;
			info.bmiHeader.biBitCount = 32;
			info.bmiHeader.biCompression = BI_RGB;
			Bitmap = CreateDIBSection(MemoryDC, &info, DIB_RGB_COLORS, &Pixels, nullptr, 0);
			if (!MemoryDC || !Bitmap || !Pixels)
			{
				throw std::runtime_error("Could not create the transparent startup surface");
			}
			PreviousBitmap = SelectObject(MemoryDC, Bitmap);
		}

		void Present(HWND window, Gdiplus::Bitmap& frame, BYTE opacity)
		{
			Gdiplus::Rect bounds(0, 0, Size.cx, Size.cy);
			Gdiplus::BitmapData data{};
			if (frame.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &data) != Gdiplus::Ok)
			{
				throw std::runtime_error("Could not read the transparent startup frame");
			}
			const size_t rowBytes = static_cast<size_t>(Size.cx) * 4;
			for (int row = 0; row < Size.cy; ++row)
			{
				std::memcpy(static_cast<BYTE*>(Pixels) + row * rowBytes,
					static_cast<BYTE*>(data.Scan0) + row * data.Stride, rowBytes);
			}
			frame.UnlockBits(&data);

			POINT source{};
			BLENDFUNCTION blend{ AC_SRC_OVER, 0, opacity, AC_SRC_ALPHA };
			if (!UpdateLayeredWindow(window, nullptr, nullptr, &Size, MemoryDC, &source, 0, &blend, ULW_ALPHA))
			{
				throw std::runtime_error("Could not present the transparent startup frame");
			}
		}

		std::unique_ptr<Gdiplus::Bitmap> CaptureDesktop(int x, int y)
		{
			// Capture only the intro's rectangle, before its window becomes visible.
			// The clone owns its pixels independently of the presentation DIB.
			HDC screen = GetDC(nullptr);
			if (!screen)
			{
				return nullptr;
			}
			const BOOL bCopied = BitBlt(MemoryDC, 0, 0, Size.cx, Size.cy, screen, x, y, SRCCOPY | CAPTUREBLT);
			GdiFlush();
			ReleaseDC(nullptr, screen);
			if (!bCopied)
			{
				return nullptr;
			}
			// GDI copies RGB but does not supply alpha. Make the captured image opaque.
			auto* pixels = static_cast<BYTE*>(Pixels);
			for (size_t index = 0; index < static_cast<size_t>(Size.cx) * Size.cy; ++index)
			{
				pixels[index * 4 + 3] = 255;
			}
			Gdiplus::Bitmap captured(Size.cx, Size.cy, Size.cx * 4, PixelFormat32bppPARGB, pixels);
			return std::unique_ptr<Gdiplus::Bitmap>(captured.Clone(
				Gdiplus::Rect(0, 0, Size.cx, Size.cy), PixelFormat32bppPARGB));
		}

	private:
		HDC MemoryDC = nullptr;
		HBITMAP Bitmap = nullptr;
		HGDIOBJ PreviousBitmap = nullptr;
		void* Pixels = nullptr;
		SIZE Size{};
	};

	// Owns the Windows timer used to sleep until the next frame or window message.
	class FrameTimer
	{
	public:
		FrameTimer()
		{
			Handle = CreateWaitableTimerExW(
				nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);

			if (!Handle)
			{
				Handle = CreateWaitableTimerW(nullptr, FALSE, nullptr);
			}
		}

		~FrameTimer()
		{
			if (Handle)
			{
				CloseHandle(Handle);
			}
		}

		FrameTimer(const FrameTimer&) = delete;
		FrameTimer& operator=(const FrameTimer&) = delete;

		void WaitUntil(std::chrono::steady_clock::time_point deadline)
		{
			const auto remaining = deadline - std::chrono::steady_clock::now();
			if (remaining <= std::chrono::steady_clock::duration::zero())
			{
				return;
			}

			// Windows uses negative 100-nanosecond units for relative timer deadlines.
			const auto remainingNanoseconds =
				std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
			LARGE_INTEGER dueTime{};
			dueTime.QuadPart = -(std::max)(1LL, remainingNanoseconds / 100);

			if (Handle && SetWaitableTimer(Handle, &dueTime, 0, nullptr, nullptr, FALSE))
			{
				MsgWaitForMultipleObjectsEx(1, &Handle, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			}
			else
			{
				const auto remainingMilliseconds =
					std::chrono::duration_cast<std::chrono::milliseconds>(remaining).count();
				MsgWaitForMultipleObjectsEx(0, nullptr, static_cast<DWORD>(remainingMilliseconds + 1),
					QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			}
		}

	private:
		HANDLE Handle = nullptr;
	};
}

namespace Nyx
{
	struct WindowsStartupBanner::Impl
	{
		std::mutex StatusMutex;
		std::wstring StatusText = L"Preparing your workspace";
		EStartupBannerMode Mode = EStartupBannerMode::Classic;
		// Destroyed first: jthread requests stop and joins before the shared status dies.
		std::jthread AnimationThread;

		std::wstring GetStatusText()
		{
			std::lock_guard lock(StatusMutex);
			return StatusText;
		}

		struct Surface
		{
			// Drawing resources are created and used only on the animation thread.
			Impl* Owner;
			std::vector<std::unique_ptr<Gdiplus::Bitmap>> Images;
			std::vector<std::unique_ptr<Gdiplus::Bitmap>> Tiles;
			std::unique_ptr<Gdiplus::Bitmap> FrameBuffer;
			Gdiplus::FontFamily FontFamily{ L"Segoe UI" };
			Gdiplus::Font TitleFont{ &FontFamily, 66, Gdiplus::FontStyleBold, Gdiplus::UnitPixel };
			Gdiplus::Font LabelFont{ &FontFamily, 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel };
			Gdiplus::Font CaptionFont{ &FontFamily, 11, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel };
			std::chrono::steady_clock::time_point StartTime = std::chrono::steady_clock::now();
			bool bHidden = false;
			StartupBannerIntro Intro;
			LayeredFramePresenter Presenter;

			bool IsCinematic() const
			{
				return Owner->Mode == EStartupBannerMode::Lightning || Owner->Mode == EStartupBannerMode::RealityCut ||
					Owner->Mode == EStartupBannerMode::Chasm || Owner->Mode == EStartupBannerMode::Rift ||
					Owner->Mode == EStartupBannerMode::Assemble ||
					Owner->Mode == EStartupBannerMode::AssembleV2;
			}

			void PrepareIntro(int bannerWidth, int bannerHeight, Gdiplus::Bitmap* desktop = nullptr,
				int canvasWidth = 0, int canvasHeight = 0)
			{
				Gdiplus::Bitmap* banner = Render(bannerWidth, bannerHeight, 0.0f);
				const Gdiplus::Size canvas = IntroCanvasSize(Owner->Mode, bannerWidth, bannerHeight);
				const int width = canvasWidth > 0 ? canvasWidth : canvas.Width;
				const int height = canvasHeight > 0 ? canvasHeight : canvas.Height;
				Intro.Prepare(width, height, *banner, Owner->Mode, desktop);
			}

			Gdiplus::Bitmap* RenderIntroFrame(float elapsedSeconds)
			{
				// Most intros use a frozen banner, and live scrolling starts after the reveal.
				// Intros that build the banner while it animates keep it live throughout.
				if (Intro.UsesLiveBanner())
				{
					Render(FrameBuffer->GetWidth(), FrameBuffer->GetHeight(), elapsedSeconds);
				}
				else if (elapsedSeconds >= StartupBannerIntro::DurationSeconds)
				{
					Render(FrameBuffer->GetWidth(), FrameBuffer->GetHeight(),
						elapsedSeconds - StartupBannerIntro::DurationSeconds);
				}
				return Intro.Render(elapsedSeconds, *FrameBuffer);
			}

			void LoadArtwork(const std::filesystem::path& directory, std::stop_token stop = {})
			{
				std::vector<std::filesystem::path> paths;
				std::error_code error;
				for (std::filesystem::directory_iterator it(directory, error), end;
					!error && it != end; it.increment(error))
				{
					if (!it->is_regular_file(error))
					{
						continue;
					}

					auto extension = it->path().extension().wstring();
					std::transform(extension.begin(), extension.end(), extension.begin(), ::towlower);
					if (extension == L".png" || extension == L".jpg" || extension == L".jpeg")
					{
						paths.push_back(it->path());
					}
				}
				std::sort(paths.begin(), paths.end());

				if (paths.empty())
				{
					const auto skyboxDirectory = directory.parent_path() / "Textures" / "Skybox" / "Skybox01";
					paths = {
						skyboxDirectory / "front.png",
						skyboxDirectory / "right.png",
						skyboxDirectory / "left.png"
					};
				}
				for (const auto& path : paths)
				{
					if (stop.stop_requested() || Images.size() == MaxArtworkImages)
					{
						break;
					}

					auto bitmap = std::make_unique<Gdiplus::Bitmap>(path.c_str());
					if (bitmap->GetLastStatus() == Gdiplus::Ok && bitmap->GetWidth() && bitmap->GetHeight())
					{
						Images.push_back(std::move(bitmap));
					}
				}
			}

			float GetElapsedSeconds() const
			{
				return std::chrono::duration<float>(std::chrono::steady_clock::now() - StartTime).count();
			}

			void PrepareBuffers(int width, int height)
			{
				if (FrameBuffer && FrameBuffer->GetWidth() == width && FrameBuffer->GetHeight() == height)
				{
					return;
				}

				FrameBuffer = std::make_unique<Gdiplus::Bitmap>(width, height, PixelFormat32bppPARGB);
				Tiles.clear();

				const int tileWidth = static_cast<int>(std::ceil(TileWidth * width / BannerWidth));
				const int tileHeight = static_cast<int>(std::ceil(TileHeight * height / BannerHeight));
				for (const auto& image : Images)
				{
					auto tile = std::make_unique<Gdiplus::Bitmap>(tileWidth, tileHeight, PixelFormat32bppPARGB);
					Gdiplus::Graphics graphics(tile.get());
					graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
					graphics.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);
					const float imageWidth = static_cast<float>(image->GetWidth());
					const float imageHeight = static_cast<float>(image->GetHeight());
					const float scale = (std::max)(tileWidth / imageWidth, tileHeight / imageHeight);
					const float sourceWidth = tileWidth / scale;
					const float sourceHeight = tileHeight / scale;

					// Decode, crop and downsample only once per display size.
					graphics.DrawImage(image.get(), Gdiplus::RectF(0, 0,
						static_cast<float>(tileWidth), static_cast<float>(tileHeight)),
						(imageWidth - sourceWidth) * 0.5f, (imageHeight - sourceHeight) * 0.5f,
						sourceWidth, sourceHeight, Gdiplus::UnitPixel);
					Tiles.push_back(std::move(tile));
				}
			}

			Gdiplus::Bitmap* Render(int width, int height, float elapsed)
			{
				PrepareBuffers(width, height);
				if (width > 0 && height > 0)
				{
					Gdiplus::Graphics canvas(FrameBuffer.get());
					canvas.ScaleTransform(width / BannerWidth, height / BannerHeight);
					canvas.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
					canvas.SetInterpolationMode(Gdiplus::InterpolationModeBilinear);
					canvas.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAliasGridFit);
					canvas.Clear(Gdiplus::Color(255, 23, 25, 30));

					DrawArtwork(canvas, width, height, elapsed);
					DrawLabelsAndActivity(canvas, elapsed);
				}

				return FrameBuffer.get();
			}

			void DrawArtwork(Gdiplus::Graphics& canvas, int width, int height, float elapsed)
			{
				canvas.SetClip(Gdiplus::RectF(0, 0, BannerWidth, ArtworkHeight));

				// Alternate drifting rows; each image is cropped to fill its tile.
				for (int row = 0; row < 2; ++row)
				{
					const float distance = elapsed * ArtworkScrollSpeed;
					const float drift = std::fmod(distance, TileColumnSpacing);
					const int shift = static_cast<int>(distance / TileColumnSpacing);
					for (int column = -2; column < 5; ++column)
					{
						const float x = column * TileColumnSpacing + (row == 0 ? drift : -drift) + row * 70.0f;
						const Gdiplus::RectF tile(x, row * TileRowSpacing - 4, TileWidth, TileHeight);
						if (x + tile.Width <= 0 || x >= BannerWidth)
						{
							continue;
						}

						if (!Tiles.empty())
						{
							const int count = static_cast<int>(Tiles.size());
							const int index = column + 2 + row * 3 + (row == 0 ? -shift : shift);

							// The first row moves backwards through the image list.
							// Normalize negative indices so wrapping stays seamless.
							const int wrappedIndex = (index % count + count) % count;
							auto& image = *Tiles[wrappedIndex];

							// Tiles already have their final physical size. Copy
							// them at pixel-aligned positions instead of invoking
							// GDI+'s transformed image resampler every frame.
							const auto state = canvas.Save();
							canvas.ResetTransform();
							canvas.DrawImage(&image,
								static_cast<int>(std::round(x * width / BannerWidth)),
								static_cast<int>(std::round(tile.Y * height / BannerHeight)),
								static_cast<int>(image.GetWidth()), static_cast<int>(image.GetHeight()));
							canvas.Restore(state);
						}
						else
						{
							Gdiplus::LinearGradientBrush fallback(tile,
								Gdiplus::Color(255, 52, 48, 69), Gdiplus::Color(255, 30, 37, 46), 35.0f);
							canvas.FillRectangle(&fallback, tile);
						}
					}
				}
				Gdiplus::SolidBrush dim(Gdiplus::Color(105, 23, 25, 30));
				canvas.FillRectangle(&dim, 0, 0, 640, 220);
				Gdiplus::LinearGradientBrush shade(Gdiplus::Point(0, 60), Gdiplus::Point(0, 220),
					Gdiplus::Color(0, 23, 25, 30), Gdiplus::Color(255, 23, 25, 30));
				canvas.FillRectangle(&shade, 0, 60, 640, 160);
				canvas.ResetClip();
			}

			void DrawLabelsAndActivity(Gdiplus::Graphics& canvas, float elapsed)
			{
				Gdiplus::SolidBrush ivory(Gdiplus::Color(255, 235, 233, 241));
				Gdiplus::SolidBrush muted(Gdiplus::Color(255, 163, 163, 176));
				Gdiplus::SolidBrush lavender(Gdiplus::Color(255, 185, 164, 239));
				Gdiplus::StringFormat centered;
				centered.SetAlignment(Gdiplus::StringAlignmentCenter);

				canvas.DrawString(L"nyx", -1, &TitleFont, Gdiplus::RectF(0, 112, 640, 88), &centered, &ivory);
				canvas.DrawString(L"G A M E   E N G I N E", -1, &CaptionFont,
					Gdiplus::RectF(0, 210, 640, 24), &centered, &muted);
				const std::wstring status = Owner->GetStatusText();
				canvas.DrawString(status.c_str(), -1, &LabelFont,
					Gdiplus::RectF(32, 264, 576, 26), &centered, &ivory);

				// Indeterminate activity, deliberately not a fabricated percentage.
				Gdiplus::SolidBrush track(Gdiplus::Color(255, 50, 49, 60));
				canvas.FillRectangle(&track, 260, 306, 120, 2);
				const float activity = (std::sin(elapsed * 2.4f) + 1.0f) * 0.5f;
				canvas.FillRectangle(&lavender, 260.0f + activity * 92.0f, 306.0f, 28.0f, 2.0f);

				canvas.DrawString(L"Esc to hide", -1, &CaptionFont,
					Gdiplus::RectF(0, 332, 640, 20), &centered, &muted);
				Gdiplus::Pen border(Gdiplus::Color(255, 65, 62, 77));
				canvas.DrawRectangle(&border, 0, 0, 639, 359);
			}

			void Paint(HWND window)
			{
				PAINTSTRUCT paint{};
				HDC deviceContext = BeginPaint(window, &paint);
				RECT clientArea{};
				GetClientRect(window, &clientArea);

				if (!IsCinematic() && clientArea.right > 0 && clientArea.bottom > 0)
				{
					Gdiplus::Bitmap* buffer = Render(clientArea.right, clientArea.bottom, GetElapsedSeconds());
					Gdiplus::Graphics output(deviceContext);
					output.DrawImage(buffer, 0, 0, clientArea.right, clientArea.bottom);
				}

				EndPaint(window, &paint);
			}
		};

		static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
		{
			auto* surface = reinterpret_cast<Surface*>(GetWindowLongPtrW(window, GWLP_USERDATA));
			if (message == WM_NCCREATE)
			{
				surface = static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
				SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(surface));
			}
			if (surface)
			{
				if (message == WM_PAINT)
				{
					surface->Paint(window);
					return 0;
				}

				if (message == WM_ERASEBKGND)
				{
					return 1;
				}

				if (message == WM_CLOSE || (message == WM_KEYDOWN && wParam == VK_ESCAPE))
				{
					surface->bHidden = true;
					ShowWindow(window, SW_HIDE);
					return 0;
				}
				if (message == WM_NCHITTEST)
				{
					// Captured desktop fragments must remain aligned with their source.
					return surface->IsCinematic() && surface->GetElapsedSeconds() < StartupBannerIntro::DurationSeconds ?
						HTCLIENT : HTCAPTION;
				}
			}
			return DefWindowProcW(window, message, wParam, lParam);
		}

		HWND CreateBannerWindow(HINSTANCE instance, Surface& surface)
		{
			POINT cursorPosition{};
			GetCursorPos(&cursorPosition);

			MONITORINFO monitorInfo{ sizeof(MONITORINFO) };
			const HMONITOR monitor = MonitorFromPoint(cursorPosition, MONITOR_DEFAULTTOPRIMARY);
			GetMonitorInfoW(monitor, &monitorInfo);

			const RECT& workArea = monitorInfo.rcWork;
			const float requestedScale = GetDpiForSystem() / 96.0f;
			const float dpiScale = surface.IsCinematic() ? std::min({ requestedScale,
				(workArea.right - workArea.left) / StartupBannerIntro::Width,
				(workArea.bottom - workArea.top) / StartupBannerIntro::Height }) : requestedScale;
			const int bannerWidth = static_cast<int>(BannerWidth * dpiScale);
			const int bannerHeight = static_cast<int>(BannerHeight * dpiScale);
			const Gdiplus::Size canvas = IntroCanvasSize(Mode, bannerWidth, bannerHeight);
			const int width = Mode == EStartupBannerMode::Chasm ? workArea.right - workArea.left : (surface.IsCinematic() ?
				canvas.Width : bannerWidth);
			const int height = Mode == EStartupBannerMode::Chasm ? workArea.bottom - workArea.top : (surface.IsCinematic() ?
				canvas.Height : bannerHeight);
			const int x = workArea.left + (workArea.right - workArea.left - width) / 2;
			const int y = workArea.top + (workArea.bottom - workArea.top - height) / 2;
			surface.PrepareBuffers(bannerWidth, bannerHeight);
			if (surface.IsCinematic())
			{
				surface.Presenter.Initialize(width, height);
				auto desktop = UsesDesktopCapture(Mode) ? surface.Presenter.CaptureDesktop(x, y) : nullptr;
				surface.PrepareIntro(bannerWidth, bannerHeight, desktop.get(), width, height);
			}

			return CreateWindowExW(
				WS_EX_TOOLWINDOW | WS_EX_LAYERED,
				BannerWindowClassName,
				L"Nyx - Starting",
				WS_POPUP,
				x, y, width, height,
				nullptr, nullptr, instance, &surface);
		}

		void AnimateUntilStopped(HWND window, Surface& surface, std::stop_token stop)
		{
			surface.StartTime = std::chrono::steady_clock::now();
			if (surface.IsCinematic())
			{
				// Do not mix SetLayeredWindowAttributes and UpdateLayeredWindow on
				// the same window: cinematic mode uses per-pixel alpha throughout.
				surface.Presenter.Present(window, *surface.RenderIntroFrame(0.0f), 0);
			}
			else
			{
				SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
			}
			ShowWindow(window, SW_SHOWNORMAL);

			FrameTimer frameTimer;
			auto nextFrameTime = std::chrono::steady_clock::now();
			float fadeOutStartSeconds = -1.0f;
			BYTE previousOpacity = 0;

			while (true)
			{
				MSG message{};
				while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&message);
					DispatchMessageW(&message);
				}

				const float elapsedSeconds = surface.GetElapsedSeconds();
				const bool bIntroFinished = !surface.IsCinematic() ||
					elapsedSeconds >= StartupBannerIntro::DurationSeconds;
				if (stop.stop_requested() && fadeOutStartSeconds < 0.0f && (bIntroFinished || surface.bHidden))
				{
					fadeOutStartSeconds = elapsedSeconds;
				}

				const bool bFadingOut = fadeOutStartSeconds >= 0.0f;
				if (bFadingOut && (surface.bHidden || elapsedSeconds - fadeOutStartSeconds >= FadeOutSeconds))
				{
					break;
				}

				float alpha = (std::min)(elapsedSeconds / FadeInSeconds, 1.0f);
				if (bFadingOut)
				{
					alpha *= 1.0f - (elapsedSeconds - fadeOutStartSeconds) / FadeOutSeconds;
				}

				const auto now = std::chrono::steady_clock::now();
				if (now >= nextFrameTime)
				{
					// Include drawing time in the frame budget, rather than waiting
					// for a full frame period after drawing has already finished.
					nextFrameTime = now + FramePeriod;
					if (!surface.bHidden)
					{
						const BYTE opacity = static_cast<BYTE>(255 * alpha);
						if (surface.IsCinematic())
						{
							surface.Presenter.Present(window, *surface.RenderIntroFrame(elapsedSeconds), opacity);
						}
						else
						{
							if (opacity != previousOpacity)
							{
								SetLayeredWindowAttributes(window, 0, opacity, LWA_ALPHA);
								previousOpacity = opacity;
							}
							InvalidateRect(window, nullptr, FALSE);
							UpdateWindow(window);
						}
					}
				}

				frameTimer.WaitUntil(nextFrameTime);
			}
		}

		void Run(std::stop_token stop, const std::filesystem::path& artworkDirectory)
		{
			Gdiplus::GdiplusStartupInput startupInput;
			ULONG_PTR gdiplusToken = 0;
			if (Gdiplus::GdiplusStartup(&gdiplusToken, &startupInput, nullptr) != Gdiplus::Ok)
			{
				return;
			}

			HWND window = nullptr;
			HINSTANCE instance = GetModuleHandleW(nullptr);
			WNDCLASSW windowClass{};
			windowClass.lpfnWndProc = WindowProc;
			windowClass.hInstance = instance;
			windowClass.lpszClassName = BannerWindowClassName;
			windowClass.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
			const ATOM registeredClass = RegisterClassW(&windowClass);

			// Keep the surface in this scope: its images and fonts must be
			// destroyed before shutting down GDI+.
			{
				Surface surface{ this };
				try
				{
					surface.LoadArtwork(artworkDirectory, stop);
					window = CreateBannerWindow(instance, surface);
					if (window && !stop.stop_requested())
					{
						AnimateUntilStopped(window, surface, stop);
					}
				}
				catch (...)
				{
					// Optional presentation must never prevent the editor from starting.
				}

				if (window)
				{
					DestroyWindow(window);
				}
			}

			if (registeredClass)
			{
				UnregisterClassW(BannerWindowClassName, instance);
			}

			Gdiplus::GdiplusShutdown(gdiplusToken);
		}
	};

	WindowsStartupBanner::WindowsStartupBanner(
		const std::filesystem::path& artworkDirectory, EStartupBannerMode mode)
		: State(std::make_unique<Impl>())
	{
		State->Mode = mode == EStartupBannerMode::Configured ? ReadConfiguredMode(artworkDirectory) : mode;
		State->AnimationThread = std::jthread(
			[state = State.get(), artworkDirectory](std::stop_token stop)
			{
				state->Run(stop, artworkDirectory);
			});
	}

	WindowsStartupBanner::~WindowsStartupBanner() = default;

	EStartupBannerMode WindowsStartupBanner::ReadConfiguredMode(const std::filesystem::path& artworkDirectory)
	{
		const auto settingsPath = std::filesystem::absolute(artworkDirectory / "Startup.ini");
		wchar_t mode[32]{};
		GetPrivateProfileStringW(L"Startup", L"Mode", L"classic", mode, 32, settingsPath.c_str());
		if (_wcsicmp(mode, L"lightning") == 0 || _wcsicmp(mode, L"cinematic") == 0)
		{
			return EStartupBannerMode::Lightning;
		}
		if (_wcsicmp(mode, L"reality-cut") == 0)
		{
			return EStartupBannerMode::RealityCut;
		}
		if (_wcsicmp(mode, L"chasm") == 0)
		{
			return EStartupBannerMode::Chasm;
		}
		if (_wcsicmp(mode, L"rift") == 0)
		{
			return EStartupBannerMode::Rift;
		}
		if (_wcsicmp(mode, L"assemble") == 0)
		{
			return EStartupBannerMode::Assemble;
		}
		if (_wcsicmp(mode, L"assemble_v2") == 0 || _wcsicmp(mode, L"assemble-v2") == 0)
		{
			return EStartupBannerMode::AssembleV2;
		}
		return EStartupBannerMode::Classic;
	}

	void WindowsStartupBanner::SetStatus(std::wstring status)
	{
		std::lock_guard lock(State->StatusMutex);
		State->StatusText = std::move(status);
	}

#ifdef NYX_STARTUP_PREVIEW
	void WindowsStartupBanner::Benchmark(const std::filesystem::path& artworkDirectory, int width, int height,
		EStartupBannerMode mode)
	{
		Gdiplus::GdiplusStartupInput input;
		ULONG_PTR token = 0;
		if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
		{
			throw std::runtime_error("Could not initialize GDI+ for benchmark");
		}

		{
			Impl owner;
			owner.Mode = mode;
			Impl::Surface surface{ &owner };
			surface.LoadArtwork(artworkDirectory);
			if (surface.IsCinematic())
			{
				const Gdiplus::Size canvas = IntroCanvasSize(mode, width, height);
				auto desktop = UsesDesktopCapture(mode) ? MakeDemoDesktop(canvas.Width, canvas.Height) : nullptr;
				surface.PrepareIntro(width, height, desktop.get());
			}
			std::vector<double> samples;
			constexpr int warmupFrames = 5;
			constexpr int measuredFrames = 240; // Include the entire intro and its transition to live artwork.
			for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame)
			{
				const auto begin = std::chrono::steady_clock::now();
				const float elapsedSeconds = frame / static_cast<float>(TargetFramesPerSecond);
				if (surface.IsCinematic())
				{
					surface.RenderIntroFrame(elapsedSeconds);
				}
				else
				{
					surface.Render(width, height, elapsedSeconds);
				}
				const double frameMilliseconds = std::chrono::duration<double, std::milli>(
					std::chrono::steady_clock::now() - begin).count();
				if (frame >= warmupFrames)
				{
					samples.push_back(frameMilliseconds);
				}
			}
			double total = 0;
			for (const double frameMilliseconds : samples)
			{
				total += frameMilliseconds;
			}
			std::sort(samples.begin(), samples.end());
			std::cout << (mode == EStartupBannerMode::AssembleV2 ? "Assemble v2 " : mode == EStartupBannerMode::Assemble ? "Assemble " : mode == EStartupBannerMode::Rift ? "Rift " : (mode == EStartupBannerMode::Chasm ? "Chasm " :
				(mode == EStartupBannerMode::RealityCut ? "Reality cut " : (surface.IsCinematic() ? "Lightning " : "Classic "))))
				<< width << 'x' << height << ": " << surface.Images.size() << " images, "
				<< samples.size() << " frames, mean " << total / samples.size()
				<< " ms, p95 " << samples[static_cast<size_t>(samples.size() * 0.95)]
				<< " ms (CPU drawing only; excludes window presentation)\n";
		}
		Gdiplus::GdiplusShutdown(token);
	}

	void WindowsStartupBanner::ExportIntroFrames(const std::filesystem::path& artworkDirectory,
		const std::filesystem::path& outputDirectory, EStartupBannerMode mode)
	{
		if (mode != EStartupBannerMode::Lightning && !UsesDesktopCapture(mode))
		{
			throw std::runtime_error("Frame export requires --lightning, --reality-cut, --chasm, --rift or --assemble");
		}
		Gdiplus::GdiplusStartupInput input;
		ULONG_PTR token = 0;
		if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
		{
			throw std::runtime_error("Could not initialize GDI+ for frame export");
		}
		try
		{
			Impl owner;
			owner.Mode = mode;
			Impl::Surface surface{ &owner };
			surface.LoadArtwork(artworkDirectory);
			const Gdiplus::Size canvas = IntroCanvasSize(mode, 640, 360);
			auto desktop = MakeDemoDesktop(canvas.Width, canvas.Height);
			surface.PrepareIntro(640, 360, mode != EStartupBannerMode::Lightning ? desktop.get() : nullptr);
			std::filesystem::create_directories(outputDirectory);
			const CLSID pngEncoder{ 0x557cf406, 0x1a04, 0x11d3, { 0x9a, 0x73, 0x00, 0x00, 0xf8, 0x1e, 0xf3, 0x2e } };
			const auto backgroundPath = outputDirectory / "demo-desktop.png";
			desktop->Save(backgroundPath.c_str(), &pngEncoder, nullptr);
			constexpr int exportFramesPerSecond = 60;
			constexpr int frameCount = 216;
			Gdiplus::Bitmap contactSheet(2080, 768, PixelFormat32bppPARGB);
			Gdiplus::Graphics sheet(&contactSheet);
			sheet.Clear(Gdiplus::Color(255, 16, 18, 24));
			int reviewIndex = 0;
			for (int index = 0; index < frameCount; ++index)
			{
				Gdiplus::Bitmap* frame = surface.RenderIntroFrame(index / static_cast<float>(exportFramesPerSecond));
				if (mode == EStartupBannerMode::Chasm && index == 180)
				{
					Gdiplus::Color hole;
					frame->GetPixel(520, 240, &hole);
					if (hole.GetValue() != Gdiplus::Color(255, 0, 0, 0).GetValue())
					{
						throw std::runtime_error("Collapsed surface must reveal opaque blackness, not the live desktop");
					}
				}
				if (mode == EStartupBannerMode::Rift && index == 132)
				{
					// 2.20 s: the sky has torn open and the breach burns hot and opaque.
					Gdiplus::Color breach;
					frame->GetPixel(520, 270, &breach);
					if (breach.GetA() != 255 || breach.GetR() < breach.GetB() + 16)
					{
						throw std::runtime_error("Rift breach must be open and molten after the shatter");
					}
				}
				if (mode == EStartupBannerMode::Rift && index == 96)
				{
					// 1.60 s: still intact, cracked and cold, before the shatter.
					Gdiplus::Color sky;
					frame->GetPixel(520, 120, &sky);
					if (sky.GetA() != 255 || sky.GetR() > sky.GetB())
					{
						throw std::runtime_error("Rift sky must stay intact and cold before the impact");
					}
				}
				if (mode == EStartupBannerMode::RealityCut && index == 132)
				{
					// The hold at 2.20 s must reproduce the source exactly before the impact.
					for (const Gdiplus::Point point : { Gdiplus::Point(350, 270), Gdiplus::Point(520, 410), Gdiplus::Point(720, 480) })
					{
						Gdiplus::Color expected, actual;
						desktop->GetPixel(point.X, point.Y, &expected);
						frame->GetPixel(point.X, point.Y, &actual);
						if (actual.GetValue() != expected.GetValue())
						{
							throw std::runtime_error("Desktop fragments must fully recombine before the finishing impact");
						}
					}
				}
				if (index == frameCount - 1)
				{
					Gdiplus::Color corner;
					frame->GetPixel(0, 0, &corner);
					// The banner's centre; intros larger than the banner also leave a transparent surround.
					const bool bBannerFillsCanvas = IsBannerSizedIntro(mode);
					Gdiplus::Color center;
					frame->GetPixel(bBannerFillsCanvas ? canvas.Width / 2 : 520, bBannerFillsCanvas ? canvas.Height / 2 : 410, &center);
					if (center.GetA() != 255 || (!bBannerFillsCanvas && corner.GetA() != 0))
					{
						throw std::runtime_error("Completed intro must have an opaque banner and transparent surround");
					}
				}
				const auto path = outputDirectory / ("intro-" + std::to_string(index) + ".png");
				if (frame->Save(path.c_str(), &pngEncoder, nullptr) != Gdiplus::Ok)
				{
					throw std::runtime_error("Could not save intro frame");
				}
				// The rift's beats are charge, cracks, pulses, inhale, impact, shatter, breach and reveal.
				const bool bReviewFrame = mode == EStartupBannerMode::Rift ?
					(index == 24 || index == 45 || index == 78 || index == 103 || index == 111 || index == 117 || index == 132 || index == 174) :
					(index == 17 || index == 42 || index == 78 || index == 132 || index == 141 || index == 153 || index == 180 || index == 208);
				if (bReviewFrame)
				{
					const int x = reviewIndex % 4 * 520;
					const int y = reviewIndex / 4 * 384;
					const int cellHeight = 520 * canvas.Height / canvas.Width;
					sheet.DrawImage(desktop.get(), x, y, 520, cellHeight);
					sheet.DrawImage(frame, x, y, 520, cellHeight);
					Gdiplus::SolidBrush ink(Gdiplus::Color(255, 220, 223, 237));
					Gdiplus::Font font(L"Segoe UI", 13, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
					const auto label = std::to_wstring(index / static_cast<float>(exportFramesPerSecond)).substr(0, 4) + L" s";
					sheet.DrawString(label.c_str(), -1, &font, Gdiplus::PointF(static_cast<float>(x + 12),
						static_cast<float>(y + cellHeight + 2)), &ink);
					++reviewIndex;
				}
			}
			const auto sheetPath = outputDirectory / "contact-sheet.png";
			contactSheet.Save(sheetPath.c_str(), &pngEncoder, nullptr);
			if (mode == EStartupBannerMode::Chasm)
			{
				// Runtime chasm windows follow monitor work areas, including portrait
				// layouts. Both must finish collapsing before the shared reveal deadline.
				for (const Gdiplus::Size size : { Gdiplus::Size(1920, 1080), Gdiplus::Size(832, 1280) })
				{
					auto source = MakeDemoDesktop(size.Width, size.Height);
					surface.PrepareIntro(640, 360, source.get(), size.Width, size.Height);
					auto* collapsed = surface.RenderIntroFrame(3.0f);
					Gdiplus::Color center;
					collapsed->GetPixel(size.Width / 2, size.Height / 2, &center);
					if (collapsed->GetWidth() != size.Width || collapsed->GetHeight() != size.Height ||
						center.GetValue() != Gdiplus::Color(255, 0, 0, 0).GetValue())
					{
						throw std::runtime_error("Chasm did not finish collapsing at the requested work-area size");
					}
				}
			}
			// Use the actual exported frames for a reviewable animation, including a
			// scrubber. The background is synthetic; this path never calls CaptureDesktop.
			std::ofstream player(outputDirectory / "preview.html");
			player << R"HTML(<!doctype html><meta charset="utf-8"><title>Nyx intro review</title>
<style>body{margin:0;background:#101218;color:#eee;font:14px system-ui;display:grid;justify-items:center;gap:12px}
canvas{width:min(95vw,1040px);height:auto}input{width:500px}button{padding:8px 18px}</style>
<p>Nyx startup animation · synthetic desktop · 60 FPS frame export</p><canvas width="1040" height="720"></canvas>
<div><button id="play">Pause</button> <input id="scrub" type="range" min="0" max="215" value="0"> <span id="time"></span></div>
<script>
const canvas=document.querySelector('canvas'),ctx=canvas.getContext('2d'),scrub=document.querySelector('#scrub');
const bg=new Image();bg.src='demo-desktop.png';const frames=Array.from({length:216},(_,i)=>{const image=new Image();image.src=`intro-${i}.png`;return image;});
let playing=true,index=0,last=0,accumulator=0;document.querySelector('#play').onclick=()=>{playing=!playing;document.querySelector('#play').textContent=playing?'Pause':'Play';};
scrub.oninput=()=>{playing=false;document.querySelector('#play').textContent='Play';index=Number(scrub.value);};
function draw(now){if(playing&&last){accumulator+=Math.min(now-last,250);const steps=Math.floor(accumulator/(1000/60));index=(index+steps)%216;accumulator-=steps*(1000/60);}last=now;
ctx.clearRect(0,0,canvas.width,canvas.height);if(bg.complete&&bg.naturalWidth)ctx.drawImage(bg,0,0);const frame=frames[index];
if(frame.complete&&frame.naturalWidth)ctx.drawImage(frame,0,0);scrub.value=index;document.querySelector('#time').textContent=(index/60).toFixed(2)+' s';requestAnimationFrame(draw);}
Promise.all([bg,...frames].map(image=>image.decode())).then(()=>{canvas.width=bg.naturalWidth;canvas.height=bg.naturalHeight;requestAnimationFrame(draw);});
</script>)HTML";
			std::cout << "Exported 216 intro frames and preview.html; recombination and transparency checks passed.\n";
		}
		catch (...)
		{
			Gdiplus::GdiplusShutdown(token);
			throw;
		}
		Gdiplus::GdiplusShutdown(token);
	}
#endif
}
