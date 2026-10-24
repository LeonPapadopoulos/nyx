#pragma once

#include "Entity.h"
#include "FrameBenchmark.h"
#include "FrameStats.h"
#include "Layer.h"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

namespace Nyx
{
	class IRenderer;
	class Registry;
}

namespace Nyx::Game
{
	// What a benchmark does (GameLaunchOptions' --benchmark options)
	struct BenchmarkSettings
	{
		double Seconds = 10.0;
		// For the scene to settle and finish loading first: at least this long, and until no shader
		// compiles any more
		double WarmupSeconds = 3.0;
		std::filesystem::path OutPath;
		// Empty: the scene's own cameras
		std::filesystem::path CameraPath;
	};

	// Benchmark mode: runs the game's scene without vsync, waits for it to settle, records every
	// frame's numbers (Application::GetFrameStats()) for some seconds, writes them as CSV
	// (MakeFrameBenchmarkCsv()) and quits, with exit code 0 once the file is written, else 1.
	// The camera follows the camera path, if given, through a camera of the benchmark's own; or else
	// the recording is shared out among the scene's cameras in turn, each primary for its share; or
	// the scene's primary camera sees it all. Pushed after the GameLayer, so it moves the camera
	// after the game's cameras have moved.
	class BenchmarkLayer : public Nyx::ILayer
	{
	public:
		// `world` is the game's, and outlives the layer
		BenchmarkLayer(BenchmarkSettings settings, Nyx::Registry& world, std::filesystem::path scenePath);

		void OnAttach(Nyx::Application& application) override;
		void OnUpdate(float deltaTime) override;
		void OnLateUpdate(float deltaTime) override;

	private:
		// Puts the camera where the recording has it `seconds` in
		void PlaceCamera(double seconds);
		// Writes the file, logs the summary and quits
		void Finish();
		void Fail(const std::string& why);

		BenchmarkSettings Settings;
		Nyx::Registry& World;
		std::filesystem::path ScenePath;

		Nyx::Application* App = nullptr;
		Nyx::IRenderer* Renderer = nullptr;

		std::optional<Nyx::BenchmarkCameraPath> CameraPath;
		// The benchmark's camera on the path, or the scene's cameras taken in turn
		Nyx::Entity PathCamera{};
		std::vector<Nyx::Entity> SceneCameras;

		enum class EPhase
		{
			Warmup,
			Recording,
			Done
		};
		EPhase Phase = EPhase::Warmup;
		// Real time in the phase
		double PhaseSeconds = 0.0;
		// Frames drawn after this one are recorded; the stats' frames counted when last looked at
		uint64_t FirstRecordedFrame = 0;
		uint64_t SeenFrameCount = 0;
		std::vector<Nyx::FrameStatsSample> Frames;
		double RecordedMilliseconds = 0.0;
	};
}
