#include "InputRecordingLayer.h"

#include "Application.h"
#include "Log.h"

namespace Nyx::Game
{
	InputRecordingLayer::InputRecordingLayer(std::filesystem::path recordPath)
		: RecordPath(std::move(recordPath))
	{
	}

	void InputRecordingLayer::OnAttach(Nyx::Application& application)
	{
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
