#pragma once
#include "NyxEngineAPI.h"
#include "Layer.h"
#include "FixedStepClock.h"
#include "FrameStats.h"
#include "GameModule.h"
#include "InputSystem.h"
#include "Window.h"
#include <memory>
#include <vector>

namespace Nyx::Input
{
	class SdlGamepadBackend;
}

namespace Nyx
{
	class IRenderer;
	class IEditorRenderer;

	// How an application wants to be set up, chosen by the client (for example the editor).
	struct ApplicationSpecs
	{
		WindowSpecs Window;

		// The game module whose component types are registered next to the engine's, before any
		// layer is attached; nullptr: the engine's types only. It must outlive the application.
		const GameModule* Module = nullptr;
	};

	// Owns the window and the renderer, and runs the frame loop. What the application actually
	// does, for example the editor, is added as layers.
	class NYXENGINE_API Application
	{
	public:
		explicit Application(const ApplicationSpecs& specs = ApplicationSpecs());
		virtual ~Application();

		void Run();

		// Attaches the layer right away; from then on it runs every frame. Push layers
		// before Run(), not from inside another layer's hooks.
		void PushLayer(std::unique_ptr<ILayer> layer);

		IWindow& GetWindow() { return *Window; }

		// The renderer that draws into the window: its scene part, what a game needs, and all of it
		// for the editor and the UI
		IRenderer& GetRenderer();
		IEditorRenderer& GetEditorRenderer() { return *Renderer; }

		// Keyboard, mouse and gamepads, gathered once per frame before the layers' OnUpdate
		Nyx::Input::InputSystem& GetInput() { return Input; }

		// The fixed simulation steps: each frame's real time in steps for the layers' OnFixedUpdate.
		// Pause, step and time scale go here.
		FixedStepClock& GetSimulationClock() { return SimulationClock; }

		// The game module the application was made with; an empty one if none
		const GameModule& GetGameModule() const { return *Module; }

		// Ends the frame loop after the current frame, as if the user closed the window
		void RequestQuit() { Window->RequestClose(); }

		// What main() returns once the application has ended; 0 unless set
		void SetExitCode(int exitCode) { ExitCode = exitCode; }
		int GetExitCode() const { return ExitCode; }

		// Every frame's numbers, a frame after it was drawn: the renderer's (IRenderer::GetFrameStats())
		// with the frame loop's own times, the last window's average and worst, and the last frames'
		// times for a graph
		const FrameStatsWindow& GetFrameStats() const { return FrameStats; }

	private:
		// The renderer's frame, with the layers' UI inside the window's; nothing while the window is
		// minimized. Whether it drew.
		bool DrawFrame();

		// Runs the steps the clock asks for this frame; how many
		uint32_t RunFixedSteps(double frameSeconds);

		const GameModule* Module = nullptr;
		std::unique_ptr<IWindow> Window;
		std::unique_ptr<IEditorRenderer> Renderer;
		// Before Input, so it outlives the input system that reads it
		std::unique_ptr<Nyx::Input::SdlGamepadBackend> Gamepads;
		Nyx::Input::InputSystem Input;
		FixedStepClock SimulationClock;
		// Set while the clock drops time, so the warning comes once, not every frame
		bool WarnedOfDroppedTime = false;
		std::vector<std::unique_ptr<ILayer>> Layers;

		// The frame loop's times of the frame drawn last, for its numbers once the renderer has
		// them; the renderer's frame they were added for last
		FrameStatsWindow FrameStats;
		ApplicationFrameTimes DrawnFrameTimes;
		uint64_t LastStatsFrame = 0;

		int ExitCode = 0;
	};

	// To be defined by the client (for example the editor), which can use the command line arguments.
	Application* CreateApplication(int argc, char** argv);
}
