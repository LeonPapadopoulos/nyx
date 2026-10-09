#include "NyxPCH.h"
#include "EditorLink.h"

#include "Log.h"

#if defined(_WIN32)
	#include <process.h>
#else
	#include <unistd.h>
#endif

namespace
{
	uint32_t GetThisProcessId()
	{
#if defined(_WIN32)
		return static_cast<uint32_t>(::_getpid());
#else
		return static_cast<uint32_t>(::getpid());
#endif
	}
}

namespace Nyx::Engine
{
	EditorLink::EditorLink(std::unique_ptr<Net::Connection> connection, std::string programName, EditorLinkObserver observer)
		: Connection(std::move(connection))
		, ProgramName(std::move(programName))
		, Observer(std::move(observer))
	{
		// Queued first, so it goes out before anything sent later, even while still connecting
		SayHello();

		if (Connection->IsConnected())
		{
			State = EEditorLinkState::WaitingForHello;
		}
	}

	void EditorLink::Update()
	{
		if (State == EEditorLinkState::Closed)
		{
			return;
		}

		Connection->Poll();

		if (State == EEditorLinkState::Connecting && Connection->IsConnected())
		{
			State = EEditorLinkState::WaitingForHello;
		}

		// Messages that arrived before the connection closed are still handled
		while (State != EEditorLinkState::Closed)
		{
			std::optional<Net::Message> message = Connection->Receive();
			if (!message)
			{
				break;
			}

			if (Observer)
			{
				Observer(ELinkDirection::Received, *message);
			}

			if (State != EEditorLinkState::Connected)
			{
				HandleHello(*message);
			}
			else if (message->Type == static_cast<uint16_t>(EEditorLinkMessage::Hello))
			{
				CloseWithError(GetOtherSideName() + " said Hello a second time");
			}
			else
			{
				Received.push_back(std::move(*message));
			}
		}

		if (State == EEditorLinkState::Closed || Connection->GetState() != Net::EConnectionState::Closed)
		{
			return;
		}

		State = EEditorLinkState::Closed;

		// After a Quit, even a reset is the other side doing what it was asked
		if (Connection->GetCloseCause() == Net::ECloseCause::OtherSide || (bCloseExpected && bHadHandshake))
		{
			LOG_INFO("Editor link: {0} closed the connection", GetOtherSideName());
		}
		else
		{
			LOG_WARNING("Editor link: {0}", Connection->GetCloseReason());
		}
	}

	const std::string& EditorLink::GetCloseReason() const
	{
		return LinkCloseReason.empty() ? Connection->GetCloseReason() : LinkCloseReason;
	}

	std::optional<Net::Message> EditorLink::Receive()
	{
		if (Received.empty())
		{
			return std::nullopt;
		}

		Net::Message message = std::move(Received.front());
		Received.pop_front();
		return message;
	}

	void EditorLink::Close(const std::string& reason)
	{
		if (State == EEditorLinkState::Closed)
		{
			return;
		}

		LinkCloseReason = reason;
		Connection->Close(reason);
		State = EEditorLinkState::Closed;
		TakeArrivedMessages();
	}

	void EditorLink::CloseGracefully(const std::string& reason, std::chrono::milliseconds timeLimit)
	{
		if (State == EEditorLinkState::Closed)
		{
			return;
		}

		LinkCloseReason = reason;
		Connection->CloseGracefully(reason, timeLimit);
		State = EEditorLinkState::Closed;
		TakeArrivedMessages();
	}

	void EditorLink::TakeArrivedMessages()
	{
		while (std::optional<Net::Message> message = Connection->Receive())
		{
			if (Observer)
			{
				Observer(ELinkDirection::Received, *message);
			}

			if (bHadHandshake && message->Type != static_cast<uint16_t>(EEditorLinkMessage::Hello))
			{
				Received.push_back(std::move(*message));
			}
		}
	}

	void EditorLink::SendPayload(uint16_t type, const std::vector<std::byte>& payload)
	{
		if (Connection->GetState() == Net::EConnectionState::Closed)
		{
			return;
		}

		if (Observer)
		{
			Observer(ELinkDirection::Sent, Net::Message{ type, payload });
		}

		Connection->Send(type, payload);
	}

	void EditorLink::SayHello()
	{
		HelloMessage hello;
		hello.ProgramName = ProgramName;
		hello.ProcessId = GetThisProcessId();
		Send(hello);
	}

	void EditorLink::HandleHello(const Net::Message& message)
	{
		if (message.Type != static_cast<uint16_t>(EEditorLinkMessage::Hello))
		{
			CloseWithError("the other side sent message type " + std::to_string(message.Type) +
				" before saying Hello; it may not be a Nyx program");
			return;
		}

		BinaryReader reader;
		reader.LoadFromMemory(message.Payload);

		HelloMessage hello;
		if (!hello.Read(reader))
		{
			CloseWithError("the other side's Hello can't be read");
			return;
		}

		OtherSide = hello;

		if (hello.ProtocolVersion != EditorLinkProtocolVersion)
		{
			CloseWithError(GetOtherSideName() + " speaks editor link protocol " + std::to_string(hello.ProtocolVersion) +
				", this " + ProgramName + " speaks " + std::to_string(EditorLinkProtocolVersion) +
				"; build both from the same sources");
			return;
		}

		if (RequiredOtherProcessId && hello.ProcessId != *RequiredOtherProcessId)
		{
			CloseWithError(GetOtherSideName() + " (process " + std::to_string(hello.ProcessId) +
				") connected, but the link is meant for process " + std::to_string(*RequiredOtherProcessId));
			return;
		}

		State = EEditorLinkState::Connected;
		bHadHandshake = true;
		LOG_INFO("Editor link: connected to {0} (process {1})", hello.ProgramName, hello.ProcessId);
	}

	void EditorLink::CloseWithError(const std::string& reason)
	{
		LOG_ERROR("Editor link: {0}", reason);
		Close(reason);
	}

	std::string EditorLink::GetOtherSideName() const
	{
		return OtherSide.ProgramName.empty() ? std::string("the other side") : OtherSide.ProgramName;
	}
}
