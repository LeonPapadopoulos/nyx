#include "InputRecordingLayer.h"

#include "Application.h"
#include "Log.h"

namespace Nyx::Game
{
	InputRecordingLayer::InputRecordingLayer(std::filesystem::path recordPath, std::filesystem::path replayPath)
		: RecordPath(std::move(recordPath))
		, ReplayPath(std::move(replayPath))
	{
	}

	void InputRecordingLayer::OnAttach(Nyx::Application& application)
	{
		if (!ReplayPath.empty())
		{
			std::string problem;
			if (std::optional<Nyx::Input::InputRecording> recording = Nyx::Input::LoadInputRecording(ReplayPath, &problem))
			{
				LOG_INFO("Input: replaying '{0}' (--input-replay)", ReplayPath.string());
				application.GetInput().StartReplay(std::move(*recording));
			}
			else
			{
				LOG_WARNING("Input: can't replay: {0}", problem);
			}
		}

		if (RecordPath.empty())
		{
			return;
		}
		if (Recording.Start(application.GetInput(), RecordPath, "NyxGame"))
		{
			LOG_INFO("Input: recording to '{0}' (--input-record)", RecordPath.string());
		}
		else
		{
			LOG_WARNING("Input: can't record to '{0}'", RecordPath.string());
		}
	}

	void InputRecordingLayer::OnDetach()
	{
		if (Recording.IsRecording())
		{
			LOG_INFO("Input: recorded {0} frames to '{1}'", Recording.GetRecorder().GetFrameCount(), RecordPath.string());
		}
		Recording.Stop();
	}

	void InputRecordingLayer::OnUpdate(float /*deltaTime*/)
	{
		Recording.Update();
	}
}
