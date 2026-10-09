#pragma once

#include "BinaryArchive.h"
#include "NetConnection.h"

#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>

// The editor link: the connection between the editor and a game it started. Both ends use the
// same EditorLink class and the same messages, whichever program listens.
namespace Nyx::Engine
{
	// Raise this whenever a message changes in a way the other side would misread. Both sides
	// refuse to talk to a different version, so an editor and a game built from different commits
	// fail with a clear error instead.
	constexpr uint32_t EditorLinkProtocolVersion = 1;

	// Message type numbers on the wire. Never reuse a number for something else.
	enum class EEditorLinkMessage : uint16_t
	{
		Hello = 1,
	};

	// The first message in both directions. Its layout never changes, so any two versions can
	// read each other's Hello and tell that they differ.
	struct HelloMessage
	{
		static constexpr EEditorLinkMessage Type = EEditorLinkMessage::Hello;

		uint32_t ProtocolVersion = EditorLinkProtocolVersion;

		// "NyxEditor" or "NyxGame"
		std::string ProgramName;

		// Lets the editor check that the program that connected is the game it started
		uint32_t ProcessId = 0;

		void Write(BinaryWriter& writer) const;
		bool Read(BinaryReader& reader);
	};

	enum class EEditorLinkState : uint8_t
	{
		Connecting,       // The connection isn't established yet; this side's Hello waits to go out
		WaitingForHello,  // This side's Hello went out; waiting for the other side's
		Connected,        // Both sides said Hello with the same protocol version
		Closed,
	};

	// One end of the editor link. Call Update() once per frame.
	class EditorLink
	{
	public:
		// Takes over a connection in any state: just started connecting, or accepted by a Listener.
		// This side's Hello, with programName, is queued right away, so messages sent before the
		// connection is established still go out after it.
		EditorLink(std::unique_ptr<Net::Connection> connection, std::string programName);

		// The other side has to report this process id in its Hello, or the link closes. The editor
		// uses it to make sure the program that connected is the game it started.
		void RequireOtherProcessId(uint32_t processId)
		{
			RequiredOtherProcessId = processId;
		}

		// Polls the connection, says Hello and checks the other side's. Messages after the Hello
		// are kept for Receive(). Logs when the link is established and when it ends, unless it is
		// closed with Close().
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

		// Sends a message type with Type, Write() and Read(), e.g. HelloMessage. Ignored once Closed.
		template <typename TMessage>
		void Send(const TMessage& message)
		{
			BinaryWriter writer;
			message.Write(writer);
			Connection->Send(static_cast<uint16_t>(TMessage::Type), writer.GetBytes());
		}

		// The next message after the handshake, in order
		std::optional<Net::Message> Receive();

		// Closes the link without logging; reason is for GetCloseReason().
		void Close(const std::string& reason);

	private:
		void SayHello();
		void HandleHello(const Net::Message& message);
		void CloseWithError(const std::string& reason);
		std::string GetOtherSideName() const;

	private:
		std::unique_ptr<Net::Connection> Connection;
		std::string ProgramName;
		EEditorLinkState State = EEditorLinkState::Connecting;

		HelloMessage OtherSide;
		bool bHadHandshake = false;
		std::optional<uint32_t> RequiredOtherProcessId;
		std::deque<Net::Message> Received;

		// Set when this class closes the link, so the reason isn't the connection's
		std::string LinkCloseReason;
	};
}
