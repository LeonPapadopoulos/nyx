#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

// TCP connections between programs on this machine, such as the editor and a running game.
// Nothing blocks and there are no threads: the owner calls Poll() once per frame.
namespace Nyx::Net
{
	// One message as it travels over a Connection: a type number, and payload bytes that only the
	// protocol using the connection understands.
	struct Message
	{
		uint16_t Type = 0;
		std::vector<std::byte> Payload;
	};

	// On the wire, a message is: payload size (u32) | type (u16) | payload, little-endian.
	constexpr size_t MessageHeaderSize = 6;

	// A message announcing a larger payload ends the connection, so a damaged stream, or a program
	// that isn't speaking this format, can't make the receiver allocate gigabytes.
	constexpr uint32_t MaxMessagePayloadSize = 64u * 1024u * 1024u;

	// Appends one message, as it travels on the wire, to the bytes.
	void AppendMessage(std::vector<std::byte>& outBytes, uint16_t type, const std::vector<std::byte>& payload);

	// Turns bytes, arriving in pieces of any size, back into whole messages.
	class MessageStreamReader
	{
	public:
		void Append(const std::byte* data, size_t size);

		// The next message, once all of its bytes have arrived
		std::optional<Message> TryTake();

		// Set once the stream announced a message larger than MaxMessagePayloadSize. Messages before
		// that can still be taken; nothing after it, since the start of the next one is unknown.
		bool HasFailed() const
		{
			return bFailed;
		}

		// Bytes received that aren't part of a taken message yet
		size_t GetBufferedSize() const
		{
			return Buffer.size() - ReadOffset;
		}

	private:
		std::vector<std::byte> Buffer;
		size_t ReadOffset = 0;

		// Where the first header not checked yet starts (it may not have arrived yet)
		size_t NextHeaderOffset = 0;

		bool bFailed = false;
	};

	enum class EConnectionState : uint8_t
	{
		Connecting,
		Connected,
		Closed,
	};

	// Who ended a connection, which tells an expected end from a problem
	enum class ECloseCause : uint8_t
	{
		None,       // Still open
		ThisSide,   // Close() was called
		OtherSide,  // The other program closed it, for example because it exited
		Error,      // Connecting failed, the connection broke, or the other side sent something unreadable
	};

	// One end of a TCP connection. It comes either from ConnectToLocalPort() or from a Listener,
	// and works the same either way, so either program can be the one that listens.
	class Connection
	{
	public:
		// Starts connecting to a program on this machine (127.0.0.1) that listens on the port.
		// Returns at once: the connection is Connecting until Poll() finds it established, or
		// Closed if it failed (GetCloseReason() says why). Never returns nullptr.
		static std::unique_ptr<Connection> ConnectToLocalPort(uint16_t port);

		~Connection();

		Connection(const Connection&) = delete;
		Connection& operator=(const Connection&) = delete;

		EConnectionState GetState() const
		{
			return State;
		}

		bool IsConnected() const
		{
			return State == EConnectionState::Connected;
		}

		ECloseCause GetCloseCause() const
		{
			return CloseCause;
		}

		// Why the connection is Closed, e.g. "the other side closed the connection"
		const std::string& GetCloseReason() const
		{
			return CloseReason;
		}

		// Queues the message and sends as much as the system takes right away; Poll() sends the
		// rest. While Connecting, it waits until the connection is established. Ignored once Closed.
		// A payload larger than MaxMessagePayloadSize closes the connection with an error.
		void Send(uint16_t type, const std::vector<std::byte>& payload);

		// Finishes connecting, sends what is queued and receives what has arrived. Never blocks.
		void Poll();

		// The next message that arrived, in order. Messages that arrived before the connection
		// closed can still be taken afterwards.
		std::optional<Message> Receive();

		// Closes at once. Queued messages that haven't been handed to the system yet are dropped;
		// messages that have arrived can still be taken with Receive().
		void Close(const std::string& reason);

		// Sends what is queued, tells the other side that nothing more comes, and waits until it
		// closes its end too, for at most timeLimit; then closes. Blocks while it waits. Messages
		// that arrive meanwhile can still be taken with Receive().
		void CloseGracefully(const std::string& reason, std::chrono::milliseconds timeLimit);

		// Bytes queued that the system hasn't taken yet
		size_t GetUnsentSize() const
		{
			return SendBuffer.size() - SendOffset;
		}

	private:
		friend class Listener;

		// The operating system's socket (a Windows SOCKET, or a file descriptor elsewhere)
		using SocketHandle = uintptr_t;

		Connection(SocketHandle socket, EConnectionState state);

		enum class EReadEnd : uint8_t
		{
			NothingMore,      // Everything the system has received is read
			OtherSideClosed,
			NotMessages,      // The bytes announce a message larger than MaxMessagePayloadSize
			Failed,
		};

		void FinishConnecting();
		void SendQueued();
		void ReceiveAvailable();

		// Reads everything the system has received into the reader, without closing anything.
		// On Failed, outError is the socket error.
		EReadEnd ReadAvailable(int& outError);

		void CloseWith(ECloseCause cause, const std::string& reason);

	private:
		SocketHandle Socket;
		EConnectionState State;
		ECloseCause CloseCause = ECloseCause::None;
		std::string CloseReason;

		uint16_t Port = 0;
		std::vector<std::byte> SendBuffer;
		size_t SendOffset = 0;
		MessageStreamReader Reader;
	};

	// Waits for programs on this machine to connect.
	class Listener
	{
	public:
		Listener() = default;
		~Listener();

		Listener(const Listener&) = delete;
		Listener& operator=(const Listener&) = delete;

		// Listens on 127.0.0.1, so only programs on this machine can connect (and the firewall
		// doesn't ask). Port 0 lets the system pick a free port; GetPort() tells which. On failure,
		// GetError() says why.
		bool Listen(uint16_t port = 0);

		bool IsListening() const;

		uint16_t GetPort() const
		{
			return Port;
		}

		const std::string& GetError() const
		{
			return LastError;
		}

		// A program that connected since the last call, already Connected. Never blocks;
		// nullptr if no one is waiting.
		std::unique_ptr<Connection> Accept();

		void Close();

	private:
		uintptr_t Socket = ~uintptr_t{ 0 };
		uint16_t Port = 0;
		std::string LastError;
	};
}
