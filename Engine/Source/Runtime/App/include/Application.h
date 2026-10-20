#pragma once
#include "NyxEngineAPI.h"
#include "Layer.h"
#include "FixedStepClock.h"
#include "GameModule.h"
#include "InputSystem.h"
#include "Window.h"
#include <memory>
#include <vector>

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

		// Keyboard and mouse, gathered once per frame before the layers' OnUpdate
		Nyx::Input::InputSystem& GetInput() { return Input; }

		// The fixed simulation steps: each frame's real time in steps for the layers' OnFixedUpdate.
		// Pause, step and time scale go here.
		FixedStepClock& GetSimulationClock() { return SimulationClock; }

		// The game module the application was made with; an empty one if none
		const GameModule& GetGameModule() const { return *Module; }

		// Ends the frame loop after the current frame, as if the user closed the window
		void RequestQuit() { Window->RequestClose(); }

	private:
		// The renderer's frame, with the layers' UI inside the window's; nothing while the window is
		// minimized
		void DrawFrame();

		// Runs the steps the clock asks for this frame
		void RunFixedSteps(double frameSeconds);

		const GameModule* Module = nullptr;
		std::unique_ptr<IWindow> Window;
		std::unique_ptr<IEditorRenderer> Renderer;
		Nyx::Input::InputSystem Input;
		FixedStepClock SimulationClock;
		// Set while the clock drops time, so the warning comes once, not every frame
		bool WarnedOfDroppedTime = false;
		std::vector<std::unique_ptr<ILayer>> Layers;
	};

	// To be defined by the client (for example the editor), which can use the command line arguments.
	Application* CreateApplication(int argc, char** argv);
}
