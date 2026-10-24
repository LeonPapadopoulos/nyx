#include "BenchmarkLayer.h"

#include "Application.h"
#include "CameraComponent.h"
#include "CameraProjection.h"
#include "Log.h"
#include "NameComponent.h"
#include "Paths.h"
#include "Renderer.h"
#include "TransformComponent.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <fstream>
#include <system_error>

namespace Nyx::Game
{
	namespace
	{
		// Saved/Benchmarks/<scene>-<date>-<time>.csv
		std::filesystem::path MakeDefaultOutPath(const std::filesystem::path& scenePath)
		{
			const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
			std::tm local{};
			localtime_s(&local, &now);
			char stamp[32];
			std::strftime(stamp, sizeof(stamp), "%Y-%m-%d-%H%M%S", &local);
			return Nyx::Paths::GetSavedDir() / "Benchmarks" / (scenePath.stem().string() + "-" + stamp + ".csv");
		}

		// A camera at `position` looking at `lookAt`, up along +Y; the camera looks down its -Z
		glm::quat LookRotation(const glm::vec3& position, const glm::vec3& lookAt)
		{
			const glm::vec3 forward = glm::normalize(lookAt - position);
			const glm::vec3 up = std::abs(forward.y) > 0.999f ? glm::vec3(0.0f, 0.0f, -1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
			return glm::quatLookAt(forward, up);
		}
	}

	BenchmarkLayer::BenchmarkLayer(BenchmarkSettings settings, Nyx::Registry& world, std::filesystem::path scenePath)
		: Settings(std::move(settings))
		, World(world)
		, ScenePath(std::move(scenePath))
	{
	}

	void BenchmarkLayer::OnAttach(Nyx::Application& application)
	{
		App = &application;
		Renderer = &application.GetRenderer();
		// As fast as the machine draws, not as the monitor shows
		Renderer->SetVSync(false);

		if (Settings.OutPath.empty())
		{
			Settings.OutPath = MakeDefaultOutPath(ScenePath);
		}

		// The cameras there are now, in the order the scene has them
		World.Each<Nyx::CameraComponent>(
			[&](Nyx::Entity entity, const Nyx::CameraComponent& /*camera*/)
			{
				SceneCameras.push_back(entity);
			});

		if (!Settings.CameraPath.empty())
		{
			std::string error;
			CameraPath = Nyx::LoadBenchmarkCameraPath(Settings.CameraPath, error);
			if (!CameraPath)
			{
				Fail(error);
				return;
			}

			// The benchmark's own camera, seeing as the scene's primary one would; the scene's are
			// left alone, just not primary
			Nyx::CameraComponent camera;
			if (const std::optional<Nyx::WorldCamera> primary = Nyx::FindPrimaryCamera(World))
			{
				camera = primary->Camera;
			}
			camera.bPrimary = true;
			for (const Nyx::Entity entity : SceneCameras)
			{
				World.Get<Nyx::CameraComponent>(entity).bPrimary = false;
			}
			PathCamera = World.CreateEntity();
			World.Add<Nyx::NameComponent>(PathCamera, Nyx::NameComponent{ "Benchmark camera" });
			World.Add<Nyx::TransformComponent>(PathCamera);
			World.Add<Nyx::CameraComponent>(PathCamera, camera);
			SceneCameras.clear();
		}
		else if (SceneCameras.size() < 2)
		{
			// The scene's primary camera, as it moves itself
			SceneCameras.clear();
		}

		LOG_INFO("Benchmark: {0:.1f} s after {1:.1f} s to settle, into '{2}'; the camera {3}", Settings.Seconds, Settings.WarmupSeconds,
			Settings.OutPath.string(),
			CameraPath           ? "follows '" + Settings.CameraPath.string() + "'"
			: SceneCameras.empty() ? std::string("is the scene's primary camera")
								   : "takes the scene's " + std::to_string(SceneCameras.size()) + " cameras in turn");
		PlaceCamera(0.0);
	}

	void BenchmarkLayer::OnUpdate(float deltaTime)
	{
		if (Phase == EPhase::Done)
		{
			return;
		}
		PhaseSeconds += deltaTime;
		const Nyx::FrameStatsWindow& stats = App->GetFrameStats();

		if (Phase == EPhase::Warmup)
		{
			if (PhaseSeconds < Settings.WarmupSeconds || Renderer->IsCompilingShaders())
			{
				return;
			}
			// From the frame drawn in this loop on: the last one drawn completes in this loop
			Phase = EPhase::Recording;
			PhaseSeconds = 0.0;
			FirstRecordedFrame = stats.GetLast().Frame + 2;
			SeenFrameCount = stats.GetFrameCount();
			Frames.reserve(static_cast<size_t>(Settings.Seconds * 1000.0) + 64);
			LOG_INFO("Benchmark: recording");
			return;
		}

		// Every frame's numbers once the renderer has them, at most one a loop
		if (stats.GetFrameCount() != SeenFrameCount)
		{
			SeenFrameCount = stats.GetFrameCount();
			const Nyx::FrameStatsSample& frame = stats.GetLast();
			if (frame.Frame >= FirstRecordedFrame)
			{
				Frames.push_back(frame);
				RecordedMilliseconds += frame[Nyx::EFrameStat::FrameTime];
			}
		}

		// The frames recorded make up the time; real time ends it too, should no frame be drawn
		// (a minimized window)
		if (RecordedMilliseconds >= Settings.Seconds * 1000.0 || PhaseSeconds >= Settings.Seconds + 10.0)
		{
			Finish();
		}
	}

	void BenchmarkLayer::OnLateUpdate(float /*deltaTime*/)
	{
		if (Phase == EPhase::Recording)
		{
			PlaceCamera(PhaseSeconds);
		}
	}

	void BenchmarkLayer::PlaceCamera(double seconds)
	{
		if (CameraPath && World.IsAlive(PathCamera))
		{
			const Nyx::BenchmarkCameraKey key = Nyx::SampleBenchmarkCameraPath(*CameraPath, static_cast<float>(seconds));
			Nyx::TransformComponent& transform = World.Get<Nyx::TransformComponent>(PathCamera);
			transform.Position = key.Position;
			transform.Rotation = LookRotation(key.Position, key.LookAt);
			return;
		}

		// Each of the scene's cameras primary for its share of the recording, in turn
		if (!SceneCameras.empty())
		{
			const size_t current = std::min(SceneCameras.size() - 1, static_cast<size_t>(seconds / Settings.Seconds * static_cast<double>(SceneCameras.size())));
			for (size_t index = 0; index < SceneCameras.size(); ++index)
			{
				if (World.IsAlive(SceneCameras[index]) && World.Has<Nyx::CameraComponent>(SceneCameras[index]))
				{
					World.Get<Nyx::CameraComponent>(SceneCameras[index]).bPrimary = index == current;
				}
			}
		}
	}

	void BenchmarkLayer::Finish()
	{
		Phase = EPhase::Done;
		if (Frames.empty())
		{
			Fail("no frame was drawn while recording");
			return;
		}

		const Nyx::FrameBenchmarkSummary summary = Nyx::SummarizeFrameBenchmark(Frames);
		const std::string csv = Nyx::MakeFrameBenchmarkCsv(Frames, summary);

		std::error_code error;
		if (Settings.OutPath.has_parent_path())
		{
			std::filesystem::create_directories(Settings.OutPath.parent_path(), error);
		}
		std::ofstream file(Settings.OutPath, std::ios::binary | std::ios::trunc);
		file.write(csv.data(), static_cast<std::streamsize>(csv.size()));
		file.close();
		if (!file)
		{
			Fail("can't write '" + Settings.OutPath.string() + "'");
			return;
		}

		LOG_INFO("Benchmark: {0}; written to '{1}'", Nyx::FormatFrameBenchmarkSummary(summary), Settings.OutPath.string());
		App->SetExitCode(0);
		App->RequestQuit();
	}

	void BenchmarkLayer::Fail(const std::string& why)
	{
		Phase = EPhase::Done;
		LOG_ERROR("Benchmark failed: {0}", why);
		App->SetExitCode(1);
		App->RequestQuit();
	}
}
