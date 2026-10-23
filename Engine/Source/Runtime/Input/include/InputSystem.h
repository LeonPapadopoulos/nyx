#pragma once

#include "ImGuiInput.h"
#include "InputActions.h"
#include "InputBackend.h"
#include "InputContexts.h"
#include "InputFrame.h"
#include "InputUser.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Nyx::Input
{
	struct InputRecording;

	// Gathers what the backends report into one InputFrame per frame, runs the user's context
	// stack over it to decide the actions, and makes the cursor what the stack says
	// (Docs/Design/input-system.md). The application owns one and runs it around the layers:
	//
	//   window.PollEvents(); input.BeginFrame(NowUs()); layers' OnUpdate and OnUI;
	//   input.SetImGuiState(ReadImGuiState()); input.EndFrame();
	//
	// Engine code puts its own contexts on the stack, as every other context:
	// - Unfocused (1000), while none of the program's windows has the focus: takes everything;
	// - ImGui (900), while ImGui has a text field active (takes the keyboard) or the mouse is over
	//   UI that isn't a view, with the cursor free (takes the mouse).
	class InputSystem
	{
	public:
		static constexpr int UnfocusedPriority = 1000;
		static constexpr int ImGuiPriority = 900;

		InputSystem();
		~InputSystem();

		// Its user points back at it
		InputSystem(const InputSystem&) = delete;
		InputSystem& operator=(const InputSystem&) = delete;

		// The backend must outlive the input system; the window owns the GLFW one.
		void AddBackend(IInputBackend& backend);

		ActionHandle AddAction(InputActionDef action);
		ContextHandle AddContext(InputContextDef context);

		// Changes what an action or context is, keeping its handle: a context stays on or off. Its
		// bindings keep what they remember (a held button, a hold's time) if they are the same
		// bindings, in the same order, with other tuning; otherwise they start afresh. For new
		// tuning, e.g. from an input asset while it is edited.
		void ReplaceAction(ActionHandle action, InputActionDef def);
		void ReplaceContext(ContextHandle context, InputContextDef def);

		// An unknown handle gives an empty definition
		const InputActionDef& GetAction(ActionHandle action) const;
		const InputContextDef& GetContext(ContextHandle context) const;

		size_t GetActionCount() const
		{
			return Actions.size();
		}

		size_t GetContextCount() const
		{
			return Contexts.size();
		}

		// The first action or context of that name; invalid if there is none
		ActionHandle FindAction(std::string_view name) const;
		ContextHandle FindContext(std::string_view name) const;

		// Counts the changes of actions and contexts: added or replaced
		uint64_t GetDefinitionsRevision() const
		{
			return DefinitionsRevision;
		}

		// The one player there is, owning every device
		InputUser& GetUser()
		{
			return User;
		}

		const InputUser& GetUser() const
		{
			return User;
		}

		ContextHandle GetUnfocusedContext() const
		{
			return UnfocusedContext;
		}

		ContextHandle GetImGuiContext() const
		{
			return ImGuiContext;
		}

		// Whether gamepads keep driving the program while none of its windows has the focus: the
		// Unfocused context then takes the keyboard and the mouse, not the pads. For several games
		// side by side (Play All) driven by one pad. Off by default.
		void SetGamepadsInBackground(bool bInBackground)
		{
			bGamepadsInBackground = bInBackground;
		}

		bool AreGamepadsInBackground() const
		{
			return bGamepadsInBackground;
		}

		// What ImGui wanted in the frame just built; the ImGui context follows it from the next frame
		void SetImGuiState(const ImGuiInputState& state)
		{
			ImGuiState = state;
		}

		const ImGuiInputState& GetImGuiState() const
		{
			return ImGuiState;
		}

		// Called in every BeginFrame(), after the frame's events and the system contexts, before
		// the actions are decided: with the stack they are decided with (InputStreamWriter). The
		// id is for RemoveFrameObserver().
		using FrameObserver = std::function<void(const InputSystem&)>;
		uint32_t AddFrameObserver(FrameObserver observer);
		void RemoveFrameObserver(uint32_t id);

		// Plays a recording (InputRecording.h) instead of the devices: one recorded frame per frame,
		// with the recorded times, events, focus and ImGui state, so the actions decide as they did
		// when it was recorded, as far as the program's code puts the same contexts on the stack.
		// Its actions and contexts are this program's own (the recording's definitions are left
		// out), so a replay can try other tuning. While it plays, the devices' controls and the
		// focus are ignored, and what they held lets go at its start; their coming and going isn't. The recorded devices get ids of their
		// own and are removed at the end; then the devices count again.
		void StartReplay(InputRecording recording);
		void StopReplay();
		bool IsReplaying() const
		{
			return Replay != nullptr;
		}

		// Frames replayed, and how many the recording has
		size_t GetReplayedFrames() const;
		size_t GetReplayFrameCount() const;

		// Whether devices coming and going and the cursor are logged; not for a replica of another
		// program's input
		void SetLogging(bool bLog)
		{
			bLogging = bLog;
		}

		// Takes every backend's events, in the order they happened, into the next frame, and
		// decides the actions
		void BeginFrame(uint64_t nowUs);

		// Applies the cursor the stack asks for, and sends the frame's output to the gamepads
		void EndFrame();

		const InputFrame& GetFrame() const
		{
			return Frame;
		}

		// The cursor as the last EndFrame() made it, and the context that asked for it
		const CursorRequest& GetCursor() const
		{
			return User.GetCursor();
		}

	private:
		void UpdateSystemContexts();

		// Sends the user's output of the frame to their gamepads, what each can do
		void SendOutput();

		std::vector<IInputBackend*> Backends;
		// Which backend reported each device, for sending output back
		std::unordered_map<uint32_t, IInputBackend*> DeviceBackends;
		InputFrame Frame;
		uint64_t PreviousFrameUs = 0;

		std::vector<InputActionDef> Actions;
		std::vector<InputContextDef> Contexts;
		InputUser User;

		ContextHandle UnfocusedContext;
		ContextHandle ImGuiContext;
		ImGuiInputState ImGuiState;
		bool bGamepadsInBackground = false;
		uint64_t DefinitionsRevision = 0;
		std::vector<std::pair<uint32_t, FrameObserver>> Observers;
		uint32_t NextObserverId = 1;

		// The recording being replayed, and where it is (InputSystem.cpp)
		struct ReplayState;
		std::unique_ptr<ReplayState> Replay;
		// The replayed devices' removal, for the frame after the replay
		std::vector<InputEvent> ReplayEnded;

		// The next replayed frame instead of the devices' events; false at the end of the recording
		bool TakeReplayFrame(uint64_t& inOutNowUs, std::vector<InputEvent>& inOutEvents);
		bool bLogging = true;
	};
}
