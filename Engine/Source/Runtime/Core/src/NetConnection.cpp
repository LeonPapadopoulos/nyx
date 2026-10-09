#include "NyxPCH.h"
#include "NetConnection.h"

#include <algorithm>
#include <array>

#if defined(_WIN32)
	// winsock2.h has to come before anything that includes windows.h
	#ifndef WIN32_LEAN_AND_MEAN
		#define WIN32_LEAN_AND_MEAN
	#endif
	#ifndef NOMINMAX
		#define NOMINMAX
	#endif
	#include <winsock2.h>
	#include <ws2tcpip.h>
#else
	#include <arpa/inet.h>
	#include <cerrno>
	#include <fcntl.h>
	#include <netinet/in.h>
	#include <netinet/tcp.h>
	#include <poll.h>
	#include <sys/socket.h>
	#include <unistd.h>
#endif

namespace
{
	using SocketHandle = uintptr_t;
	constexpr SocketHandle InvalidSocket = ~SocketHandle{ 0 };

	// The few differences between Winsock and BSD sockets that this file needs
#if defined(_WIN32)
	using NativeSocket = SOCKET;
	using AddressLength = int;
	constexpr int SendFlags = 0;

	NativeSocket ToNative(SocketHandle socket)
	{
		return static_cast<NativeSocket>(socket);
	}

	int GetLastSocketError()
	{
		return ::WSAGetLastError();
	}

	bool IsWouldBlock(int error)
	{
		return error == WSAEWOULDBLOCK;
	}

	bool IsInterrupted(int error)
	{
		return error == WSAEINTR;
	}

	bool IsConnectInProgress(int error)
	{
		return error == WSAEWOULDBLOCK || error == WSAEINPROGRESS;
	}

	bool IsAcceptAborted(int error)
	{
		return error == WSAECONNRESET;
	}

	void CloseSocket(SocketHandle socket)
	{
		::closesocket(ToNative(socket));
	}

	// Winsock has to be started once per program. It is never shut down (WSACleanup): sockets
	// can live until the program ends, and the system cleans up after it.
	bool StartSockets()
	{
		static const bool bStarted = []
		{
			WSADATA data{};
			return ::WSAStartup(MAKEWORD(2, 2), &data) == 0;
		}();
		return bStarted;
	}

	// Non-blocking, and not inherited by programs this one starts
	bool PrepareSocket(SocketHandle socket)
	{
		// Only a precaution (ChildProcess doesn't pass handles on anyway), so a failure is ignored:
		// some network filter software makes it fail for sockets
		::SetHandleInformation(reinterpret_cast<HANDLE>(socket), HANDLE_FLAG_INHERIT, 0);

		u_long nonBlocking = 1;
		return ::ioctlsocket(ToNative(socket), FIONBIO, &nonBlocking) == 0;
	}
#else
	using NativeSocket = int;
	using AddressLength = socklen_t;

	#if defined(MSG_NOSIGNAL)
	// A broken connection reports an error instead of ending the program with SIGPIPE
	constexpr int SendFlags = MSG_NOSIGNAL;
	#else
	constexpr int SendFlags = 0;
	#endif

	NativeSocket ToNative(SocketHandle socket)
	{
		return static_cast<NativeSocket>(socket);
	}

	int GetLastSocketError()
	{
		return errno;
	}

	bool IsWouldBlock(int error)
	{
		return error == EAGAIN || error == EWOULDBLOCK;
	}

	bool IsInterrupted(int error)
	{
		return error == EINTR;
	}

	bool IsConnectInProgress(int error)
	{
		return error == EINPROGRESS;
	}

	bool IsAcceptAborted(int error)
	{
		return error == ECONNABORTED;
	}

	void CloseSocket(SocketHandle socket)
	{
		::close(ToNative(socket));
	}

	bool StartSockets()
	{
		return true;
	}

	bool PrepareSocket(SocketHandle socket)
	{
		const int flags = ::fcntl(ToNative(socket), F_GETFL, 0);
		return flags != -1 && ::fcntl(ToNative(socket), F_SETFL, flags | O_NONBLOCK) == 0 &&
			::fcntl(ToNative(socket), F_SETFD, FD_CLOEXEC) == 0;
	}
#endif

	SocketHandle CreateTcpSocket()
	{
		const NativeSocket socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#if defined(_WIN32)
		if (socket == INVALID_SOCKET)
#else
		if (socket < 0)
#endif
		{
			return InvalidSocket;
		}

		return static_cast<SocketHandle>(socket);
	}

	// Small messages go out at once instead of waiting to be combined with later ones
	void DisableSendDelay(SocketHandle socket)
	{
		const int enabled = 1;
		::setsockopt(ToNative(socket), IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&enabled), sizeof(enabled));
	}

	sockaddr_in MakeLocalAddress(uint16_t port)
	{
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_port = htons(port);
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		return address;
	}

	// The same English text on every system, with the code for looking up the rest
	std::string DescribeSocketError(int error)
	{
		const char* description = "socket error";
#if defined(_WIN32)
		switch (error)
		{
		case WSAECONNREFUSED: description = "connection refused"; break;
		case WSAECONNRESET: description = "connection reset"; break;
		case WSAECONNABORTED: description = "connection aborted"; break;
		case WSAETIMEDOUT: description = "timed out"; break;
		case WSAEADDRINUSE: description = "port already in use"; break;
		case WSAEACCES: description = "access denied"; break;
		default: break;
		}
#else
		switch (error)
		{
		case ECONNREFUSED: description = "connection refused"; break;
		case ECONNRESET: description = "connection reset"; break;
		case ECONNABORTED: description = "connection aborted"; break;
		case EPIPE: description = "connection broken"; break;
		case ETIMEDOUT: description = "timed out"; break;
		case EADDRINUSE: description = "port already in use"; break;
		case EACCES: description = "access denied"; break;
		default: break;
		}
#endif
		return std::string(description) + " (" + std::to_string(error) + ")";
	}

	void WriteUInt32LittleEndian(std::byte* out, uint32_t value)
	{
		for (int i = 0; i < 4; ++i)
		{
			out[i] = static_cast<std::byte>((value >> (8 * i)) & 0xFF);
		}
	}

	uint32_t ReadUInt32LittleEndian(const std::byte* in)
	{
		uint32_t value = 0;
		for (int i = 0; i < 4; ++i)
		{
			value |= static_cast<uint32_t>(in[i]) << (8 * i);
		}
		return value;
	}

	// Received bytes are drained in pieces of this size
	constexpr size_t ReceiveChunkSize = 64 * 1024;

	// Consumed bytes at the front of a buffer are removed once there are this many, instead of
	// moving the rest after every message
	constexpr size_t CompactThreshold = 64 * 1024;
}

namespace Nyx::Net
{
	void AppendMessage(std::vector<std::byte>& outBytes, uint16_t type, const std::vector<std::byte>& payload)
	{
		const size_t start = outBytes.size();
		outBytes.resize(start + MessageHeaderSize);
		WriteUInt32LittleEndian(outBytes.data() + start, static_cast<uint32_t>(payload.size()));
		outBytes[start + 4] = static_cast<std::byte>(type & 0xFF);
		outBytes[start + 5] = static_cast<std::byte>(type >> 8);
		outBytes.insert(outBytes.end(), payload.begin(), payload.end());
	}

	void MessageStreamReader::Append(const std::byte* data, size_t size)
	{
		if (bFailed)
		{
			return;
		}

		Buffer.insert(Buffer.end(), data, data + size);

		// Check every header as soon as it is complete, so a stream that announces a huge message
		// fails right away instead of waiting for bytes that never come
		while (Buffer.size() >= NextHeaderOffset + MessageHeaderSize)
		{
			const uint32_t payloadSize = ReadUInt32LittleEndian(Buffer.data() + NextHeaderOffset);
			if (payloadSize > MaxMessagePayloadSize)
			{
				bFailed = true;
				return;
			}

			NextHeaderOffset += MessageHeaderSize + payloadSize;
		}
	}

	std::optional<Message> MessageStreamReader::TryTake()
	{
		if (GetBufferedSize() < MessageHeaderSize)
		{
			return std::nullopt;
		}

		// After a failure, the messages before the bad header can still be taken
		const std::byte* header = Buffer.data() + ReadOffset;
		const uint32_t payloadSize = ReadUInt32LittleEndian(header);
		if (payloadSize > MaxMessagePayloadSize)
		{
			bFailed = true;
			return std::nullopt;
		}

		if (GetBufferedSize() < MessageHeaderSize + payloadSize)
		{
			return std::nullopt;
		}

		Message message;
		message.Type = static_cast<uint16_t>(static_cast<uint16_t>(header[4]) | (static_cast<uint16_t>(header[5]) << 8));
		message.Payload.assign(header + MessageHeaderSize, header + MessageHeaderSize + payloadSize);
		ReadOffset += MessageHeaderSize + payloadSize;

		if (ReadOffset == Buffer.size())
		{
			Buffer.clear();
			NextHeaderOffset -= ReadOffset;
			ReadOffset = 0;
		}
		else if (ReadOffset >= CompactThreshold)
		{
			Buffer.erase(Buffer.begin(), Buffer.begin() + static_cast<std::ptrdiff_t>(ReadOffset));
			NextHeaderOffset -= ReadOffset;
			ReadOffset = 0;
		}

		return message;
	}

	Connection::Connection(SocketHandle socket, EConnectionState state)
		: Socket(socket)
		, State(state)
	{
	}

	Connection::~Connection()
	{
		if (Socket != InvalidSocket)
		{
			CloseSocket(Socket);
		}
	}

	std::unique_ptr<Connection> Connection::ConnectToLocalPort(uint16_t port)
	{
		std::unique_ptr<Connection> connection(new Connection(InvalidSocket, EConnectionState::Connecting));
		connection->Port = port;

		const std::string target = "port " + std::to_string(port);

		if (!StartSockets())
		{
			connection->CloseWith(ECloseCause::Error, "couldn't connect to " + target + ": the system's sockets didn't start");
			return connection;
		}

		connection->Socket = CreateTcpSocket();
		if (connection->Socket == InvalidSocket || !PrepareSocket(connection->Socket))
		{
			connection->CloseWith(ECloseCause::Error,
				"couldn't connect to " + target + ": " + DescribeSocketError(GetLastSocketError()));
			return connection;
		}

		DisableSendDelay(connection->Socket);

		const sockaddr_in address = MakeLocalAddress(port);
		if (::connect(ToNative(connection->Socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0)
		{
			connection->State = EConnectionState::Connected;
			return connection;
		}

		const int error = GetLastSocketError();
		if (!IsConnectInProgress(error))
		{
			connection->CloseWith(ECloseCause::Error, "couldn't connect to " + target + ": " + DescribeSocketError(error));
		}

		return connection;
	}

	void Connection::Send(uint16_t type, const std::vector<std::byte>& payload)
	{
		if (State == EConnectionState::Closed)
		{
			return;
		}

		// The other side would refuse it and close the connection; better to fail here, clearly
		if (payload.size() > MaxMessagePayloadSize)
		{
			CloseWith(ECloseCause::Error, "a message of " + std::to_string(payload.size()) + " bytes is larger than the limit of " +
				std::to_string(MaxMessagePayloadSize / (1024 * 1024)) + " MB");
			return;
		}

		AppendMessage(SendBuffer, type, payload);

		if (State == EConnectionState::Connected)
		{
			SendQueued();
		}
	}

	void Connection::Poll()
	{
		if (State == EConnectionState::Connecting)
		{
			FinishConnecting();
		}

		// Receive first: if sending then fails because the other side is gone, what it sent
		// before is already here
		if (State == EConnectionState::Connected)
		{
			ReceiveAvailable();
		}

		if (State == EConnectionState::Connected)
		{
			SendQueued();
		}
	}

	std::optional<Message> Connection::Receive()
	{
		return Reader.TryTake();
	}

	void Connection::Close(const std::string& reason)
	{
		// Bytes left unread when the socket closes make the system reset the connection, which the
		// other side reports as an error. Taken in first, the other side sees a normal close, and
		// the messages among them can still be taken with Receive().
		if (State == EConnectionState::Connected)
		{
			int ignoredError = 0;
			ReadAvailable(ignoredError);
		}

		CloseWith(ECloseCause::ThisSide, reason);
	}

	void Connection::CloseWith(ECloseCause cause, const std::string& reason)
	{
		if (State == EConnectionState::Closed)
		{
			return;
		}

		if (Socket != InvalidSocket)
		{
			CloseSocket(Socket);
			Socket = InvalidSocket;
		}

		State = EConnectionState::Closed;
		CloseCause = cause;
		CloseReason = reason;
		SendBuffer.clear();
		SendOffset = 0;
	}

	void Connection::FinishConnecting()
	{
		// Without waiting: is the socket writable (connected) or in error (failed)?
		bool bDone = false;
#if defined(_WIN32)
		fd_set writable;
		fd_set failed;
		FD_ZERO(&writable);
		FD_ZERO(&failed);
		FD_SET(ToNative(Socket), &writable);
		FD_SET(ToNative(Socket), &failed);
		timeval noWait{};
		const int ready = ::select(0, nullptr, &writable, &failed, &noWait);
		bDone = ready > 0;
#else
		pollfd entry{};
		entry.fd = ToNative(Socket);
		entry.events = POLLOUT;
		const int ready = ::poll(&entry, 1, 0);
		bDone = ready > 0;
#endif
		if (!bDone)
		{
			if (ready < 0 && !IsInterrupted(GetLastSocketError()))
			{
				CloseWith(ECloseCause::Error, "couldn't connect to port " + std::to_string(Port) + ": " +
					DescribeSocketError(GetLastSocketError()));
			}
			return;
		}

		int error = 0;
		AddressLength errorSize = sizeof(error);
		if (::getsockopt(ToNative(Socket), SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&error), &errorSize) != 0)
		{
			error = GetLastSocketError();
		}

		if (error != 0)
		{
			CloseWith(ECloseCause::Error, "couldn't connect to port " + std::to_string(Port) + ": " + DescribeSocketError(error));
			return;
		}

		State = EConnectionState::Connected;
	}

	void Connection::SendQueued()
	{
		while (SendOffset < SendBuffer.size())
		{
			const size_t remaining = SendBuffer.size() - SendOffset;
			const int chunkSize = static_cast<int>(std::min<size_t>(remaining, 1024 * 1024));
			const auto sent = ::send(ToNative(Socket), reinterpret_cast<const char*>(SendBuffer.data() + SendOffset), chunkSize, SendFlags);

			if (sent > 0)
			{
				SendOffset += static_cast<size_t>(sent);
				continue;
			}

			const int error = GetLastSocketError();
			if (sent == 0 || IsWouldBlock(error))
			{
				// The system's buffer is full; the next Poll() continues
				break;
			}

			if (IsInterrupted(error))
			{
				continue;
			}

			// Usually the other side is gone. Take in what it sent before, then close.
			int readError = 0;
			if (ReadAvailable(readError) == EReadEnd::OtherSideClosed)
			{
				CloseWith(ECloseCause::OtherSide, "the other side closed the connection");
			}
			else
			{
				CloseWith(ECloseCause::Error, "sending failed: " + DescribeSocketError(error));
			}
			return;
		}

		if (SendOffset == SendBuffer.size())
		{
			SendBuffer.clear();
			SendOffset = 0;
		}
		else if (SendOffset >= CompactThreshold)
		{
			SendBuffer.erase(SendBuffer.begin(), SendBuffer.begin() + static_cast<std::ptrdiff_t>(SendOffset));
			SendOffset = 0;
		}
	}

	void Connection::ReceiveAvailable()
	{
		int error = 0;
		switch (ReadAvailable(error))
		{
		case EReadEnd::NothingMore:
			break;
		case EReadEnd::OtherSideClosed:
			CloseWith(ECloseCause::OtherSide, "the other side closed the connection");
			break;
		case EReadEnd::NotMessages:
			CloseWith(ECloseCause::Error, "the other side sent a message larger than " +
				std::to_string(MaxMessagePayloadSize / (1024 * 1024)) + " MB, or something that isn't a message");
			break;
		case EReadEnd::Failed:
			CloseWith(ECloseCause::Error, "receiving failed: " + DescribeSocketError(error));
			break;
		}
	}

	Connection::EReadEnd Connection::ReadAvailable(int& outError)
	{
		std::array<std::byte, ReceiveChunkSize> chunk;

		for (;;)
		{
			const auto received = ::recv(ToNative(Socket), reinterpret_cast<char*>(chunk.data()), static_cast<int>(chunk.size()), 0);

			if (received > 0)
			{
				Reader.Append(chunk.data(), static_cast<size_t>(received));
				if (Reader.HasFailed())
				{
					return EReadEnd::NotMessages;
				}
				continue;
			}

			if (received == 0)
			{
				return EReadEnd::OtherSideClosed;
			}

			outError = GetLastSocketError();
			if (IsWouldBlock(outError))
			{
				return EReadEnd::NothingMore;
			}

			if (!IsInterrupted(outError))
			{
				return EReadEnd::Failed;
			}
		}
	}

	Listener::~Listener()
	{
		Close();
	}

	bool Listener::IsListening() const
	{
		return Socket != InvalidSocket;
	}

	bool Listener::Listen(uint16_t port)
	{
		Close();
		LastError.clear();

		if (!StartSockets())
		{
			LastError = "the system's sockets didn't start";
			return false;
		}

		const SocketHandle socket = CreateTcpSocket();
		if (socket == InvalidSocket)
		{
			LastError = DescribeSocketError(GetLastSocketError());
			return false;
		}

#if defined(_WIN32)
		// No other program may take over the port while this one listens on it
		const int exclusive = 1;
		::setsockopt(ToNative(socket), SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#endif

		const sockaddr_in address = MakeLocalAddress(port);
		sockaddr_in boundAddress{};
		AddressLength boundAddressSize = sizeof(boundAddress);

		const bool bListening =
			::bind(ToNative(socket), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) == 0 &&
			::listen(ToNative(socket), 8) == 0 &&
			PrepareSocket(socket) &&
			::getsockname(ToNative(socket), reinterpret_cast<sockaddr*>(&boundAddress), &boundAddressSize) == 0;

		if (!bListening)
		{
			LastError = DescribeSocketError(GetLastSocketError());
			CloseSocket(socket);
			return false;
		}

		Socket = socket;
		Port = ntohs(boundAddress.sin_port);
		return true;
	}

	std::unique_ptr<Connection> Listener::Accept()
	{
		while (IsListening())
		{
			const NativeSocket accepted = ::accept(ToNative(Socket), nullptr, nullptr);
#if defined(_WIN32)
			const bool bAccepted = accepted != INVALID_SOCKET;
#else
			const bool bAccepted = accepted >= 0;
#endif
			if (!bAccepted)
			{
				const int error = GetLastSocketError();
				if (IsInterrupted(error) || IsAcceptAborted(error))
				{
					// The program gave up connecting before it was accepted; look at the next one
					continue;
				}

				return nullptr;
			}

			const SocketHandle socket = static_cast<SocketHandle>(accepted);
			if (!PrepareSocket(socket))
			{
				CloseSocket(socket);
				return nullptr;
			}

			DisableSendDelay(socket);
			return std::unique_ptr<Connection>(new Connection(socket, EConnectionState::Connected));
		}

		return nullptr;
	}

	void Listener::Close()
	{
		if (Socket != InvalidSocket)
		{
			CloseSocket(Socket);
			Socket = InvalidSocket;
		}

		Port = 0;
	}
}
