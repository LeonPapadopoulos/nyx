#pragma once

#include "BinaryArchive.h"
#include "EditorLinkMessages.h"
#include "NetConnection.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// The editor link: the connection between the editor and a game it started. Both ends use the
// same EditorLink class and the same messages (EditorLinkMessages.h), whichever program listens.
namespace Nyx::Engine
{
	enum class EEditorLinkState : uint8_t
	{
		Connecting,       // The connection isn't established yet; this side's Hello waits to go out
		WaitingForHello,  // This side's Hello went out; waiting for the other side's
		Connected,        // Both sides said Hello with the same protocol version
		Closed,
	};

	// Seen from the side that observes the message
	enum class ELinkDirection : uint8_t
	{
		Sent,
		Received,
	};

	// Sees every message of a link, in both directions, including the Hellos and messages that
	// end the link with a protocol error. For message logs and recordings.
	using EditorLinkObserver = std::function<void(ELinkDirection direction, const Net::Message& message)>;

	// One end of the editor link. Call Update() once per frame.
	class EditorLink
	{
	public:
		// Takes over a connection in any state: just started connecting, or accepted by a Listener.
		// This side's Hello, with programName, is queued right away, so messages sent before the
		// connection is established still go out after it. The observer, if any, sees that Hello too.
		EditorLink(std::unique_ptr<Net::Connection> connection, std::string programName, EditorLinkObserver observer = {});

		// The other side has to report this process id in its Hello, or the link closes. The editor
		// uses it to make sure the program that connected is the game it started.
		void RequireOtherProcessId(uint32_t processId)
		{
			RequiredOtherProcessId = processId;
		}

		// Polls the connection, says Hello and checks the other side's. Messages after the Hello
		// are kept for Receive(). Logs when the link is established and when it ends, unless it is
		// closed with Close() or CloseGracefully().
		void Update();

		EEditorLinkState GetState() const
		{
			return State;
		}

		bool IsConnected() const
		{
			return State == EEditorLinkState::Connected;
		}

		// Whether the other side's Hello was accepted, even if the link closed since. Its messages
		// can still be taken with Receive() then.
		bool HadHandshake() const
		{
			return bHadHandshake;
		}

		// The other side's Hello, once Connected
		const HelloMessage& GetOtherSide() const
		{
			return OtherSide;
		}

		// Why the link is Closed
		const std::string& GetCloseReason() const;

		// Sends a message type with Type, Write() and Read(), e.g. LogLineMessage. Ignored once
		// Closed. After a Quit, the other side closing the link is expected and logged as such.
		template <typename TMessage>
		void Send(const TMessage& message)
		{
			Send(MakeNetMessage(message));
		}

		// Sends a message made with MakeNetMessage(), e.g. one that waited in a queue
		void Send(const Net::Message& message);

		// The next message after the handshake, in order
		std::optional<Net::Message> Receive();

		// Bytes sent but not taken by the system yet, e.g. because the other side doesn't read
		size_t GetUnsentSize() const
		{
			return Connection->GetUnsentSize();
		}

		// Closes the link without logging; reason is for GetCloseReason(). Messages that have
		// arrived can still be taken with Receive().
		void Close(const std::string& reason);

		// Sends what is queued, tells the other side that nothing more comes, and waits until it
		// closes its end too, for at most timeLimit. Then closes like Close(). Blocks while it waits.
		// Use it when this program ends, so its last messages arrive.
		void CloseGracefully(const std::string& reason, std::chrono::milliseconds timeLimit);

	private:
		void SayHello();
		void HandleHello(const Net::Message& message);
		void CloseWithError(const std::string& reason);
		std::string GetOtherSideName() const;

		// After closing: shows the messages that arrived meanwhile to the observer, and keeps them for Receive()
		void TakeArrivedMessages();

	private:
		std::unique_ptr<Net::Connection> Connection;
		std::string ProgramName;
		EditorLinkObserver Observer;
		EEditorLinkState State = EEditorLinkState::Connecting;

		HelloMessage OtherSide;
		bool bHadHandshake = false;
		bool bCloseExpected = false;
		std::optional<uint32_t> RequiredOtherProcessId;
		std::deque<Net::Message> Received;

		// Set when this class closes the link, so the reason isn't the connection's
		std::string LinkCloseReason;
	};
}
