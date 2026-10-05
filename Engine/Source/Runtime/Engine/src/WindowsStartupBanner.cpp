#include "WindowsStartupBanner.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <gdiplus.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cwctype>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#ifdef NYX_STARTUP_PREVIEW
#include <iostream>
#endif

namespace
{
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

				if (clientArea.right > 0 && clientArea.bottom > 0)
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
					return HTCAPTION;
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

			const float dpiScale = GetDpiForSystem() / 96.0f;
			const int width = static_cast<int>(BannerWidth * dpiScale);
			const int height = static_cast<int>(BannerHeight * dpiScale);
			surface.PrepareBuffers(width, height);

			const RECT& workArea = monitorInfo.rcWork;
			const int x = workArea.left + (workArea.right - workArea.left - width) / 2;
			const int y = workArea.top + (workArea.bottom - workArea.top - height) / 2;

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
			SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
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
				if (stop.stop_requested() && fadeOutStartSeconds < 0.0f)
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
						if (opacity != previousOpacity)
						{
							SetLayeredWindowAttributes(window, 0, opacity, LWA_ALPHA);
							previousOpacity = opacity;
						}

						InvalidateRect(window, nullptr, FALSE);
						UpdateWindow(window);
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

	WindowsStartupBanner::WindowsStartupBanner(const std::filesystem::path& artworkDirectory)
		: State(std::make_unique<Impl>())
	{
		State->AnimationThread = std::jthread(
			[state = State.get(), artworkDirectory](std::stop_token stop)
			{
				state->Run(stop, artworkDirectory);
			});
	}

	WindowsStartupBanner::~WindowsStartupBanner() = default;

	void WindowsStartupBanner::SetStatus(std::wstring status)
	{
		std::lock_guard lock(State->StatusMutex);
		State->StatusText = std::move(status);
	}

#ifdef NYX_STARTUP_PREVIEW
	void WindowsStartupBanner::Benchmark(const std::filesystem::path& artworkDirectory, int width, int height)
	{
		Gdiplus::GdiplusStartupInput input;
		ULONG_PTR token = 0;
		if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
		{
			throw std::runtime_error("Could not initialize GDI+ for benchmark");
		}

		{
			Impl owner;
			Impl::Surface surface{ &owner };
			surface.LoadArtwork(artworkDirectory);
			std::vector<double> samples;
			constexpr int warmupFrames = 5;
			constexpr int measuredFrames = 120;
			for (int frame = 0; frame < warmupFrames + measuredFrames; ++frame)
			{
				const auto begin = std::chrono::steady_clock::now();
				surface.Render(width, height, frame / static_cast<float>(TargetFramesPerSecond));
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
			std::cout << width << 'x' << height << ": " << surface.Images.size() << " images, "
				<< samples.size() << " frames, mean " << total / samples.size()
				<< " ms, p95 " << samples[static_cast<size_t>(samples.size() * 0.95)]
				<< " ms (CPU drawing only; excludes window presentation)\n";
		}
		Gdiplus::GdiplusShutdown(token);
	}
#endif
}
